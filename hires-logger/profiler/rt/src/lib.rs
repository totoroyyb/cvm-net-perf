//! Safe Rust wrapper for FFI bindings.

use rt_ffi as ffi;
use std::ffi::{CStr, CString};
use std::fmt;
use std::marker::PhantomData;
use std::ops::Deref;
use std::path::Path;
use std::ptr;

// Re-export shared types for convenience, ensuring they match FFI defs
pub use ffi::{LOG_FLAG_KERNEL, LOG_FLAG_VALID, log_entry_t, shared_ring_buffer_t};

// --- Error Handling ---
#[derive(Debug)]
pub struct HiResError {
    message: String,
}

impl std::error::Error for HiResError {}

impl fmt::Display for HiResError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "HiResLogger runtime error: {}", self.message)
    }
}

// Helper to check for errors from the C API
fn check_error() -> Result<(), HiResError> {
    let err_ptr = unsafe { ffi::hires_get_last_error() };
    if err_ptr.is_null() {
        Ok(())
    } else {
        let err_cstr = unsafe { CStr::from_ptr(err_ptr) };
        Err(HiResError {
            message: err_cstr.to_string_lossy().into_owned(),
        })
    }
}

// --- Safe Wrapper Struct ---
#[repr(align(64))]
pub struct AlignedU64(pub u64);

impl Deref for AlignedU64 {
    type Target = u64;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

pub struct HiResConn<'a> {
    handle: *mut ffi::HiResLoggerConnHandle,
    pub cycle_per_us: AlignedU64, 
    // Use PhantomData to indicate lifetime relationship if buffer access is tied
    // to the connection's lifetime, although the buffer itself is static memory.
    // Not strictly needed here as get_buffer returns a raw pointer.
    _marker: PhantomData<&'a ()>,
}

impl<'a> HiResConn<'a> {
    /// Connects to the profiler device.
    ///
    /// # Arguments
    /// * `device_path` - Optional path to the device node (e.g., "/dev/khires").
    ///                   Uses default if None.
    ///
    /// # Errors
    /// Returns `HiResError` if connection fails.
    pub fn connect(device_path: Option<&Path>) -> Result<Self, HiResError> {
        let path_cstr = device_path
            .map(|p| CString::new(p.to_string_lossy().as_bytes()))
            .transpose()
            .map_err(|e| HiResError {
                message: format!("Invalid device path: {}", e),
            })?;

        let c_path_ptr = path_cstr.as_ref().map_or(ptr::null(), |cs| cs.as_ptr());

        let handle = unsafe { ffi::hires_connect(c_path_ptr) };
        if handle.is_null() {
            check_error()?; // Check error if handle is null
            // If check_error didn't return Err, something unexpected happened
            Err(HiResError {
                message: "profiler_connect returned null without setting error".to_string(),
            })
        } else {
            let cycle_per_us = unsafe { ffi::hires_get_cycles_per_us(handle) };
            Ok(HiResConn {
                handle,
                cycle_per_us: AlignedU64(cycle_per_us),
                _marker: PhantomData,
            })
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
    #[inline]
    pub fn log(&self, event_id: u32, data1: u64, data2: u64) -> bool {
        if self.handle.is_null() {
            return false;
        } // Should not happen with RAII wrapper
        unsafe { ffi::hires_log(self.handle, event_id, data1, data2) }
        // Note: We don't check error here, as false return indicates buffer full, not API error.
    }

    #[inline]
    pub fn pop(&self) -> Option<log_entry_t> {
        if self.handle.is_null() {
            return None;
        }
        let mut entry = log_entry_t::default();
        let result = unsafe { ffi::hires_pop(self.handle, &mut entry) };
        if result { Some(entry) } else { None }
    }

    #[inline]
    pub fn get_rb_capacity(&self) -> u64 {
        if self.handle.is_null() {
            return 0;
        }
        return unsafe { ffi::hires_get_rb_capacity(self.handle) as u64 };
    }

    #[inline]
    pub fn get_rb_idx_mask(&self) -> u64 {
        if self.handle.is_null() {
            return 0;
        }
        return unsafe { ffi::hires_get_rb_idx_mask(self.handle) as u64 };
    }
    
    #[inline]
    pub fn get_drop_num(&self) -> u64 {
        if self.handle.is_null() {
            return 0;
        }
        return unsafe { ffi::hires_get_drop_num(self.handle) as u64 };
    }

    /// Gets a raw pointer to the underlying shared memory buffer structure.
    ///
    /// # Safety
    /// Accessing the returned pointer requires `unsafe` code. The caller must
    /// ensure correct synchronization (atomics, memory ordering) when reading
    /// or writing fields, especially `head`, `tail`, `dropped_count`, and
    /// individual `log_entry_t` flags and data, according to the MPSC protocol.
    /// The pointer is valid as long as this `ProfilerConnection` object exists.
    #[inline]
    pub unsafe fn get_raw_buffer(&self) -> *mut shared_ring_buffer_t {
        if self.handle.is_null() {
            return ptr::null_mut();
        }
        unsafe { ffi::hires_get_buffer(self.handle) }
    }

    /// Gets the size of the mapped shared memory region.
    #[inline]
    pub fn get_shm_size(&self) -> u64 {
        if self.handle.is_null() {
            return 0;
        }
        unsafe { ffi::hires_get_shm_size(self.handle) as u64 }
    }
    
    #[inline]
    pub fn get_cycles_per_us(&self) -> u64 {
        return *self.cycle_per_us;
    }
}

#[inline]
fn rdtsc() -> u64 {
    unsafe { ffi::hires_rdtsc() }
}

#[inline]
fn rdtscp() -> (u64, u32) {
    let mut cpu_id: u32 = 0;
    let ts = unsafe { ffi::hires_rdtscp(&mut cpu_id as *mut u32) };
    return (ts, cpu_id as u32);
}

// Implement Drop to automatically call profiler_disconnect
impl<'a> Drop for HiResConn<'a> {
    fn drop(&mut self) {
        if !self.handle.is_null() {
            unsafe { ffi::hires_disconnect(self.handle) };
            self.handle = ptr::null_mut(); // Prevent double free
        }
    }
}

// Implement Send/Sync if the handle itself is thread-safe (depends on C++ lib's internals)
// Assuming the C++ object itself doesn't have hidden thread-unsafe state,
// and operations like log() are atomic w.r.t the shared buffer, it should be safe.
unsafe impl<'a> Send for HiResConn<'a> {}
unsafe impl<'a> Sync for HiResConn<'a> {}
