#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/sched.h> // For smp_processor_id()
#include <linux/slab.h> // For kzalloc/kfree (if not using pages directly)
#include <linux/vmalloc.h> // For vmalloc/vfree if using that
#include <linux/mm.h>      // For virt_to_page, page_to_pfn
#include <linux/ktime.h>   // For ktime_get_ns()
#include <linux/uaccess.h> // For copy_to_user etc (if using ioctl)
#include <linux/atomic.h>  // Kernel atomics
#include <linux/smp.h>     // For memory barriers smp_wmb/rmb

// Include the shared header
#include "../shared/common.h"

#define DEVICE_NAME "profiler_buf"
#define CLASS_NAME  "profiler"

// --- Module Parameters ---
static int log2_buffer_size = RING_BUFFER_LOG2_SIZE; // Default size
module_param(log2_buffer_size, int, S_IRUGO); // Allow overriding size at load time
MODULE_PARM_DESC(log2_buffer_size, "Log2 of the ring buffer size in entries");

// --- Global Variables ---
static dev_t dev_num;
static struct cdev profiler_cdev;
static struct class* profiler_class = NULL;
static shared_ring_buffer_t* shared_buffer = NULL; // Pointer to kernel-allocated buffer
static unsigned long buffer_total_size = 0; // Actual allocated size
static unsigned long buffer_num_pages = 0;
static struct page **buffer_pages = NULL; // For page-based allocation

// --- Forward Declarations ---
static int profiler_dev_open(struct inode *, struct file *);
static int profiler_dev_release(struct inode *, struct file *);
static int profiler_dev_mmap(struct file *filp, struct vm_area_struct *vma);

// --- File Operations ---
static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = profiler_dev_open,
    .release = profiler_dev_release,
    .mmap = profiler_dev_mmap,
    // .unlocked_ioctl = profiler_dev_ioctl, // Add if ioctl is needed
};

// --- Mmap Helper Functions (using alloc_pages) ---

