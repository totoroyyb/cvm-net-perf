#include <linux/atomic.h> // Kernel atomics (Needed for functions now)
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/ktime.h> // For ktime_get_ns()
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>   // For smp_processor_id()
#include <linux/slab.h>    // For kcalloc/kfree
#include <linux/smp.h>     // For memory barriers smp_wmb/rmb
#include <linux/uaccess.h> // For copy_to_user etc (if using ioctl)
#include <linux/version.h>
#include <linux/vmalloc.h> // For vmalloc/vfree if using that
#include <linux/stddef.h>  // For offsetof if needed

// Include the common header with PLAIN DATA TYPES
#include "../shared/common.h"

#define DEVICE_NAME "khires"
#define CLASS_NAME "hireslogger"

// --- Module Parameters ---
// Use the default from the header unless overridden
static int rb_size_log2 = RING_BUFFER_LOG2_SIZE;
module_param(rb_size_log2, int, S_IRUGO);
MODULE_PARM_DESC(rb_size_log2, "Log2 of the ring buffer size in entries");

// --- Global Variables ---
static dev_t dev_num;
static struct cdev hires_cdev;
static struct class *hireslogger_class = NULL;
static shared_ring_buffer_t *shared_buffer = NULL; // Kernel virtual address mapping (usually just first page)
static unsigned long buffer_total_size = 0; // Total size in bytes (page aligned)
static unsigned long buffer_num_pages = 0;
static struct page **buffer_pages = NULL; // Array holding buffer pages

// --- Forward Declarations ---
static int hireslogger_dev_open(struct inode *, struct file *);
static int hireslogger_dev_release(struct inode *, struct file *);
static int hireslogger_dev_mmap(struct file *filp, struct vm_area_struct *vma);
int hires_log(uint32_t event_id, uint64_t data1, uint64_t data2);

// --- File Operations ---
static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = hireslogger_dev_open,
    .release = hireslogger_dev_release,
    .mmap = hireslogger_dev_mmap,
    // .unlocked_ioctl = profiler_dev_ioctl, // Add if ioctl is needed
};

// --- Mmap Helper Functions (using alloc_pages) ---

// Called when a userspace process accesses a mapped page for the first time
static vm_fault_t hireslogger_vma_fault(struct vm_fault *vmf) {
  struct page *page;
  unsigned long offset = vmf->pgoff; // Page offset into the mapping

  // Use READ_ONCE for buffer_num_pages in case of future modifications (unlikely here)
  if (!buffer_pages || offset >= READ_ONCE(buffer_num_pages)) {
    return VM_FAULT_SIGBUS; // Invalid offset
  }

  page = buffer_pages[offset]; // Get the corresponding physical page
  if (!page) {
    return VM_FAULT_SIGBUS; // Should not happen if allocation succeeded
  }

  get_page(page); // Increment page reference count
  vmf->page = page;

  return 0; // Success
}

// Define vm_ops for our mapped area
static const struct vm_operations_struct hireslogger_vm_ops = {
    .fault = hireslogger_vma_fault,
};

// --- Device Operations Implementation ---
static int hireslogger_dev_open(struct inode *inodep, struct file *filep) {
  // Could track number of openers if needed
  pr_info("kHiResLogger: Device opened.\n");
  return 0;
}

static int hireslogger_dev_release(struct inode *inodep, struct file *filep) {
  pr_info("kHiResLogger: Device closed.\n");
  return 0;
}

