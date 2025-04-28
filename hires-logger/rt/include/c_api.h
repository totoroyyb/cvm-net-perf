#ifndef PROFILER_C_API_H
#define PROFILER_C_API_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Opaque pointer type for the ProfilerConnection object
typedef struct ProfilerConnectionHandle ProfilerConnectionHandle;

// Bring in the shared struct definition for users of the C API
#include "../../shared/common.h"


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates a profiler connection object.
 * Opens and mmaps the profiler device.
 * @param device_path Path to the device (e.g., "/dev/profiler_buf"). If NULL, uses default.
 * @return A handle to the connection object, or NULL on failure.
 * Call profiler_get_last_error() for details on failure.
 */
ProfilerConnectionHandle* profiler_connect(const char* device_path);

/**
 * @brief Destroys a profiler connection object.
 * Unmaps the shared memory and closes the device file descriptor.
 * @param handle The handle returned by profiler_connect. If NULL, does nothing.
 */
void profiler_disconnect(ProfilerConnectionHandle* handle);

/**
 * @brief Logs an event using the provided connection handle.
 * @param handle The handle returned by profiler_connect. Must not be NULL.
 * @param event_id Identifier for the event type.
 * @param data1 Custom data payload 1.
 * @param data2 Custom data payload 2.
 * @return True on success, false if the buffer was full and the entry was dropped,
 * or if the handle is invalid.
 */
bool profiler_log(ProfilerConnectionHandle* handle, uint32_t event_id, uint64_t data1, uint64_t data2);

/**
 * @brief Gets a raw pointer to the shared ring buffer structure.
 * Use with extreme caution. Allows direct manipulation/reading of the buffer.
 * @param handle The handle returned by profiler_connect. Must not be NULL.
 * @return Pointer to the shared_ring_buffer_t structure, or NULL if handle is invalid.
 */
shared_ring_buffer_t* profiler_get_buffer(ProfilerConnectionHandle* handle);

/**
 * @brief Gets the size of the mapped shared memory region associated with the handle.
 * @param handle The handle returned by profiler_connect. Must not be NULL.
 * @return Size in bytes, or 0 if handle is invalid.
 */
size_t profiler_get_buffer_size(ProfilerConnectionHandle* handle);

/**
 * @brief Gets the last error message encountered by the API functions for the current thread.
 * Note: This is a simple thread-local error reporting mechanism. Not robust for
 * complex multi-threaded error handling within the same API usage sequence.
 * @return A pointer to a statically allocated string containing the last error message,
 * or NULL if no error occurred since the last call. The string is valid
 * until the next API call in the same thread that might generate an error.
 */
const char* profiler_get_last_error(void);


#ifdef __cplusplus
} // extern "C"
#endif

#endif // PROFILER_C_API_H