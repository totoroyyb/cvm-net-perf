#ifndef SHARED_COMMON_H
#define SHARED_COMMON_H


// Includes for basic types and alignment based on environment
#ifdef __KERNEL__
    #include <linux/types.h> // u8, u16, u32, u64
    #include <linux/align.h> // __aligned
    #include <linux/stddef.h> // offsetof (potentially)
    #include <linux/ioctl.h>

    #define PROF_CACHE_LINE_SIZE 64 // Define for kernel
    #define PROF_CACHE_LINE_ALIGNED __aligned(PROF_CACHE_LINE_SIZE)
    typedef u64 prof_size_t; // Use fixed size for size_t representation
#else
    #include <stdint.h> // uintN_t types
    #include <stddef.h> // size_t, offsetof
    #include <sys/ioctl.h>
    #define PROF_CACHE_LINE_SIZE 64 // Define for userspace
    #ifdef __cplusplus
        // C++ includes
        #include <atomic> // Needed in .cpp files, not necessarily here
        #include <cstddef>
        #include <cstdint>
        #define PROF_CACHE_LINE_ALIGNED alignas(PROF_CACHE_LINE_SIZE)
    #else
        // C11 includes
        #include <stdatomic.h> // Needed in .c files, not necessarily here
        #include <stdalign.h>  // alignas
        #define PROF_CACHE_LINE_ALIGNED alignas(PROF_CACHE_LINE_SIZE)
    #endif // __cplusplus
    typedef uint64_t prof_size_t; // Use standard size_t for userspace
#endif // __KERNEL__

// --- IOCTL Definitions ---
typedef struct {
    prof_size_t capacity;                   // Actual number of entries available in the RB
    prof_size_t idx_mask;                   // Mask (usually capacity - 1)
    prof_size_t shm_size_bytes_unaligned;   // Size of the shared memory region in bytes (unaligned)
} hires_rb_meta_t;

#define HIRES_IOCTL_MAGIC 'h'
#define HIRES_IOCTL_RESET_RB                _IO(HIRES_IOCTL_MAGIC, 1)
#define HIRES_IOCTL_GET_RB_META             _IOR(HIRES_IOCTL_MAGIC, 2, hires_rb_meta_t)
#define HIRES_IOCTL_GET_TSC_CYCLE_PER_US    _IOR(HIRES_IOCTL_MAGIC, 3, prof_size_t)
// --- End IOCTL Definitions ---

typedef struct {
    uint64_t timestamp;
    uint32_t event_id;
    uint32_t cpu_id;
    uint16_t flags;
    uint64_t data1;
    uint64_t data2;
} log_entry_t;

// Flag definitions
#define LOG_FLAG_VALID (1 << 0)
#define LOG_FLAG_KERNEL (1 << 1)

// Ring buffer constants
#define RING_BUFFER_LOG2_SIZE 16
#define RING_BUFFER_SIZE (1UL << RING_BUFFER_LOG2_SIZE)
#define RING_BUFFER_MASK (RING_BUFFER_SIZE - 1)

// Shared structure using PLAIN types for atomic fields
typedef struct {
    // Producer Control
    PROF_CACHE_LINE_ALIGNED prof_size_t head; // Plain type
    char pad0[PROF_CACHE_LINE_SIZE > sizeof(prof_size_t) ? PROF_CACHE_LINE_SIZE - sizeof(prof_size_t) : 1];

    // Consumer Control
    PROF_CACHE_LINE_ALIGNED prof_size_t tail; // Plain type
    char pad1[PROF_CACHE_LINE_SIZE > sizeof(prof_size_t) ? PROF_CACHE_LINE_SIZE - sizeof(prof_size_t) : 1];

    // Metadata & Stats
    uint64_t shm_size_bytes_unaligned;
    uint64_t shm_size_bytes_aligned;
    uint64_t capacity;
    uint64_t idx_mask;
    uint64_t dropped_count;
    char pad2[PROF_CACHE_LINE_SIZE > (sizeof(uint64_t) * 3) ? PROF_CACHE_LINE_SIZE - (sizeof(uint64_t) * 3) : 1]; // Adjusted padding size calculation

    // The Actual Buffer
    PROF_CACHE_LINE_ALIGNED log_entry_t buffer[RING_BUFFER_SIZE];

} shared_ring_buffer_t;

// Recalculate based on the actual buffer size needed
// The size is now determined by the header size plus the buffer array size.
#define SHARED_RING_BUFFER_CTRL_SIZE (offsetof(shared_ring_buffer_t, buffer))
#define SHARED_RING_BUFFER_TOTAL_SIZE (SHARED_RING_BUFFER_CTRL_SIZE + (RING_BUFFER_SIZE * sizeof(log_entry_t)))

#endif // SHARED_COMMON_H