static int hireslogger_dev_mmap(struct file *filp, struct vm_area_struct *vma) {
  unsigned long requested_size = vma->vm_end - vma->vm_start;
  // unsigned long physical_pfn; // Needed only for remap_pfn_range
  // int ret; // Needed only for remap_pfn_range

  pr_info("kHiResLogger: mmap called. Requested size: %lu, Buffer size: %lu\n",
          requested_size, READ_ONCE(buffer_total_size));

  // Check if requested size matches our buffer size
  if (requested_size > READ_ONCE(buffer_total_size) || vma->vm_pgoff != 0) {
      pr_err("kHiResLogger: Invalid mmap request. ReqSize=%lu > BufSize=%lu or PageOffset=%lu != 0\n",
             requested_size, READ_ONCE(buffer_total_size), vma->vm_pgoff);
      return -EINVAL;
  }

  // Prevent modifications to vma->vm_flags & co after this point
  vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
  // Set our custom vm_ops to handle page faults on demand
  vma->vm_ops = &hireslogger_vm_ops;

  pr_info("kHiResLogger: mmap successful using page fault handler.\n");
  return 0;

  /* --- Alternative: Using remap_pfn_range (Not suitable for vmalloc or page arrays) ---
     If the buffer were allocated as a single contiguous block (e.g., small kmalloc
     or low-order alloc_pages), remap_pfn_range could be used. But with the
     current page array approach, the fault handler is necessary.
  */
}

// --- Kernel Producer Logging API ---
// Exported function for other kernel parts to call
/**
 * hires_log - Log an event from kernel context.
 * @event_id: Identifier for the event type.
 * @data1: Custom data payload 1.
 * @data2: Custom data payload 2.
 *
 * Returns 0 on success, -ENOMEM if buffer is full, -EIO if not initialized.
 */
int hires_log(uint32_t event_id, uint64_t data1, uint64_t data2) {
  prof_size_t head_val, tail_val, next_head_val, current_idx;
  log_entry_t *entry;
  uint16_t old_flags, new_flags;

  // Use READ_ONCE for shared_buffer check for robustness
  if (unlikely(!READ_ONCE(shared_buffer))) {
    // Module not initialized or buffer allocation failed
    return -EIO;
  }

  // 1. Atomically reserve a slot (Acquire semantics ensure tail read below is ordered after)
  //    We fetch-and-add, then calculate the index from the *previous* head value.
  //    Using atomic64_fetch_add_acquire on the *address* of the plain u64 head field.
  head_val = atomic64_fetch_add_acquire(1, (atomic64_t*)&shared_buffer->head);
  current_idx = head_val & shared_buffer->size_mask; // Use mask from header

  // 2. Check if buffer is full (using Acquire semantics for tail read)
  //    We compare the *next* potential head position against the current tail.
  //    Read tail atomically using atomic64_read_acquire on its address.
  tail_val = atomic64_read_acquire((atomic64_t*)&shared_buffer->tail);

  // If the slot we're about to write (current_idx) is the same as the tail,
  // and head has already wrapped around past tail, the buffer is full.
  if (unlikely(current_idx == tail_val && (head_val - tail_val) >= shared_buffer->buffer_size)) {
      // Buffer is full. Increment dropped count atomically.
      // No need to roll back head with fetch_add.
      atomic64_inc((atomic64_t*)&shared_buffer->dropped_count);
      return -ENOMEM; // Indicate buffer full
  }

  // 3. Get pointer to the entry in the buffer
  //    This calculation relies on shared_buffer pointing to the start of the
  //    mmappable region, and the buffer array being correctly placed.
  //    Careful if shared_buffer only maps the first page! Access needs validation
  //    if buffer spans pages and shared_buffer is only a partial kernel mapping.
  //    Assuming for now the kernel has full access via shared_buffer (e.g., vmap).
  //    If using page_address(buffer_pages[0]), direct access beyond PAGE_SIZE is invalid.
  //    *** This needs careful handling depending on allocation strategy ***
  //    Let's assume shared_buffer IS the correct kernel virtual address for the whole region.
  entry = &shared_buffer->buffer[current_idx];

  // 4. Fill in the data (flags field handled atomically later)
  //    Direct writes to plain struct members.
  entry->timestamp = ktime_get_ns(); // High-resolution monotonic timestamp
  entry->event_id = event_id;
  entry->cpu_id = (uint16_t)raw_smp_processor_id(); // Use raw version inside preemption-unsafe sections if needed
  entry->data1 = data1;
  entry->data2 = data2;

  // 5. Write Memory Barrier: Ensure all prior writes to the entry data payload
  //    are globally visible before the atomic update to the 'flags' field.
  smp_wmb();

  // 6. Atomically set the flags including the VALID bit using cmpxchg for uint16_t.
  //    This provides release semantics implicitly on success on most architectures,
  //    making the written data visible to the consumer.
  do {
    // Read the current flags value (non-atomically is okay inside CAS loop)
    old_flags = READ_ONCE(entry->flags);
    // Prepare the new flags value, ensuring VALID bit is set and kernel bit.
    new_flags = (old_flags & ~LOG_FLAG_VALID) | LOG_FLAG_VALID | LOG_FLAG_KERNEL;
    // Attempt atomic swap. cmpxchg returns the *old* value. Loop if it wasn't what we read.
  } while (cmpxchg(&entry->flags, old_flags, new_flags) != old_flags);
  // --- Entry is now visible to consumer ---

  return 0; // Success
}

