#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include "../../shared/common.h"

namespace HiResLogger {

class HiResError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class HiResConn {
private:
  int fd_ = -1;
  shared_ring_buffer_t *shm_buf_ = nullptr;
  uint64_t rb_runtime_capacity_ = 0; // RB capacity
  uint64_t rb_runtime_idx_mask_ = 0;
  uint64_t rb_runtime_shm_size_ = 0; // Size of the mapped region

  // Helper to get monotonic time
  static uint64_t get_monotonic_ns();

  inline __attribute__((always_inline)) void
  set_runtime_rb_meta(const hires_rb_meta_t &meta) noexcept {
    this->rb_runtime_capacity_ = static_cast<uint64_t>(meta.capacity);
    this->rb_runtime_idx_mask_ = static_cast<uint64_t>(meta.idx_mask);
    this->rb_runtime_shm_size_ =
        static_cast<uint64_t>(meta.shm_size_bytes_unaligned);
  }

public:
  /**
   * @brief Constructs a connection, opening and mmapping the device.
   * @param device_path Path to the HiResLogger character device.
   * @throws HiResError if opening or mmapping fails.
   */
  explicit HiResConn(const std::string &device_path = "/dev/khires");

  /**
   * @brief Destructor, automatically unmaps and closes the device.
   */
  ~HiResConn();

  // --- Rule of Five: Disable copy/move for simplicity ---
  // Prevent accidental copying or moving which would mess up resource
  // management. Could be implemented properly if needed, but deletion is safer
  // for now.
  HiResConn(const HiResConn &) = delete;
  HiResConn &operator=(const HiResConn &) = delete;
  HiResConn(HiResConn &&) = delete;
  HiResConn &operator=(HiResConn &&) = delete;

  std::optional<hires_rb_meta_t> get_rb_meta() const noexcept;

  /**
   * @brief Logs an event to the shared ring buffer (Userspace Producer Logic).
   * @param event_id Identifier for the event type.
   * @param data1 Custom data payload 1.
   * @param data2 Custom data payload 2.
   * @return True on success, false if the buffer was full and the entry was
   * dropped.
   */
  bool log(uint32_t event_id, uint64_t data1 = 0, uint64_t data2 = 0);

  /**
   * @brief Attempts to pop one log entry from the buffer (Consumer Logic).
   * This implements the single-consumer side of the MPSC queue.
   * It waits briefly for the entry's VALID flag if necessary.
   * @return An std::optional containing the log_entry_t if successful,
   * std::nullopt if the buffer is empty or the entry wasn't ready
   * within a short wait.
   */
  std::optional<log_entry_t> pop();

  /**
   * @brief Gets a raw pointer to the underlying shared memory buffer structure.
   * Use with caution. Primarily intended for the consumer or advanced usage.
   * @return Pointer to the shared_ring_buffer_t, or nullptr if not connected.
   */
  inline __attribute__((always_inline)) shared_ring_buffer_t *
  get_raw_buf() const noexcept {
    return shm_buf_;
  }

  /**
   * @brief Gets the size of the mapped shared memory region.
   * @return Size in bytes.
   */
  inline __attribute__((always_inline)) size_t
  get_mapped_size() const noexcept {
    return get_rb_shm_size();
  }

  /**
   * @brief Gets the file descriptor of the opened device.
   * @return The file descriptor, or -1 if not connected.
   */
  inline __attribute__((always_inline)) int get_fd() const noexcept {
    return fd_;
  }

  inline __attribute__((always_inline)) size_t
  get_rb_capacity() const noexcept {
    return rb_runtime_capacity_;
  }
  inline __attribute__((always_inline)) size_t
  get_rb_idx_mask() const noexcept {
    return rb_runtime_idx_mask_;
  }
  inline __attribute__((always_inline)) size_t
  get_rb_shm_size() const noexcept {
    return rb_runtime_shm_size_;
  }
};

} // namespace HiResLogger