// Called when a userspace process accesses a mapped page for the first time
static vm_fault_t profiler_vma_fault(struct vm_fault *vmf)
{
    struct page *page;
    unsigned long offset = vmf->pgoff; // Page offset into the mapping

    if (!shared_buffer || offset >= buffer_num_pages) {
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
static const struct vm_operations_struct profiler_vm_ops = {
    .fault = profiler_vma_fault,
};


// --- Device Operations Implementation ---
static int profiler_dev_open(struct inode *inodep, struct file *filep) {
    // Could track number of openers if needed
    pr_info("ProfilerKM: Device opened.\n");
    // try_module_get(THIS_MODULE); // Deprecated, ref counting is mostly automatic now
    return 0;
}

static int profiler_dev_release(struct inode *inodep, struct file *filep) {
    pr_info("ProfilerKM: Device closed.\n");
    // module_put(THIS_MODULE); // Deprecated
    return 0;
}

static int profiler_dev_mmap(struct file *filp, struct vm_area_struct *vma) {
    unsigned long requested_size = vma->vm_end - vma->vm_start;
    unsigned long physical_pfn;
    int ret;

    pr_info("ProfilerKM: mmap called. Requested size: %lu, Buffer size: %lu\n", requested_size, buffer_total_size);

    // Check if requested size matches our buffer size
    // Allow mapping smaller, but typically should match exactly
    if (requested_size > buffer_total_size) {
        pr_err("ProfilerKM: Requested mmap size %lu is larger than allocated buffer size %lu\n", requested_size, buffer_total_size);
        return -EINVAL;
    }

     // Prevent modifications to vma->vm_flags & co after this point
    vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP;
    // Set our custom vm_ops to handle page faults
    vma->vm_ops = &profiler_vm_ops;

    pr_info("ProfilerKM: mmap successful.\n");
    return 0;

    /* --- Alternative: Using remap_pfn_range (simpler if buffer is contiguous) ---
    // This requires the buffer to be allocated with methods guaranteeing
    // physical continuity (like alloc_pages with low order, or kmalloc
    // for small sizes). vmalloc is NOT physically contiguous.

    if (!shared_buffer) return -EIO;

    // Get the physical address (Page Frame Number) of the start of the buffer
    physical_pfn = page_to_pfn(virt_to_page(shared_buffer)); // Only valid if contiguous!

    // Remap the physical pages into the user's virtual address space
    ret = remap_pfn_range(vma,
                          vma->vm_start,
                          physical_pfn + vma->vm_pgoff, // Add page offset from mmap call
                          requested_size,
                          vma->vm_page_prot); // Use protection flags from VMA

    if (ret) {
        pr_err("ProfilerKM: remap_pfn_range failed: %d\n", ret);
        return ret;
    }

    pr_info("ProfilerKM: mmap successful using remap_pfn_range.\n");
    return 0;
    */
}


// --- Kernel Producer Logging API ---
// Exported function for other kernel parts to call (or call internally via tracepoints etc)
/**
 * profiler_km_log - Log an event from kernel context.
 * @event_id: Identifier for the event type.
 * @data1: Custom data payload 1.
 * @data2: Custom data payload 2.
 *
 * Returns 0 on success, -ENOMEM if buffer is full.
 */
int profiler_km_log(uint32_t event_id, uint64_t data1, uint64_t data2) {
    size_t head, tail, current_idx;
    log_entry_t *entry;
    uint16_t flags;

    if (unlikely(!shared_buffer)) {
        // Module not initialized or buffer allocation failed
        return -EIO;
    }

    // 1. Atomically reserve a slot (Acquire semantics needed to sync with consumer tail read)
    head = (size_t)atomic64_fetch_add_acquire(1, &shared_buffer->head);
    current_idx = head & shared_buffer->size_mask; // Faster than modulo

    // 2. Check if buffer is full (using Acquire semantics for tail read)
    // We compare how far head is ahead of tail. If it's >= size, buffer is full.
    tail = (size_t)atomic64_read_acquire(&shared_buffer->tail);
    if (unlikely((head - tail) >= shared_buffer->buffer_size)) {
        // Buffer is full - Roll back head (optional, but prevents head running too far ahead)
        // atomic64_dec(&shared_buffer->head); // Be careful with races if doing this
        atomic64_inc(&shared_buffer->dropped_count);
        return -ENOMEM; // Or just return 0 and indicate drop via count
    }

    // 3. Get pointer to the entry in the buffer
    entry = &shared_buffer->buffer[current_idx];

    // 4. Fill in the data (flags first, without VALID bit initially)
    // Ensure flags field is written before data payload for some MPSC algos,
    // but here we rely on the final atomic write of flags with VALID bit.
    flags = LOG_FLAG_KERNEL; // Mark as kernel origin
    entry->flags = flags; // Initial write (optional, VALID bit is key)

    entry->timestamp = ktime_get_ns(); // High-resolution monotonic timestamp
    entry->event_id = event_id;
    entry->cpu_id = (uint16_t)smp_processor_id();
    entry->data1 = data1;
    entry->data2 = data2;

    // 5. Write Memory Barrier: Ensure all prior writes to the entry struct
    // are completed before the final flags write becomes visible.
    smp_wmb();

    // 6. Atomically set the VALID flag (Release semantics)
    // This makes the entry visible to the consumer *after* all data is written.
    // Use atomic OR or atomic_set if available and appropriate.
    // Simple store after WMB is often sufficient if flags field is atomic type,
    // but atomic RMW ensures atomicity if flags were non-atomic.
    // Assuming flags itself isn't atomic, use atomic op on the whole field:
    // For simplicity, let's assume direct write after barrier is okay for flags
    // if consumer uses acquire read. A safer way is atomic ops if available.
    // atomic_or(LOG_FLAG_VALID, &entry->flags); // Example if flags were atomic_t
    // Let's stick to direct write after barrier:
    entry->flags = flags | LOG_FLAG_VALID;

    // Optional: Release barrier if not inherent in atomic op used for flags
    // smp_mb__after_atomic(); // If needed after atomic flag set

    return 0; // Success
}
EXPORT_SYMBOL(profiler_km_log); // Export for other kernel modules


// --- Module Initialization and Exit ---
static int __init profiler_km_init(void) {
    int ret = 0;
    size_t i;
    unsigned long calculated_buffer_size;
    unsigned long calculated_ring_buffer_size;

    pr_info("ProfilerKM: Initializing module...\n");

    // Calculate actual sizes based on potentially overridden log2_buffer_size
    calculated_ring_buffer_size = (1UL << log2_buffer_size);
    calculated_buffer_size = sizeof(shared_ring_buffer_t) - sizeof(log_entry_t) * RING_BUFFER_SIZE // Base struct size
                             + sizeof(log_entry_t) * calculated_ring_buffer_size; // Actual buffer array size
    buffer_total_size = PAGE_ALIGN(calculated_buffer_size); // Align total size to page boundary
    buffer_num_pages = buffer_total_size / PAGE_SIZE;

    pr_info("ProfilerKM: Requested log2_size=%d, Ring buffer entries=%lu, Struct size=%lu, Total size aligned=%lu (%lu pages)\n",
            log2_buffer_size, calculated_ring_buffer_size, calculated_buffer_size, buffer_total_size, buffer_num_pages);


    // 1. Allocate page array and the pages themselves
    buffer_pages = kcalloc(buffer_num_pages, sizeof(struct page *), GFP_KERNEL);
    if (!buffer_pages) {
        pr_err("ProfilerKM: Failed to allocate page pointer array\n");
        return -ENOMEM;
    }

    for (i = 0; i < buffer_num_pages; ++i) {
        // Allocate pages with GFP_KERNEL | __GFP_ZERO to get zeroed memory
        buffer_pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);
        if (!buffer_pages[i]) {
            pr_err("ProfilerKM: Failed to allocate page %zu\n", i);
            ret = -ENOMEM;
            goto fail_alloc_pages;
        }
    }
    // Get a kernel virtual address mapping for the first page (needed to access the struct header)
    // Note: This only maps the *first* page. Accessing beyond PAGE_SIZE via this pointer is invalid.
    // We primarily need this to initialize the header. Direct buffer access uses indices.
    shared_buffer = page_address(buffer_pages[0]);
    if (!shared_buffer) {
         pr_err("ProfilerKM: Failed to get virtual address for page 0\n");
         ret = -ENOMEM; // Or another appropriate error
         goto fail_alloc_pages;
    }


    // 2. Initialize the shared buffer structure header (in the first page)
    pr_info("ProfilerKM: Initializing shared buffer header at %px\n", shared_buffer);
    shared_buffer->buffer_size = calculated_ring_buffer_size;
    shared_buffer->size_mask = calculated_ring_buffer_size - 1;
    atomic64_set(&shared_buffer->head, 0);
    atomic64_set(&shared_buffer->tail, 0);
    atomic64_set(&shared_buffer->dropped_count, 0);
    // Buffer entries are already zeroed by __GFP_ZERO

    // 3. Allocate device number
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("ProfilerKM: Failed to allocate major number: %d\n", ret);
        goto fail_alloc_pages; // Use the page cleanup path
    }
    pr_info("ProfilerKM: Major number allocated: %d\n", MAJOR(dev_num));

    // 4. Create device class
    profiler_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(profiler_class)) {
        pr_err("ProfilerKM: Failed to create device class: %ld\n", PTR_ERR(profiler_class));
        ret = PTR_ERR(profiler_class);
        goto fail_chrdev_region;
    }
    pr_info("ProfilerKM: Device class created successfully.\n");

    // 5. Create character device
    cdev_init(&profiler_cdev, &fops);
    profiler_cdev.owner = THIS_MODULE;
    ret = cdev_add(&profiler_cdev, dev_num, 1);
    if (ret < 0) {
        pr_err("ProfilerKM: Failed to add cdev: %d\n", ret);
        goto fail_class_create;
    }
    pr_info("ProfilerKM: Character device added successfully.\n");

    // 6. Create device node (/dev/profiler_buf)
    device_create(profiler_class, NULL, dev_num, NULL, DEVICE_NAME);
    pr_info("ProfilerKM: Device node /dev/%s created.\n", DEVICE_NAME);

    pr_info("ProfilerKM: Module loaded successfully.\n");
    return 0; // Success

