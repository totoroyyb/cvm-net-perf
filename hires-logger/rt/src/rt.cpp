#include "../include/profiler_rt.hpp"
#include "../../shared_include/shared_profiler_data.h" // Need full definition here

#include <fcntl.h>       // For open()
#include <unistd.h>      // For close()
#include <sys/mman.h>    // For mmap(), munmap()
#include <sys/syscall.h> // For syscall(SYS_getcpu, ...)
#include <time.h>        // For clock_gettime()
#include <stdexcept>     // For runtime_error
#include <atomic>        // For std::atomic
#include <cstring>       // For strerror
#include <cerrno>        // For errno
#include <thread>        // For std::this_thread::yield

// Helper to map C11 _Atomic types in the struct to C++ std::atomic references
// This relies on them having compatible layout and size, which is generally true
// for standard integer types on common platforms. Use with caution.
template<typename T>
std::atomic<T>& as_std_atomic(volatile _Atomic(T)& c11_atomic) {
    return *reinterpret_cast<volatile std::atomic<T>*>(&c11_atomic);
}
// Const version
template<typename T>
const std::atomic<T>& as_std_atomic(const volatile _Atomic(T)& c11_atomic) {
    return *reinterpret_cast<const volatile std::atomic<T>*>(&c11_atomic);
}


namespace Profiler {

// --- ProfilerConnection Implementation ---

uint64_t ProfilerConnection::get_monotonic_ns() {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
        // Should not happen in practice on Linux unless system is broken
        return 0;
    }
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

ProfilerConnection::ProfilerConnection(const std::string& device_path) {
    // 1. Calculate required size (should match kernel module's calculation)
    // For simplicity, assume fixed size defined in header for now.
    // A robust way would be to read size via ioctl or from metadata in buffer.
    shm_size_ = SHARED_RING_BUFFER_TOTAL_SIZE;
    if (shm_size_ == 0 || shm_size_ < sizeof(shared_ring_buffer_t) - sizeof(log_entry_t)*RING_BUFFER_SIZE) {
         throw ProfilerError("Invalid shared buffer size calculation");
    }

    // 2. Open the device
    fd_ = open(device_path.c_str(), O_RDWR | O_CLOEXEC); // Use O_CLOEXEC
    if (fd_ == -1) {
        throw ProfilerError("Failed to open device '" + device_path + "': " + strerror(errno));
    }

    // 3. Map the device memory
    void* mapped_ptr = mmap(
        NULL,                   // Let kernel choose address
        shm_size_,              // Map the calculated size
        PROT_READ | PROT_WRITE, // Read/write access
        MAP_SHARED,             // Share changes with other processes (and kernel)
        fd_,                    // File descriptor of the device
        0                       // Offset within the device memory (must be 0 for char device mmap)
    );

    if (mapped_ptr == MAP_FAILED) {
        close(fd_); // Clean up fd before throwing
        fd_ = -1;
        throw ProfilerError("Failed to mmap device '" + device_path + "': " + strerror(errno));
    }

    shm_buf_ = static_cast<shared_ring_buffer_t*>(mapped_ptr);

    // 4. Optional: Sanity check mapped buffer (e.g., check magic number or version if added)
    // if (shm_buf_->magic_number != EXPECTED_MAGIC) { ... }
    if (shm_buf_->buffer_size != (1UL << RING_BUFFER_LOG2_SIZE)) {
         // Warning or error if size doesn't match compile time expectation
         // Could dynamically adapt if size is read from metadata.
         fprintf(stderr, "Warning: Mapped buffer size (%lu) differs from expected (%lu)\n",
                 (unsigned long)shm_buf_->buffer_size, (unsigned long)RING_BUFFER_SIZE);
         // For now, we'll proceed assuming the compile-time size is correct for masking etc.
         // A truly robust system would use shm_buf_->buffer_size and shm_buf_->size_mask.
    }
}

ProfilerConnection::~ProfilerConnection() {
    if (shm_buf_ != nullptr) {
        if (munmap(shm_buf_, shm_size_) == -1) {
            // Log error, but can't do much else in destructor
            fprintf(stderr, "ProfilerRT: munmap failed: %s\n", strerror(errno));
        }
        shm_buf_ = nullptr;
    }
    if (fd_ != -1) {
        if (close(fd_) == -1) {
             fprintf(stderr, "ProfilerRT: close failed: %s\n", strerror(errno));
        }
        fd_ = -1;
    }
}

bool ProfilerConnection::log(uint32_t event_id, uint64_t data1, uint64_t data2) {
    if (shm_buf_ == nullptr) {
        return false; // Not initialized
    }

    // Use C++ atomics via the helper function for clarity
    auto& atomic_head = as_std_atomic(shm_buf_->head);
    auto& atomic_tail = as_std_atomic(shm_buf_->tail); // Consumer tail
    auto& atomic_dropped = as_std_atomic(shm_buf_->dropped_count);

    // 1. Atomically reserve a slot (Acquire needed to sync with consumer tail write)
    size_t head = atomic_head.fetch_add(1, std::memory_order_acquire);
    size_t current_idx = head & RING_BUFFER_MASK; // Use compile-time mask

    // 2. Check if buffer is full (Acquire needed for tail read)
    // Compare how far head is ahead of tail.
    size_t tail = atomic_tail.load(std::memory_order_acquire);
    if ((head - tail) >= RING_BUFFER_SIZE) {
        // Buffer is full
        atomic_dropped.fetch_add(1, std::memory_order_relaxed); // Relaxed is fine for counter
        // Optional: Roll back head if desired, but complicates logic slightly
        // atomic_head.fetch_sub(1, std::memory_order_relaxed);
        return false; // Indicate drop
    }

    // 3. Get pointer to the entry
    log_entry_t* entry = &shm_buf_->buffer[current_idx];

    // 4. Fill data (flags first, without VALID)
    uint16_t flags = 0; // Userspace origin
    // Direct write to flags is okay here, final atomic store makes it visible
    entry->flags = flags;

    entry->timestamp = get_monotonic_ns();
    entry->event_id = event_id;
    // Get CPU ID using syscall (more portable than sched_getcpu glibc wrapper)
    unsigned cpu = 0, node = 0; // Cache cpu/node info if needed for performance
    #ifdef SYS_getcpu
    if (syscall(SYS_getcpu, &cpu, &node, NULL) == -1) {
        cpu = 0xFFFF; // Indicate error
    }
    #else
    cpu = sched_getcpu(); // Fallback to glibc wrapper if syscall number unknown
    #endif
    entry->cpu_id = static_cast<uint16_t>(cpu);
    entry->data1 = data1;
    entry->data2 = data2;

    // 5. Release Memory Barrier: Ensure all prior writes to the entry
    // are visible before the flags update becomes visible.
    std::atomic_thread_fence(std::memory_order_release);

    // 6. Atomically set the VALID flag (Release semantics)
    // This makes the entry visible to the consumer.
    // Use atomic store on the flags field directly.
    auto& atomic_flags = as_std_atomic(entry->flags); // Treat flags field as atomic
    atomic_flags.store(flags | LOG_FLAG_VALID, std::memory_order_release);

    return true; // Success
}


} // namespace Profiler