//! Safe Rust wrapper around the profiler_rt_sys FFI bindings.

use profiler_rt_sys as ffi; // Use the raw bindings crate
use std::ffi::{CStr, CString};
use std::marker::PhantomData;
use std::path::Path;
use std::sync::atomic::{AtomicUsize, AtomicU64, Ordering}; // For accessing shared buffer atomics
use std::ptr;
use std::fmt;

// Re-export shared types for convenience, ensuring they match FFI defs
pub use ffi::{log_entry_t, shared_ring_buffer_t, LOG_FLAG_VALID, LOG_FLAG_KERNEL};


// --- Error Handling ---
#[derive(Debug)]
pub struct ProfilerError {
    message: String,
}

impl std::error::Error for ProfilerError {}

impl fmt::Display for ProfilerError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Profiler runtime error: {}", self.message)
    }
}

// Helper to check for errors from the C API
fn check_error() -> Result<(), ProfilerError> {
    let err_ptr = unsafe { ffi::profiler_get_last_error() };
    if err_ptr.is_null() {
        Ok(())
    } else {
        let err_cstr = unsafe { CStr::from_ptr(err_ptr) };
        Err(ProfilerError {
            message: err_cstr.to_string_lossy().into_owned(),
        })
    }
}


// --- Safe Wrapper Struct ---
/// Represents a connection to the profiler device.
/// Manages the lifetime of the underlying C handle using RAII.
pub struct ProfilerConnection<'a> {
    handle: *mut ffi::ProfilerConnectionHandle,
    // Use PhantomData to indicate lifetime relationship if buffer access is tied
    // to the connection's lifetime, although the buffer itself is static memory.
    // Not strictly needed here as get_buffer returns a raw pointer.
    _marker: PhantomData<&'a ()>,
}

impl<'a> ProfilerConnection<'a> {
    /// Connects to the profiler device.
    ///
    /// # Arguments
    /// * `device_path` - Optional path to the device node (e.g., "/dev/profiler_buf").
    ///                   Uses default if None.
    ///
    /// # Errors
    /// Returns `ProfilerError` if connection fails.
    pub fn connect(device_path: Option<&Path>) -> Result<Self, ProfilerError> {
        let path_cstr = device_path
            .map(|p| CString::new(p.to_string_lossy().as_bytes()))
            .transpose()
            .map_err(|e| ProfilerError { message: format!("Invalid device path: {}", e) })?;

        let c_path_ptr = path_cstr.as_ref().map_or(ptr::null(), |cs| cs.as_ptr());

        let handle = unsafe { ffi::profiler_connect(c_path_ptr) };
        if handle.is_null() {
            check_error()?; // Check error if handle is null
            // If check_error didn't return Err, something unexpected happened
            Err(ProfilerError { message: "profiler_connect returned null without setting error".to_string() })
        } else {
            Ok(ProfilerConnection { handle, _marker: PhantomData })
        }
    }

    /// Logs an event to the shared ring buffer.
    ///
    /// # Arguments
    /// * `event_id` - Identifier for the event type.
    /// * `data1` - Custom data payload 1.
    /// * `data2` - Custom data payload 2.
    ///
    /// # Returns
    /// `true` if the event was logged successfully.
    /// `false` if the buffer was full and the event was dropped.
    pub fn log(&self, event_id: u32, data1: u64, data2: u64) -> bool {
        if self.handle.is_null() { return false; } // Should not happen with RAII wrapper
        unsafe { ffi::profiler_log(self.handle, event_id, data1, data2) }
        // Note: We don't check error here, as false return indicates buffer full, not API error.
    }

    /// Gets a raw pointer to the underlying shared memory buffer structure.
    ///
    /// # Safety
    /// Accessing the returned pointer requires `unsafe` code. The caller must
    /// ensure correct synchronization (atomics, memory ordering) when reading
    /// or writing fields, especially `head`, `tail`, `dropped_count`, and
    /// individual `log_entry_t` flags and data, according to the MPSC protocol.
    /// The pointer is valid as long as this `ProfilerConnection` object exists.
    pub unsafe fn get_raw_buffer(&self) -> *mut shared_ring_buffer_t {
        if self.handle.is_null() { return ptr::null_mut(); }
        ffi::profiler_get_buffer(self.handle)
    }

     /// Gets the size of the mapped shared memory region.
     pub fn get_buffer_size(&self) -> usize {
         if self.handle.is_null() { return 0; }
         unsafe { ffi::profiler_get_buffer_size(self.handle) }
     }

    // --- Consumer-specific helpers (could be in a separate Consumer struct) ---

    /// Reads the current value of the head pointer atomically.
    /// Uses Acquire ordering to ensure visibility of producer writes.
    ///
    /// # Safety
    /// Requires a valid buffer pointer obtained from `get_raw_buffer`.
    pub unsafe fn read_head_acquire(buffer: *mut shared_ring_buffer_t) -> usize {
        // Assuming shared_ring_buffer_t::head is compatible with AtomicUsize
        (*buffer).head.load(Ordering::Acquire)
    }

    /// Reads the current value of the tail pointer atomically.
    /// Uses Relaxed ordering as only the consumer modifies tail.
    ///
    /// # Safety
    /// Requires a valid buffer pointer obtained from `get_raw_buffer`.
     pub unsafe fn read_tail_relaxed(buffer: *mut shared_ring_buffer_t) -> usize {
        (*buffer).tail.load(Ordering::Relaxed)
    }

    /// Writes a new value to the tail pointer atomically.
    /// Uses Release ordering to make consumed space visible to producers.
    ///
    /// # Safety
    /// Requires a valid buffer pointer obtained from `get_raw_buffer`.
    /// Only the single consumer should ever call this.
    pub unsafe fn write_tail_release(buffer: *mut shared_ring_buffer_t, value: usize) {
         (*buffer).tail.store(value, Ordering::Release);
    }

    /// Reads the flags of a log entry atomically.
    /// Uses Acquire ordering to ensure visibility of producer data writes before the VALID flag.
    ///
    /// # Safety
    /// Requires a valid pointer to a `log_entry_t` within the buffer.
    pub unsafe fn read_flags_acquire(entry: *const log_entry_t) -> u16 {
        // Assuming log_entry_t::flags is compatible with AtomicU16
        (*(entry as *const std::sync::atomic::AtomicU16)).load(Ordering::Acquire)
    }

     /// Reads the dropped count atomically.
     /// Uses Relaxed ordering, as exact timing isn't usually critical.
     ///
     /// # Safety
     /// Requires a valid buffer pointer obtained from `get_raw_buffer`.
     pub unsafe fn read_dropped_count_relaxed(buffer: *mut shared_ring_buffer_t) -> u64 {
         (*buffer).dropped_count.load(Ordering::Relaxed)
     }
}

// Implement Drop to automatically call profiler_disconnect
impl<'a> Drop for ProfilerConnection<'a> {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe { ffi::profiler_disconnect(self.handle) };
            self.handle = ptr::null_mut(); // Prevent double free
        }
    }
}

// Implement Send/Sync if the handle itself is thread-safe (depends on C++ lib's internals)
// Assuming the C++ object itself doesn't have hidden thread-unsafe state,
// and operations like log() are atomic w.r.t the shared buffer, it should be safe.
unsafe impl<'a> Send for ProfilerConnection<'a> {}
unsafe impl<'a> Sync for ProfilerConnection<'a> {}