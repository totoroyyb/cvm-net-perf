#ifndef SHARED_COMMON_H
#define SHARED_COMMON_H

// Use standard types available in both kernel and userspace
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/align.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h> // C11 atomics for userspace C/C++
#include <stdalign.h>  // C11 alignment
#endif

// Define fixed-size log entry structure
typedef struct {
    uint64_t timestamp;     // Nanoseconds (monotonic)
    uint32_t event_id;      // Unique identifier for the event type
    uint16_t cpu_id;        // CPU core where the event occurred
    uint16_t flags;         // Status flags (see below)
    uint64_t data1;         // Custom payload data field 1
    uint64_t data2;         // Custom payload data field 2
} log_entry_t;

// Flag definitions for log_entry_t.flags
#define LOG_FLAG_VALID (1 << 0) // Set by producer when entry fully written
#define LOG_FLAG_KERNEL (1 << 1) // Set if logged from kernel

// --- MPSC Ring Buffer Control Block ---

// Define buffer size (must be power of 2 for efficient masking)
// Choose a size appropriate for expected throughput and consumer latency
#define RING_BUFFER_LOG2_SIZE 16 // 2^16 = 65536 entries
#define RING_BUFFER_SIZE (1UL << RING_BUFFER_LOG2_SIZE)
#define RING_BUFFER_MASK (RING_BUFFER_SIZE - 1)

// Define cache line size (common value, adjust if needed for specific arch)
#define PROFILER_CACHE_LINE_SIZE 64

// Define atomic types and alignment macros consistently
#ifdef __KERNEL__
    // Kernel definitions
    #define PROF_ATOMIC_SIZE_T atomic64_t // Assuming 64-bit size_t/long
    #define PROF_ATOMIC_U64 atomic64_t
    #define PROF_CACHE_LINE_ALIGNED __aligned(PROFILER_CACHE_LINE_SIZE)
    // Helper macro for kernel atomic init
    #define PROF_ATOMIC_INIT(i) ATOMIC64_INIT(i)
#else
    // Userspace C11/C++11 definitions
    #define PROF_ATOMIC_SIZE_T _Atomic size_t
    #define PROF_ATOMIC_U64 _Atomic uint64_t
    #define PROF_CACHE_LINE_ALIGNED alignas(PROFILER_CACHE_LINE_SIZE)
    // Helper macro for userspace atomic init (can be done at runtime)
    #define PROF_ATOMIC_INIT(i) (i) // Runtime init needed for userspace
#endif


typedef struct {
    // --- Producer Control ---
    // Head: Index for the *next* slot producers will attempt to claim.
    // Incremented atomically by producers. Wraps around.
    PROF_CACHE_LINE_ALIGNED PROF_ATOMIC_SIZE_T head;
    char pad0[PROFILER_CACHE_LINE_SIZE - sizeof(PROF_ATOMIC_SIZE_T)];

    // --- Consumer Control ---
    // Tail: Index of the *next* slot the consumer will read.
    // Only modified by the single consumer. Wraps around.
    PROF_CACHE_LINE_ALIGNED PROF_ATOMIC_SIZE_T tail;
    char pad1[PROFILER_CACHE_LINE_SIZE - sizeof(PROF_ATOMIC_SIZE_T)];

    // --- Metadata & Stats ---
    uint64_t buffer_size;     // Capacity (RING_BUFFER_SIZE)
    uint64_t size_mask;       // Mask (RING_BUFFER_MASK)
    // Count of entries dropped by producers because buffer was full.
    PROF_ATOMIC_U64 dropped_count;
    // Add other potential metadata: version, magic number, start_time_ns etc.
    // Ensure this metadata section is also padded if needed.
    char pad2[PROFILER_CACHE_LINE_SIZE - (sizeof(uint64_t) * 2 + sizeof(PROF_ATOMIC_U64))];


    // --- The Actual Buffer ---
    // Ensure buffer starts on a cache line boundary relative to start of struct
    PROF_CACHE_LINE_ALIGNED log_entry_t buffer[RING_BUFFER_SIZE];

} shared_ring_buffer_t;

// Calculate the total size needed for allocation/mmap
// Note: This simple sizeof might not account for potential padding *after*
// the flexible array member if the compiler adds any, but usually okay.
// For mmap, rounding up to page size is often needed anyway.
#define SHARED_RING_BUFFER_TOTAL_SIZE sizeof(shared_ring_buffer_t)


#endif // SHARED_PROFILER_DATA_H