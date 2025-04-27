#include "../include/profiler_c_api.h"
#include "../include/profiler_rt.hpp" // Include the C++ implementation header
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

const char* profiler_get_last_error(void) {
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

ProfilerConnectionHandle* profiler_connect(const char* device_path) {
    set_last_error(""); // Clear last error
    try {
        std::string path = (device_path != nullptr) ? device_path : "/dev/profiler_buf";
        Profiler::ProfilerConnection* conn = new Profiler::ProfilerConnection(path);
        // Cast to opaque handle type
        return reinterpret_cast<ProfilerConnectionHandle*>(conn);
    } catch (const Profiler::ProfilerError& e) {
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

void profiler_disconnect(ProfilerConnectionHandle* handle) {
    set_last_error(""); // Clear last error
    if (handle != nullptr) {
        // Cast back to C++ type and delete
        Profiler::ProfilerConnection* conn = reinterpret_cast<Profiler::ProfilerConnection*>(handle);
        delete conn;
    }
}

bool profiler_log(ProfilerConnectionHandle* handle, uint32_t event_id, uint64_t data1, uint64_t data2) {
    set_last_error(""); // Clear last error
    if (handle == nullptr) {
        set_last_error("Invalid handle passed to profiler_log");
        return false;
    }
    Profiler::ProfilerConnection* conn = reinterpret_cast<Profiler::ProfilerConnection*>(handle);
    try {
        // The C++ log method already handles buffer full case and returns bool
        return conn->log(event_id, data1, data2);
    } catch (const std::exception& e) {
        // Should ideally not throw from log, but catch just in case
        set_last_error(std::string("Exception during log: ") + e.what());
        return false;
    } catch (...) {
         set_last_error("Unknown exception during log");
        return false;
    }
}

shared_ring_buffer_t* profiler_get_buffer(ProfilerConnectionHandle* handle) {
    set_last_error(""); // Clear last error
    if (handle == nullptr) {
        set_last_error("Invalid handle passed to profiler_get_buffer");
        return nullptr;
    }
    Profiler::ProfilerConnection* conn = reinterpret_cast<Profiler::ProfilerConnection*>(handle);
    return conn->getRawBuffer();
}

size_t profiler_get_buffer_size(ProfilerConnectionHandle* handle) {
    set_last_error(""); // Clear last error
     if (handle == nullptr) {
        set_last_error("Invalid handle passed to profiler_get_buffer_size");
        return 0;
    }
    Profiler::ProfilerConnection* conn = reinterpret_cast<Profiler::ProfilerConnection*>(handle);
    return conn->getMappedSize();
}


} // extern "C"