// Export the function for use in the kernel.
EXPORT_SYMBOL(hires_log);

// --- Module Initialization and Exit ---
static int __init hireslogger_km_init(void) {
  int ret = 0;
  size_t i;
  unsigned long calculated_ring_buffer_entries;
  // Use the macro from the header if available, otherwise calculate offset manually
  unsigned long calculated_buffer_ctrl_size = SHARED_RING_BUFFER_CTRL_SIZE; // From common.h
  unsigned long calculated_buffer_total_size_unaligned;

  pr_info("kHiResLogger: Initializing module...\n");

  calculated_ring_buffer_entries = (1UL << rb_size_log2);
  // Calculate total size needed based on control block size + actual buffer array size
  calculated_buffer_total_size_unaligned =
      calculated_buffer_ctrl_size +
      (calculated_ring_buffer_entries * sizeof(log_entry_t));

  // Align total size UP to the nearest page boundary
  buffer_total_size = PAGE_ALIGN(calculated_buffer_total_size_unaligned);
  buffer_num_pages = buffer_total_size / PAGE_SIZE;

  pr_info("kHiResLogger: Requested log2_size=%d, Ring buffer entries=%lu, "
          "Ctrl size=%lu, Total size unaligned=%lu, Total size aligned=%lu (%lu pages)\n",
          rb_size_log2, calculated_ring_buffer_entries,
          calculated_buffer_ctrl_size, calculated_buffer_total_size_unaligned,
          buffer_total_size, buffer_num_pages);

  // 1. Allocate page array and the pages themselves
  buffer_pages = kcalloc(buffer_num_pages, sizeof(struct page *), GFP_KERNEL);
  if (!buffer_pages) {
    pr_err("kHiResLogger: Failed to allocate page pointer array\n");
    return -ENOMEM;
  }

  for (i = 0; i < buffer_num_pages; ++i) {
    // Allocate pages with GFP_KERNEL | __GFP_ZERO to get zeroed memory
    buffer_pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);
    if (!buffer_pages[i]) {
      pr_err("kHiResLogger: Failed to allocate page %zu\n", i);
      ret = -ENOMEM;
      goto fail_alloc_pages;
    }
  }

  // ---- IMPORTANT KERNEL VIRTUAL ADDRESS MAPPING ----
  // We need a contiguous kernel virtual mapping of potentially non-contiguous physical pages
  // vmap() is the standard way to achieve this.
  shared_buffer = vmap(buffer_pages, buffer_num_pages, VM_MAP, PAGE_KERNEL);
  if (!shared_buffer) {
      pr_err("kHiResLogger: Failed to vmap page array\n");
      ret = -ENOMEM;
      goto fail_alloc_pages;
  }
  // Now, shared_buffer is a valid kernel virtual address for the *entire* allocated range.
  // kcalloc already zeroed the pages, so the buffer starts zeroed.


  // 2. Initialize the shared buffer structure header (at the start of the vmap'd region)
  pr_info("kHiResLogger: Initializing shared buffer header at %px\n", shared_buffer);
  // Direct writes to plain integer fields
  shared_buffer->buffer_size = calculated_ring_buffer_entries;
  shared_buffer->size_mask = calculated_ring_buffer_entries - 1;
  // Use atomic64_set on the *address* of the plain u64 fields
  atomic64_set((atomic64_t*)&shared_buffer->head, 0);
  atomic64_set((atomic64_t*)&shared_buffer->tail, 0);
  atomic64_set((atomic64_t*)&shared_buffer->dropped_count, 0);

  // 3. Allocate device number
  ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
  if (ret < 0) {
    pr_err("kHiResLogger: Failed to allocate major number: %d\n", ret);
    goto fail_vmap; // Use the vmap cleanup path
  }
  pr_info("kHiResLogger: Major number allocated: %d\n", MAJOR(dev_num));

  // 4. Create device class
  hireslogger_class = class_create(CLASS_NAME);
  if (IS_ERR(hireslogger_class)) {
    pr_err("kHiResLogger: Failed to create device class: %ld\n",
           PTR_ERR(hireslogger_class));
    ret = PTR_ERR(hireslogger_class);
    goto fail_chrdev_region;
  }
  pr_info("kHiResLogger: Device class created successfully.\n");

  // 5. Create character device
  cdev_init(&hires_cdev, &fops);
  hires_cdev.owner = THIS_MODULE;
  ret = cdev_add(&hires_cdev, dev_num, 1);
  if (ret < 0) {
    pr_err("kHiResLogger: Failed to add cdev: %d\n", ret);
    goto fail_class_create;
  }
  pr_info("kHiResLogger: Character device added successfully.\n");

  // 6. Create device node (/dev/khires)
  device_create(hireslogger_class, NULL, dev_num, NULL, DEVICE_NAME);
  pr_info("kHiResLogger: Device node /dev/%s created.\n", DEVICE_NAME);

  pr_info("kHiResLogger: Module loaded successfully.\n");
  return 0; // Success

