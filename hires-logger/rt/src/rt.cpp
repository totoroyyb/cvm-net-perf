#include <atomic> // For std::atomic_ref, std::memory_order, std::atomic_thread_fence
#include <cerrno> // For errno
#include <cstdint>
#include <cstring>       // For strerror
#include <fcntl.h>       // For open()
#include <iostream>
#include <optional>
#include <stdexcept>     // For runtime_error
#include <sys/mman.h>    // For mmap(), munmap()
#include <sys/syscall.h> // For syscall(SYS_getcpu, ...)
#include <system_error>  // Include for std::system_error (better exception)
#include <thread>        // For std::this_thread::yield
#include <time.h>        // For clock_gettime()
#include <unistd.h>      // For close()

#include "../../shared/common.h"
#include "../include/rt.hpp"

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

  // ioctl for reading the runtime rb size and mask.
  auto rb_meta = this->get_rb_meta();
  if (!rb_meta.has_value()) {
    throw HiResError(
        "Failed to get ring buffer metadata from device '" + device_path + "'");
  }
  std::cout << "Ring buffer size: " << rb_meta->buffer_size
            << ", size mask: " << rb_meta->size_mask << std::endl;
  this->set_runtime_rb_meta(*rb_meta);
  // use the runtime size for the mmap
  this->shm_size_ = this->rb_runtime_size_;

  // 3. Map the device memory
  void *mapped_ptr =
      mmap(NULL,                      // Let kernel choose address
           shm_size_,                 // Map the calculated size
           PROT_READ | PROT_WRITE,    // Read/write access
           MAP_SHARED | MAP_POPULATE, // Share changes + Hint to pre-fault pages
           fd_,                       // File descriptor of the device
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
  // TODO: use ioctl to get the size, as the header size won't match the runtime
  // size_t expected_entries = (1UL << RING_BUFFER_LOG2_SIZE);
  // if (shm_buf_->buffer_size != expected_entries ||
  //     shm_buf_->size_mask != (expected_entries - 1)) {
  //   // Handle mismatch - either throw, log warning, or adapt dynamically
  //   munmap(shm_buf_, shm_size_); // Clean up map
  //   close(fd_);                  // Clean up fd
  //   fd_ = -1;
  //   shm_buf_ = nullptr;
  //   throw HiResError(
  //       "Mapped buffer metadata mismatch. Expected size=" +
  //       std::to_string(expected_entries) +
  //       ", Mapped size=" + std::to_string(shm_buf_->buffer_size));
  //   // Note: Using shm_buf_->buffer_size read *before* unmap! Better to store
  //   // first.
  // }
  // Store the validated size/mask for later use if needed
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

inline __attribute__((always_inline)) std::optional<hires_rb_meta_t> HiResConn::get_rb_meta() const noexcept {
  long ioctl_ret = 0;
  hires_rb_meta_t meta;
  ioctl_ret = ioctl(this->get_fd(), HIRES_IOCTL_GET_RB_META, &meta);
  if (ioctl_ret < 0) {
    std::cerr << "ERROR: HIRES_IOCTL_GET_RB_META failed. Error " << errno << ": "
              << strerror(errno) << std::endl;
    return std::nullopt;
  } 
  return meta;
}

bool HiResConn::log(uint32_t event_id, uint64_t data1, uint64_t data2) {
  if (shm_buf_ == nullptr) {
    return false; // Not initialized
  }

  std::atomic_ref<uint64_t> atomic_head(shm_buf_->head);
  std::atomic_ref<uint64_t> atomic_tail(
      shm_buf_->tail); // For checking fullness
  std::atomic_ref<uint64_t> atomic_dropped(
      shm_buf_->dropped_count); // For incrementing drops

  // Atomically reserve a slot (acquire needed for fetch, release not strictly
  // needed but common)
  //    fetch_add returns the value BEFORE the addition.
  size_t head = atomic_head.fetch_add(1, std::memory_order_acq_rel);

  size_t tail = atomic_tail.load(std::memory_order_acquire);
  if ((head - tail) >= get_rb_size()) [[unlikely]] {
    atomic_dropped.fetch_add(1, std::memory_order_relaxed);
    // Note: Head was already incremented. No explicit rollback needed for this
    // scheme.
    return false;
  }

  size_t current_idx = head & get_rb_mask();
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
  uint16_t initial_flags =
      0; // Userspace origin, VALID bit will be added by store
  atomic_flags.store(initial_flags | LOG_FLAG_VALID, std::memory_order_release);

  return true; // Success
}

std::optional<log_entry_t> HiResConn::pop() {
  if (shm_buf_ == nullptr) {
    return std::nullopt; // Not initialized
  }

  std::atomic_ref<uint64_t> atomic_head(shm_buf_->head);
  std::atomic_ref<uint64_t> atomic_tail(shm_buf_->tail);

  // 1. Read current tail (Relaxed is okay, only consumer modifies tail)
  size_t tail = atomic_tail.load(std::memory_order_relaxed);

  // 2. Check if buffer is empty (use Acquire on head load)
  //    Ensures we see producer writes that happened *before* head was updated.
  size_t head = atomic_head.load(std::memory_order_acquire);
  if (tail == head) {
    return std::nullopt; // Buffer is empty
  }

  // 3. Calculate index and get entry pointer
  size_t current_idx = tail & get_rb_mask();
  log_entry_t *entry = &shm_buf_->buffer[current_idx];
  std::atomic_ref<uint16_t> atomic_flags(entry->flags);

  // 4. Wait for the VALID flag (use Acquire load)
  //    Ensures we see the data writes that happened *before* the flag was set.
  //    Implement a short spin-wait with yield.
  constexpr int max_spins = 100; // Limit spinning
  int spin_count = 0;
  while ((atomic_flags.load(std::memory_order_acquire) & LOG_FLAG_VALID) == 0) {
    if (++spin_count > max_spins) {
      // Entry wasn't ready quickly enough, maybe producer is slow or stuck.
      // Return nullopt to allow caller to decide how to handle (e.g., retry
      // later).
      return std::nullopt;
    }
    std::this_thread::yield();
  }

  // 5. Read data (Entry is valid and ready)
  //    Perform a simple copy. Volatile isn't strictly needed due to
  //    atomics/fences.
  log_entry_t result_entry = *entry; // Direct struct copy

  // 6. Optional: Clear the VALID flag (Relaxed store is sufficient)
  //    This helps debugging and potentially some producer logic variants.
  //    Read current flags first to preserve other bits (like KERNEL flag).
  uint16_t current_flags = atomic_flags.load(std::memory_order_relaxed);
  atomic_flags.store(current_flags & ~LOG_FLAG_VALID,
                     std::memory_order_relaxed);

  // 7. Advance tail (Release semantics)
  //    Make the slot available for producers *after* we've finished reading.
  //    Store the *next* tail value.
  atomic_tail.store(tail + 1, std::memory_order_release);

  // 8. Return the copied data
  return result_entry;
}
} // namespace HiResLogger
