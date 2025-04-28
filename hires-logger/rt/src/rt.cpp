#include <atomic>        // For std::atomic_ref, std::memory_order, std::atomic_thread_fence
#include <cerrno>        // For errno
#include <cstring>       // For strerror
#include <fcntl.h>       // For open()
#include <stdexcept>     // For runtime_error
#include <sys/mman.h>    // For mmap(), munmap()
#include <sys/syscall.h> // For syscall(SYS_getcpu, ...)
#include <system_error>  // Include for std::system_error (better exception)
#include <thread>        // For std::this_thread::yield
#include <time.h>        // For clock_gettime()
#include <unistd.h>      // For close()

#include "../include/rt.hpp"
#include "../../shared/common.h"

#if __cplusplus < 202002L
#error "This code requires C++20 or later for std::atomic_ref"
#endif

namespace HiResLogger {

[[noreturn]] void throw_system_error(const std::string &context) {
  throw std::system_error(errno, std::system_category(), context);
}

uint64_t HiResConn::get_monotonic_ns() {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
    // Use system_error for better exception handling
    throw_system_error("clock_gettime(CLOCK_MONOTONIC) failed");
  }
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
         static_cast<uint64_t>(ts.tv_nsec);
}

HiResConn::HiResConn(const std::string &device_path) {
  // 1. Use the size calculation macro from the shared header
  //    This ensures consistency with the kernel module's allocation.
  shm_size_ = SHARED_RING_BUFFER_TOTAL_SIZE;
  if (shm_size_ < sizeof(shared_ring_buffer_t)) { // Basic sanity check
    throw HiResError("Invalid shared buffer size macro definition");
  }

  // 2. Open the device
  fd_ = open(device_path.c_str(), O_RDWR | O_CLOEXEC); 
  if (fd_ == -1) {
    throw_system_error("Failed to open device '" + device_path + "'");
  }

  // 3. Map the device memory
  void *mapped_ptr =
      mmap(NULL,                     // Let kernel choose address
           shm_size_,                // Map the calculated size
           PROT_READ | PROT_WRITE,   // Read/write access
           MAP_SHARED | MAP_POPULATE, // Share changes + Hint to pre-fault pages
           fd_,                      // File descriptor of the device
           0 // Offset within the device memory (must be 0 for char device mmap)
      );

  if (mapped_ptr == MAP_FAILED) {
    int saved_errno = errno; // Save errno before close() might change it
    close(fd_);
    fd_ = -1;
    errno = saved_errno; // Restore errno for throw_system_error
    throw_system_error("Failed to mmap device '" + device_path + "'");
  }

  shm_buf_ = static_cast<shared_ring_buffer_t *>(mapped_ptr);

  // 4. Optional but Recommended: Sanity check mapped buffer metadata
  //    Read buffer_size and size_mask ONCE after mapping.
  //    These are written by the kernel during init and don't change,
  //    so no atomicity needed here.
  size_t expected_entries = (1UL << RING_BUFFER_LOG2_SIZE); // From header
  if (shm_buf_->buffer_size != expected_entries ||
      shm_buf_->size_mask != (expected_entries - 1)) {
    // Handle mismatch - either throw, log warning, or adapt dynamically
    munmap(shm_buf_, shm_size_); // Clean up map
    close(fd_);                  // Clean up fd
    fd_ = -1;
    shm_buf_ = nullptr;
    throw HiResError(
        "Mapped buffer metadata mismatch. Expected size=" +
        std::to_string(expected_entries) +
        ", Mapped size=" + std::to_string(shm_buf_->buffer_size));
    // Note: Using shm_buf_->buffer_size read *before* unmap! Better to store first.
  }
  // Store the validated size/mask for later use if needed
  rb_runtime_size_ = shm_buf_->buffer_size;
  rb_runtime_mask_ = shm_buf_->size_mask;
}

HiResConn::~HiResConn() {
  if (shm_buf_ != nullptr) {
    if (munmap(shm_buf_, shm_size_) == -1) {
      fprintf(stderr, "HiResLoggerRT: munmap failed: %s\n", strerror(errno));
    }
    shm_buf_ = nullptr;
  }
  if (fd_ != -1) {
    if (close(fd_) == -1) {
      fprintf(stderr, "HiResLoggerRT: close failed: %s\n", strerror(errno));
    }
    fd_ = -1;
  }
}

bool HiResConn::log(uint32_t event_id, uint64_t data1,
                                 uint64_t data2) {
  if (shm_buf_ == nullptr) {
    return false; // Not initialized
  }

  std::atomic_ref<prof_size_t> atomic_head(shm_buf_->head);
  std::atomic_ref<prof_size_t> atomic_tail(shm_buf_->tail); // For checking fullness
  std::atomic_ref<uint64_t> atomic_dropped(shm_buf_->dropped_count); // For incrementing drops

  // Atomically reserve a slot (acquire needed for fetch, release not strictly needed but common)
  //    fetch_add returns the value BEFORE the addition.
  size_t head = atomic_head.fetch_add(1, std::memory_order_acq_rel);
  size_t current_idx = head & rb_runtime_mask_; // Use validated mask

  size_t tail = atomic_tail.load(std::memory_order_acquire);
  if ((head - tail) >= rb_runtime_size_) [[unlikely]] {
    atomic_dropped.fetch_add(1, std::memory_order_relaxed);
    // Note: Head was already incremented. No explicit rollback needed for this scheme.
    return false;
  }

  log_entry_t *entry = &shm_buf_->buffer[current_idx];

  // Fill data (flags are handled atomically below)
  //    Direct writes to plain members are fine before the release operation.
  entry->timestamp = get_monotonic_ns();
  entry->event_id = event_id;

  // Get CPU ID using syscall (more portable than sched_getcpu glibc wrapper)
  unsigned cpu = 0, node = 0; // Cache cpu/node info if needed for performance
#ifdef SYS_getcpu
  if (syscall(SYS_getcpu, &cpu, &node, NULL) == -1) {
    cpu = 0xFFFF; // error
  }
#else
  cpu = sched_getcpu();
  if (cpu < 0) {
    cpu = 0xFFFF;
  }
#endif
  entry->cpu_id = static_cast<uint16_t>(cpu);
  entry->data1 = data1;
  entry->data2 = data2;

  // Release Operations: Ensure prior writes are visible before VALID flag
  //    Option A: Use atomic_thread_fence (explicit fence)
  // std::atomic_thread_fence(std::memory_order_release);
  //    Option B: Rely on the release semantics of the atomic store below

  // Atomically set the flags including the VALID bit (Release semantics)
  //    This makes the entry visible to the consumer.
  std::atomic_ref<uint16_t> atomic_flags(entry->flags);
  uint16_t initial_flags = 0; // Userspace origin, VALID bit will be added by store
  atomic_flags.store(initial_flags | LOG_FLAG_VALID, std::memory_order_release);

  return true; // Success
}

} // namespace Profiler