#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/delay.h> // For msleep/udelay
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
// #include <linux/ktime.h>  // For ktime_get_ns()
#include <linux/math64.h> // For div64_u64
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>   // For smp_processor_id()
#include <linux/slab.h>    // For kcalloc/kfree
#include <linux/smp.h>     // For memory barriers smp_wmb/rmb
#include <linux/stddef.h>  // For offsetof if needed
#include <linux/uaccess.h> // For copy_to_user etc (if using ioctl)
#include <linux/version.h>
#include <linux/vmalloc.h> // For vmalloc/vfree if using that

#include "../shared/common.h"
#include "../shared/ops.h"

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
static shared_ring_buffer_t *shared_buffer = NULL;
// Total size in bytes (page aligned)
static unsigned long buffer_total_size = 0;
static unsigned long buffer_num_pages = 0;
// Array holding buffer pages
static struct page **buffer_pages = NULL;

// --- Forward Declarations ---
static int hireslogger_dev_open(struct inode *, struct file *);
static int hireslogger_dev_release(struct inode *, struct file *);
static int hireslogger_dev_mmap(struct file *filp, struct vm_area_struct *vma);
static long hireslogger_dev_ioctl(struct file *filp, unsigned int cmd,
                                  unsigned long arg);
static u64 hires_calibrate_tsc(void);
int hires_log(u32 event_id, u64 data1, u64 data2);
u64 hires_rdtsc(void);
u64 hires_rdtscp(u32 *auxp);

// --- TSC Calibration ---
static u64 cycles_per_us PROF_CACHE_LINE_ALIGNED = 0;

// Helper function to calibrate TSC frequency (cycles per microsecond)
static u64 hires_calibrate_tsc(void) {
  u64 start_tsc, end_tsc, elapsed_tsc;
  ktime_t start_time, end_time;
  s64 elapsed_ns;
  const unsigned int delay_ms = 500;

  // Prevent migration during measurement
  preempt_disable();
  cpu_serialize();
  start_time = ktime_get();
  start_tsc = __rdtsc();
  preempt_enable();

  msleep(delay_ms);

  preempt_disable();
  end_tsc = __rdtscp(NULL);
  end_time = ktime_get();
  preempt_enable();

  elapsed_tsc = end_tsc - start_tsc;
  elapsed_ns = ktime_to_ns(ktime_sub(end_time, start_time));

  if (elapsed_ns <= 0) {
    pr_warn("kHiResLogger: TSC calibration failed (elapsed_ns <= 0)\n");
    return 0;
  }

  // Calculate cycles per microsecond: (cycles * 1,000) / ns
  cycles_per_us = div64_u64(elapsed_tsc * 1000, elapsed_ns);
  return cycles_per_us;
}

// --- File Operations ---
static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = hireslogger_dev_open,
    .release = hireslogger_dev_release,
    .mmap = hireslogger_dev_mmap,
    .unlocked_ioctl = hireslogger_dev_ioctl,
};

// --- Mmap Helper Functions (using alloc_pages) ---
static vm_fault_t hireslogger_vma_fault(struct vm_fault *vmf) {
  struct page *page;
  unsigned long offset = vmf->pgoff;

  if (!buffer_pages || offset >= READ_ONCE(buffer_num_pages)) {
    return VM_FAULT_SIGBUS;
  }

  page = buffer_pages[offset];
  if (!page) {
    return VM_FAULT_SIGBUS;
  }

  // Increment page reference count
  get_page(page);
  vmf->page = page;

  return 0;
}

static const struct vm_operations_struct hireslogger_vm_ops = {
    .fault = hireslogger_vma_fault,
};

