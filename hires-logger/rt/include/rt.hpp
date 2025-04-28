#pragma once // Use pragma once for simplicity in headers

#include <string>
#include <stdexcept> // For exceptions
#include <atomic>    // For std::atomic
#include <cstddef>   // For size_t
#include <cstdint>   // For uint types

#include "../../shared/common.h"

// Forward declare the shared struct (avoids pulling the whole C header into C++ header)
// Or include it directly if preferred, but forward declaration is cleaner interface.
// struct shared_ring_buffer_t; // Defined in shared_profiler_data.h
// struct log_entry_t;          // Defined in shared_profiler_data.h

namespace Profiler {

// Exception class for runtime errors
class ProfilerError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};


class ProfilerConnection {
private:
    int fd_ = -1;                     // File descriptor for /dev/profiler_buf
    shared_ring_buffer_t* shm_buf_ = nullptr; // Mapped pointer
    size_t shm_size_ = 0;             // Size of the mapped region
    size_t rb_runtime_size_ = 0;      // Size of the ring buffer
    size_t rb_runtime_mask_ = 0;      // Mask for ring buffer size

    // Helper to get monotonic time
    static uint64_t get_monotonic_ns();

public:
    /**
     * @brief Constructs a connection, opening and mmapping the device.
     * @param device_path Path to the profiler character device.
     * @throws ProfilerError if opening or mmapping fails.
     */
    explicit ProfilerConnection(const std::string& device_path = "/dev/profiler_buf");

    /**
     * @brief Destructor, automatically unmaps and closes the device.
     */
    ~ProfilerConnection();

    // --- Rule of Five: Disable copy/move for simplicity ---
    // Prevent accidental copying or moving which would mess up resource management.
    // Could be implemented properly if needed, but deletion is safer for now.
    ProfilerConnection(const ProfilerConnection&) = delete;
    ProfilerConnection& operator=(const ProfilerConnection&) = delete;
    ProfilerConnection(ProfilerConnection&&) = delete;
    ProfilerConnection& operator=(ProfilerConnection&&) = delete;

    /**
     * @brief Logs an event to the shared ring buffer (Userspace Producer Logic).
     * @param event_id Identifier for the event type.
     * @param data1 Custom data payload 1.
     * @param data2 Custom data payload 2.
     * @return True on success, false if the buffer was full and the entry was dropped.
     */
    bool log(uint32_t event_id, uint64_t data1 = 0, uint64_t data2 = 0);

    /**
     * @brief Gets a raw pointer to the underlying shared memory buffer structure.
     * Use with caution. Primarily intended for the consumer or advanced usage.
     * @return Pointer to the shared_ring_buffer_t, or nullptr if not connected.
     */
    shared_ring_buffer_t* getRawBuffer() const noexcept {
        return shm_buf_;
    }

     /**
     * @brief Gets the size of the mapped shared memory region.
     * @return Size in bytes.
     */
    size_t getMappedSize() const noexcept {
        return shm_size_;
    }

     /**
     * @brief Gets the file descriptor of the opened device.
     * @return The file descriptor, or -1 if not connected.
     */
    int getFileDescriptor() const noexcept {
        return fd_;
    }
};

} // namespace Profiler