// --- Error Handling Cleanup ---
fail_class_create:
    class_destroy(profiler_class);
fail_chrdev_region:
    unregister_chrdev_region(dev_num, 1);
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
    shared_buffer = NULL; // Ensure pointer is null on failure
    pr_err("ProfilerKM: Module initialization failed with error %d.\n", ret);
    return ret;
}

static void __exit profiler_km_exit(void) {
    size_t i;
    pr_info("ProfilerKM: Exiting module...\n");

    // Destroy device node
    device_destroy(profiler_class, dev_num);
    // Destroy character device
    cdev_del(&profiler_cdev);
    // Destroy device class
    class_destroy(profiler_class);
    // Unregister device number
    unregister_chrdev_region(dev_num, 1);
    // Free allocated buffer memory
    if (buffer_pages) {
        for (i = 0; i < buffer_num_pages; ++i) {
            if (buffer_pages[i]) {
                __free_page(buffer_pages[i]);
            }
        }
        kfree(buffer_pages);
        buffer_pages = NULL;
    }
    shared_buffer = NULL; // Clear pointer

    pr_info("ProfilerKM: Module unloaded.\n");
}

module_init(profiler_km_init);
module_exit(profiler_km_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Profiler Kernel Module with MPSC Ring Buffer via mmap");
MODULE_VERSION("0.1");