// --- Device Operations Implementation ---
static int hireslogger_dev_open(struct inode *inodep, struct file *filep) {
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

  if (requested_size > READ_ONCE(buffer_total_size) || vma->vm_pgoff != 0) {
    pr_err("kHiResLogger: Invalid mmap request. ReqSize=%lu > BufSize=%lu or "
           "PageOffset=%lu != 0\n",
           requested_size, READ_ONCE(buffer_total_size), vma->vm_pgoff);
    return -EINVAL;
  }

  vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
  vma->vm_ops = &hireslogger_vm_ops;

  pr_info("kHiResLogger: mmap successful using page fault handler.\n");
  return 0;
}

// --- IOCTL Handler ---
static long hireslogger_dev_ioctl(struct file *filp, unsigned int cmd,
                                  unsigned long arg) {
  int ret = 0;
  prof_size_t i;
  void __user *user_ptr = (void __user *)arg;

  // Check if the shared buffer is initialized
  if (unlikely(!READ_ONCE(shared_buffer))) {
    pr_warn("kHiResLogger: IOCTL called before buffer initialization.\n");
    return -EIO;
  }

  switch (cmd) {
  case HIRES_IOCTL_RESET_RB:
    pr_info("kHiResLogger: IOCTL: Resetting buffer.\n");

    // Atomically reset head, tail, and dropped count
    atomic64_set((atomic64_t *)&shared_buffer->head, 0);
    atomic64_set((atomic64_t *)&shared_buffer->tail, 0);
    atomic64_set((atomic64_t *)&shared_buffer->dropped_count, 0);

    smp_wmb();

    // Clear the VALID flag for all entries to prevent stale reads
    // This might contend with producers, but reset is usually infrequent.
    // A more complex scheme could involve a generation count.
    for (i = 0; i < shared_buffer->capacity; ++i) {
      uint16_t old_flags, new_flags;
      log_entry_t *entry = &shared_buffer->buffer[i];
      do {
        old_flags = READ_ONCE(entry->flags);
        new_flags = old_flags & ~LOG_FLAG_VALID;
      } while (cmpxchg(&entry->flags, old_flags, new_flags) != old_flags);
    }
    smp_wmb();
    ret = 0;
    break;

  case HIRES_IOCTL_GET_RB_META: {
    hires_rb_meta_t meta;

    pr_info("kHiResLogger: IOCTL: Get buffer info.\n");

    meta.capacity = READ_ONCE(shared_buffer->capacity);
    meta.idx_mask = READ_ONCE(shared_buffer->idx_mask);
    meta.shm_size_bytes_unaligned =
        READ_ONCE(shared_buffer->shm_size_bytes_unaligned);

    if (copy_to_user(user_ptr, &meta, sizeof(meta))) {
      pr_err("kHiResLogger: IOCTL: Failed to copy buffer info to user.\n");
      ret = -EFAULT;
    } else {
      ret = 0;
    }
    break;
  }

  case HIRES_IOCTL_GET_TSC_CYCLE_PER_MS: {
    pr_info("kHiResLogger: IOCTL: Get calibrated TSC freq (cycles/ms).\n");
    if (cycles_per_us == 0) {
      pr_err("kHiResLogger: TSC freq not calibrated yet or error happened.\n");
      ret = -EFAULT;
      break;
    }

    if (put_user(cycles_per_us, (u64 __user *)user_ptr)) {
      pr_err("kHiResLogger: IOCTL: Failed to copy TSC freq to user.\n");
      ret = -EFAULT;
    } else {
      ret = 0;
    }
    break;
  }

  default:
    pr_warn("kHiResLogger: IOCTL: Unknown command %u.\n", cmd);
    ret = -ENOTTY;
    break;
  }

  return ret;
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
int hires_log(u32 event_id, u64 data1, u64 data2) {
  prof_size_t head_val, tail_val, next_head_val, current_idx;
  log_entry_t *entry;
  uint16_t old_flags, new_flags;

  // Use READ_ONCE for shared_buffer check for robustness
  if (unlikely(!READ_ONCE(shared_buffer))) {
    // Module not initialized or buffer allocation failed
    return -EIO;
  }

  // 1. Atomically reserve a slot (Acquire semantics ensure tail read below is
  // ordered after)
  //    We fetch-and-add, then calculate the index from the *previous* head
  //    value. Using atomic64_fetch_add_acquire on the *address* of the plain
  //    u64 head field.
  head_val = atomic64_fetch_add_acquire(1, (atomic64_t *)&shared_buffer->head);
  current_idx = head_val & shared_buffer->idx_mask; // Use mask from header

  // 2. Check if buffer is full (using Acquire semantics for tail read)
  //    We compare the *next* potential head position against the current tail.
  //    Read tail atomically using atomic64_read_acquire on its address.
  tail_val = atomic64_read_acquire((atomic64_t *)&shared_buffer->tail);

  // If the slot we're about to write (current_idx) is the same as the tail,
  // and head has already wrapped around past tail, the buffer is full.
  if (unlikely(current_idx == tail_val &&
               (head_val - tail_val) >= shared_buffer->capacity)) {
    // Buffer is full. Increment dropped count atomically.
    // No need to roll back head with fetch_add.
    atomic64_inc((atomic64_t *)&shared_buffer->dropped_count);
    return -ENOMEM;
  }

  // 3. Get pointer to the entry in the buffer
  //    This calculation relies on shared_buffer pointing to the start of the
  //    mmappable region, and the buffer array being correctly placed.
  //    Careful if shared_buffer only maps the first page! Access needs
  //    validation if buffer spans pages and shared_buffer is only a partial
  //    kernel mapping. Assuming for now the kernel has full access via
  //    shared_buffer (e.g., vmap). If using page_address(buffer_pages[0]),
  //    direct access beyond PAGE_SIZE is invalid.
  //    *** This needs careful handling depending on allocation strategy ***
  //    Let's assume shared_buffer IS the correct kernel virtual address for the
  //    whole region.
  entry = &shared_buffer->buffer[current_idx];

  // 4. Fill in the data (flags field handled atomically later)
  //    Direct writes to plain struct members.
  entry->timestamp = __rdtscp(&entry->cpu_id);
  entry->event_id = event_id;

  entry->data1 = data1;
  entry->data2 = data2;

  // 5. Write Memory Barrier: Ensure all prior writes to the entry data payload
  //    are globally visible before the atomic update to the 'flags' field.
  smp_wmb();

  // 6. Atomically set the flags including the VALID bit using cmpxchg for
  // uint16_t.
  //    This provides release semantics implicitly on success on most
  //    architectures, making the written data visible to the consumer.
  do {
    old_flags = READ_ONCE(entry->flags);
    new_flags =
        (old_flags & ~LOG_FLAG_VALID) | LOG_FLAG_VALID | LOG_FLAG_KERNEL;
  } while (cmpxchg(&entry->flags, old_flags, new_flags) != old_flags);
  // --- Entry is now visible to consumer ---

  return 0;
}

// Export the function for use in the kernel.
EXPORT_SYMBOL(hires_log);

__always_inline u64 hires_rdtsc(void) { return __rdtsc(); }
EXPORT_SYMBOL(hires_rdtsc);

__always_inline u64 hires_rdtscp(u32 *auxp) { return __rdtscp(auxp); }
EXPORT_SYMBOL(hires_rdtscp);

// --- Module Initialization and Exit ---
static int __init hireslogger_km_init(void) {
  int ret = 0;
  size_t i;
  unsigned long calculated_ring_buffer_entries;
  unsigned long calculated_buffer_ctrl_size = SHARED_RING_BUFFER_CTRL_SIZE;
  unsigned long calculated_buffer_total_size_unaligned;

  pr_info("kHiResLogger: Initializing module...\n");

  u64 tsc_cycle = hires_calibrate_tsc();
  if (tsc_cycle == 0) {
    pr_err("kHiResLogger: TSC calibration failed.\n");
    return -EIO;
  }
  pr_info("kHiResLogger: TSC cycles per us: %llu\n", tsc_cycle);

  calculated_ring_buffer_entries = (1UL << rb_size_log2);
  calculated_buffer_total_size_unaligned =
      calculated_buffer_ctrl_size +
      (calculated_ring_buffer_entries * sizeof(log_entry_t));

  buffer_total_size = PAGE_ALIGN(calculated_buffer_total_size_unaligned);
  buffer_num_pages = buffer_total_size / PAGE_SIZE;

  pr_info("kHiResLogger: Requested log2_size=%d, Ring buffer entries=%lu, "
          "Ctrl size=%lu, Total size unaligned=%lu, Total size aligned=%lu "
          "(%lu pages)\n",
          rb_size_log2, calculated_ring_buffer_entries,
          calculated_buffer_ctrl_size, calculated_buffer_total_size_unaligned,
          buffer_total_size, buffer_num_pages);

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

  // We need a contiguous kernel virtual mapping of potentially non-contiguous
  shared_buffer = vmap(buffer_pages, buffer_num_pages, VM_MAP, PAGE_KERNEL);
  if (!shared_buffer) {
    pr_err("kHiResLogger: Failed to vmap page array\n");
    ret = -ENOMEM;
    goto fail_alloc_pages;
  }

  pr_info("kHiResLogger: Initializing shared buffer header at %px\n",
          shared_buffer);
  shared_buffer->capacity = calculated_ring_buffer_entries;
  shared_buffer->idx_mask = calculated_ring_buffer_entries - 1;
  shared_buffer->shm_size_bytes_unaligned =
      calculated_buffer_total_size_unaligned;
  shared_buffer->shm_size_bytes_aligned = buffer_total_size;

  atomic64_set((atomic64_t *)&shared_buffer->head, 0);
  atomic64_set((atomic64_t *)&shared_buffer->tail, 0);
  atomic64_set((atomic64_t *)&shared_buffer->dropped_count, 0);

  ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
  if (ret < 0) {
    pr_err("kHiResLogger: Failed to allocate major number: %d\n", ret);
    goto fail_vmap;
  }

  hireslogger_class = class_create(CLASS_NAME);
  if (IS_ERR(hireslogger_class)) {
    pr_err("kHiResLogger: Failed to create device class: %ld\n",
           PTR_ERR(hireslogger_class));
    ret = PTR_ERR(hireslogger_class);
    goto fail_chrdev_region;
  }

  cdev_init(&hires_cdev, &fops);
  hires_cdev.owner = THIS_MODULE;
  ret = cdev_add(&hires_cdev, dev_num, 1);
  if (ret < 0) {
    pr_err("kHiResLogger: Failed to add cdev: %d\n", ret);
    goto fail_class_create;
  }

  device_create(hireslogger_class, NULL, dev_num, NULL, DEVICE_NAME);
  pr_info("kHiResLogger: Device node /dev/%s created.\n", DEVICE_NAME);
  pr_info("kHiResLogger: Module loaded successfully.\n");
  return 0;

fail_class_create:
  class_destroy(hireslogger_class);
fail_chrdev_region:
  unregister_chrdev_region(dev_num, 1);
fail_vmap:
  if (shared_buffer) {
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
  if (!shared_buffer) {
    shared_buffer = NULL;
  }
  pr_err("kHiResLogger: Module initialization failed with error %d.\n", ret);
  return ret;
}

static void __exit hireslogger_km_exit(void) {
  size_t i;
  pr_info("kHiResLogger: Exiting module...\n");

  device_destroy(hireslogger_class, dev_num);
  cdev_del(&hires_cdev);
  class_destroy(hireslogger_class);
  unregister_chrdev_region(dev_num, 1);

  if (shared_buffer) {
    vunmap(shared_buffer);
    shared_buffer = NULL;
  }
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
MODULE_AUTHOR("Yibo Yan");
MODULE_DESCRIPTION("HiResLogger Kernel Module with MPSC Ring Buffer via mmap");
MODULE_VERSION("0.1");