// --- Error Handling Cleanup ---
fail_class_create:
  class_destroy(hireslogger_class);
fail_chrdev_region:
  unregister_chrdev_region(dev_num, 1);
fail_vmap:
  if(shared_buffer) {
      vunmap(shared_buffer);
      shared_buffer = NULL;
  }
fail_alloc_pages:
  if (buffer_pages) {
    for (i = 0; i < buffer_num_pages; ++i) {
      if (buffer_pages[i]) {
        __free_page(buffer_pages[i]);
      }
    }
    kfree(buffer_pages);
    buffer_pages = NULL;
  }
  // Ensure pointer is null on failure if vmap wasn't reached or failed
  if (!shared_buffer) {
       shared_buffer = NULL;
  }
  pr_err("kHiResLogger: Module initialization failed with error %d.\n", ret);
  return ret;
}

static void __exit hireslogger_km_exit(void) {
  size_t i;
  pr_info("kHiResLogger: Exiting module...\n");

  // Destroy device node
  device_destroy(hireslogger_class, dev_num);
  // Destroy character device
  cdev_del(&hires_cdev);
  // Destroy device class
  class_destroy(hireslogger_class);
  // Unregister device number
  unregister_chrdev_region(dev_num, 1);

  // Free buffer memory
  // Unmap the kernel virtual mapping first
  if (shared_buffer) {
    vunmap(shared_buffer);
    shared_buffer = NULL;
  }
  // Then free the physical pages and the page array
  if (buffer_pages) {
    for (i = 0; i < buffer_num_pages; ++i) {
      if (buffer_pages[i]) {
        __free_page(buffer_pages[i]);
      }
    }
    kfree(buffer_pages);
    buffer_pages = NULL;
  }

  pr_info("kHiResLogger: Module unloaded.\n");
}

module_init(hireslogger_km_init);
module_exit(hireslogger_km_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yibo Yan (Modified by AI)");
MODULE_DESCRIPTION(
    "HiResLogger Kernel Module with MPSC Ring Buffer via mmap (Plain Types)");
MODULE_VERSION("0.2");