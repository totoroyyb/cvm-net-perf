#ifndef HIRES_RT_C_H
#define HIRES_RT_C_H

#include <cstdint>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "../../shared/common.h"

typedef struct HiResLoggerConnHandle HiResLoggerConnHandle;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates a profiler connection object.
 * Opens and mmaps the profiler device.
 * @param device_path Path to the device (e.g., "/dev/khires"). If NULL, uses default.
 * @return A handle to the connection object, or NULL on failure.
 * Call profiler_get_last_error() for details on failure.
 */
HiResLoggerConnHandle* hires_connect(const char* device_path);

/**
 * @brief Destroys a profiler connection object.
 * Unmaps the shared memory and closes the device file descriptor.
 * @param handle The handle returned by profiler_connect. If NULL, does nothing.
 */
void hires_disconnect(HiResLoggerConnHandle* handle);

/**
 * @brief Logs an event using the provided connection handle.
 * @param handle The handle returned by hires_connect. Must not be NULL.
 * @param event_id Identifier for the event type.
 * @param data1 Custom data payload 1.
 * @param data2 Custom data payload 2.
 * @return True on success, false if the buffer was full and the entry was dropped,
 * or if the handle is invalid.
 */
bool hires_log(HiResLoggerConnHandle* handle, uint32_t event_id, uint64_t data1, uint64_t data2);

/**
 * @brief Attempts to pop one log entry from the buffer using the provided handle.
 * @param handle The handle returned by hires_connect. Must not be NULL.
 * @param entry Pointer to a log_entry_t structure where the popped entry will be copied. Must not be NULL.
 * @return True if an entry was successfully popped and copied, false if the buffer
 * was empty, the entry wasn't ready, or if the handle/entry pointer is invalid.
 * Call hires_get_last_error() for details on failure.
 */
bool hires_pop(HiResLoggerConnHandle* handle, log_entry_t* entry);

/**
 * @brief Gets a raw pointer to the shared ring buffer structure.
 * Use with extreme caution. Allows direct manipulation/reading of the buffer.
 * @param handle The handle returned by profiler_connect. Must not be NULL.
 * @return Pointer to the shared_ring_buffer_t structure, or NULL if handle is invalid.
 */
shared_ring_buffer_t* hires_get_buffer(HiResLoggerConnHandle* handle);

/**
 * @brief Gets the size of the mapped shared memory region associated with the handle.
 * @param handle The handle returned by profiler_connect. Must not be NULL.
 * @return Size in bytes, or 0 if handle is invalid.
 */
size_t hires_get_shm_size(HiResLoggerConnHandle* handle);

size_t hires_get_rb_capacity(HiResLoggerConnHandle* handle);
size_t hires_get_rb_idx_mask(HiResLoggerConnHandle* handle);
uint64_t hires_get_cycles_per_us(HiResLoggerConnHandle* handle);

/**
 * @brief Gets the last error message encountered by the API functions for the current thread.
 * Note: This is a simple thread-local error reporting mechanism. Not robust for
 * complex multi-threaded error handling within the same API usage sequence.
 * @return A pointer to a statically allocated string containing the last error message,
 * or NULL if no error occurred since the last call. The string is valid
 * until the next API call in the same thread that might generate an error.
 */
const char* hires_get_last_error(void);

#ifdef __cplusplus
}
#endif

#endif // HIRES_RT_C_H