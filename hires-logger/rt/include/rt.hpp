#pragma once // Use pragma once for simplicity in headers

#include <optional>
#include <string>
#include <stdexcept> // For exceptions
#include <atomic>    // For std::atomic
#include <cstddef>   // For size_t
#include <cstdint>   // For uint types

#include "../../shared/common.h"

namespace HiResLogger {

class HiResError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class HiResConn {
private:
    int fd_ = -1;                     // File descriptor for /dev/khires
    shared_ring_buffer_t* shm_buf_ = nullptr; // Mapped pointer
    size_t shm_size_ = 0;             // Size of the mapped region
    size_t rb_runtime_size_ = 0;      // Size of the ring buffer
    size_t rb_runtime_mask_ = 0;      // Mask for ring buffer size

    // Helper to get monotonic time
    static uint64_t get_monotonic_ns();

public:
    /**
     * @brief Constructs a connection, opening and mmapping the device.
     * @param device_path Path to the HiResLogger character device.
     * @throws HiResError if opening or mmapping fails.
     */
    explicit HiResConn(const std::string& device_path = "/dev/khires");

    /**
     * @brief Destructor, automatically unmaps and closes the device.
     */
    ~HiResConn();

    // --- Rule of Five: Disable copy/move for simplicity ---
    // Prevent accidental copying or moving which would mess up resource management.
    // Could be implemented properly if needed, but deletion is safer for now.
    HiResConn(const HiResConn&) = delete;
    HiResConn& operator=(const HiResConn&) = delete;
    HiResConn(HiResConn&&) = delete;
    HiResConn& operator=(HiResConn&&) = delete;

    /**
     * @brief Logs an event to the shared ring buffer (Userspace Producer Logic).
     * @param event_id Identifier for the event type.
     * @param data1 Custom data payload 1.
     * @param data2 Custom data payload 2.
     * @return True on success, false if the buffer was full and the entry was dropped.
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
    inline __attribute__((always_inline)) shared_ring_buffer_t* get_raw_buf() const noexcept {
        return shm_buf_;
    }

     /**
     * @brief Gets the size of the mapped shared memory region.
     * @return Size in bytes.
     */
    inline __attribute__((always_inline)) size_t get_mapped_size() const noexcept {
        return shm_size_;
    }

     /**
     * @brief Gets the file descriptor of the opened device.
     * @return The file descriptor, or -1 if not connected.
     */
    inline __attribute__((always_inline)) int get_fd() const noexcept {
        return fd_;
    }
    
    inline __attribute__((always_inline)) size_t get_rb_size() const noexcept { return rb_runtime_size_; }
    inline __attribute__((always_inline)) size_t get_rb_mask() const noexcept { return rb_runtime_mask_; }
};

} // namespace Profiler