#include "../include/rt_c.h"
#include "../include/rt.hpp"
#include <cstddef>
#include <string>
#include <vector> // Could use vector for thread-local storage if needed

// --- Thread-Local Error Handling ---
// Simple mechanism using thread_local for storing the last error message.
// Note: Error message buffer size is fixed.
namespace { // Anonymous namespace for internal linkage
    thread_local std::string last_error_message;
    // Alternatively, use a fixed-size char array:
    // thread_local char last_error_buffer[256] = {0};
}

const char* hires_get_last_error(void) {
    if (last_error_message.empty()) {
        return nullptr;
    }
    return last_error_message.c_str();
    // If using char array:
    // return (last_error_buffer[0] == '\0') ? nullptr : last_error_buffer;
}

// Helper to set last error
static void set_last_error(const std::string& msg) {
    last_error_message = msg;
    // If using char array:
    // strncpy(last_error_buffer, msg.c_str(), sizeof(last_error_buffer) - 1);
    // last_error_buffer[sizeof(last_error_buffer) - 1] = '\0'; // Ensure null termination
}

// --- C API Implementation ---

extern "C" {

HiResLoggerConnHandle* hires_connect(const char* device_path) {
    set_last_error(""); // Clear last error
    try {
        std::string path = (device_path != nullptr) ? device_path : "/dev/khires";
        HiResLogger::HiResConn* conn = new HiResLogger::HiResConn(path);
        // Cast to opaque handle type
        return reinterpret_cast<HiResLoggerConnHandle*>(conn);
    } catch (const HiResLogger::HiResError& e) {
        set_last_error(e.what());
        return nullptr;
    } catch (const std::bad_alloc&) {
        set_last_error("Memory allocation failed during connect");
        return nullptr;
    } catch (...) {
        set_last_error("Unknown exception during connect");
        return nullptr;
    }
}

void hires_disconnect(HiResLoggerConnHandle* handle) {
    set_last_error(""); // Clear last error
    if (handle != nullptr) {
        // Cast back to C++ type and delete
        HiResLogger::HiResConn* conn = reinterpret_cast<HiResLogger::HiResConn*>(handle);
        delete conn;
    }
}

bool hires_log(HiResLoggerConnHandle* handle, uint32_t event_id, uint64_t data1, uint64_t data2) {
    set_last_error(""); // Clear last error
    if (handle == nullptr) {
        set_last_error("Invalid handle passed to profiler_log");
        return false;
    }
    HiResLogger::HiResConn* conn = reinterpret_cast<HiResLogger::HiResConn*>(handle);
    try {
        return conn->log(event_id, data1, data2);
    } catch (const std::exception& e) {
        set_last_error(std::string("Exception during log: ") + e.what());
        return false;
    } catch (...) {
         set_last_error("Unknown exception during log");
        return false;
    }
}

bool hires_pop(HiResLoggerConnHandle* handle, log_entry_t* entry) {
    set_last_error(""); // Clear last error
    if (handle == nullptr) {
        set_last_error("Invalid handle passed to hires_pop");
        return false;
    }
    if (entry == nullptr) {
        set_last_error("NULL entry pointer passed to hires_pop");
        return false;
    }

    HiResLogger::HiResConn* conn = reinterpret_cast<HiResLogger::HiResConn*>(handle);
    try {
        std::optional<log_entry_t> result = conn->pop();
        if (result.has_value()) {
            *entry = result.value(); // Copy the popped entry
            return true;
        } else {
            // Buffer might be empty or entry wasn't ready, not necessarily an error state
            // set_last_error("Buffer empty or entry not ready"); // Optional: set error if needed
            return false;
        }
    } catch (const std::exception& e) {
        set_last_error(std::string("Exception during pop: ") + e.what());
        return false;
    } catch (...) {
         set_last_error("Unknown exception during pop");
        return false;
    }
}

shared_ring_buffer_t* hires_get_buffer(HiResLoggerConnHandle* handle) {
    set_last_error(""); // Clear last error
    if (handle == nullptr) {
        set_last_error("Invalid handle passed to profiler_get_buffer");
        return nullptr;
    }
    HiResLogger::HiResConn* conn = reinterpret_cast<HiResLogger::HiResConn*>(handle);
    return conn->get_raw_buf();
}

size_t hires_get_buffer_size(HiResLoggerConnHandle* handle) {
    set_last_error(""); // Clear last error
     if (handle == nullptr) {
        set_last_error("Invalid handle passed to profiler_get_buffer_size");
        return 0;
    }
    HiResLogger::HiResConn* conn = reinterpret_cast<HiResLogger::HiResConn*>(handle);
    return conn->get_mapped_size();
}

size_t hires_get_rb_size(HiResLoggerConnHandle* handle) {
    set_last_error(""); // Clear last error
    if (handle == nullptr) {
        set_last_error("Invalid handle passed to profiler_get_rb_size");
        return 0;
    }
    HiResLogger::HiResConn* conn = reinterpret_cast<HiResLogger::HiResConn*>(handle);
    return conn->get_rb_size();
}

size_t hires_get_rb_mask(HiResLoggerConnHandle* handle) {
    set_last_error(""); // Clear last error
    if (handle == nullptr) {
        set_last_error("Invalid handle passed to profiler_get_rb_mask");
        return 0;
    }
    HiResLogger::HiResConn* conn = reinterpret_cast<HiResLogger::HiResConn*>(handle);
    return conn->get_rb_mask();
}

} // extern "C"