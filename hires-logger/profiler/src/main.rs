use profiler_rt::{ProfilerConnection, log_entry_t, LOG_FLAG_VALID};
use std::ptr;
use std::thread;
use std::time::Duration;
use std::sync::atomic::Ordering;
use clap::Parser; // For command-line args

#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    /// Path to the profiler device node
    #[arg(short, long, default_value = "/dev/profiler_buf")]
    device: String,

    /// Polling interval in milliseconds when buffer is empty
    #[arg(short, long, default_value_t = 10)]
    poll_interval_ms: u64,
}


fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args = Args::parse();

    println!("Profiler Consumer starting...");
    println!("Connecting to device: {}", args.device);
    println!("Polling interval: {} ms", args.poll_interval_ms);

    // Connect using the safe wrapper
    let connection = ProfilerConnection::connect(Some(args.device.as_ref()))?;
    println!("Connected successfully.");

    // Get the raw buffer pointer (requires unsafe block to use)
    let buffer_ptr = unsafe { connection.get_raw_buffer() };
    if buffer_ptr.is_null() {
        eprintln!("Error: Failed to get shared buffer pointer.");
        return Ok(()); // Or return an error
    }

    // Read buffer constants (safely via pointer deref within unsafe block)
    let (buffer_size, buffer_mask) = unsafe {
        ((*buffer_ptr).buffer_size, (*buffer_ptr).size_mask)
    };
     println!("Buffer Size: {}, Mask: 0x{:x}", buffer_size, buffer_mask);
     if buffer_size == 0 || (buffer_size & buffer_mask) != 0 {
         eprintln!("Error: Invalid buffer size/mask read from shared memory.");
         return Ok(());
     }

    // --- Consumer Loop ---
    let mut current_tail: usize = unsafe { ProfilerConnection::read_tail_relaxed(buffer_ptr) };
    let mut entries_processed: u64 = 0;
    let mut last_dropped_count: u64 = 0;

    println!("Starting consumer loop...");

    loop {
        // Read head with acquire semantics to see producer writes
        let head = unsafe { ProfilerConnection::read_head_acquire(buffer_ptr) };

        if current_tail == head {
            // Buffer is empty
            // Check for dropped count changes
            let dropped = unsafe { ProfilerConnection::read_dropped_count_relaxed(buffer_ptr) };
            if dropped != last_dropped_count {
                 println!("Warning: {} entries dropped by producers.", dropped - last_dropped_count);
                 last_dropped_count = dropped;
            }

            // Wait a bit before polling again
            thread::sleep(Duration::from_millis(args.poll_interval_ms));
            continue; // Re-check head
        }

        // Buffer has data, process one entry
        let read_idx = current_tail & (buffer_mask as usize); // Apply mask
        let entry_ptr = unsafe { (*buffer_ptr).buffer.as_ptr().add(read_idx) };

        // Spin-wait for the VALID flag (use acquire load)
        // Add a yield/sleep inside the spin to prevent burning CPU excessively
        let mut spin_count = 0;
        while (unsafe { ProfilerConnection::read_flags_acquire(entry_ptr) } & LOG_FLAG_VALID) == 0 {
            // Producer hasn't finished writing this entry yet.
            // This shouldn't happen often if producers are faster than consumer,
            // but possible during context switches or high contention.
            spin_count += 1;
            if spin_count > 1000 { // Avoid infinite spinning if something is wrong
                 eprintln!("Warning: Spun too long waiting for VALID flag at index {}", read_idx);
                 // Consider breaking or advancing tail cautiously if this happens often
                 break;
            }
            // Yield CPU slice to allow producer to run
            thread::yield_now();
            // Alternatively, sleep for a microsecond:
            // thread::sleep(Duration::from_micros(1));
        }
         if (unsafe { ProfilerConnection::read_flags_acquire(entry_ptr) } & LOG_FLAG_VALID) == 0 {
             // Still not valid after spinning, maybe skip?
             // For now, let's try advancing cautiously, but log it.
             eprintln!("Warning: Advancing tail past potentially invalid entry at index {}", read_idx);
         } else {
            // Entry is valid, process it (unsafe block needed to dereference entry_ptr)
            let entry_data: log_entry_t = unsafe { ptr::read_volatile(entry_ptr) };
            entries_processed += 1;

            // --- Process the entry data ---
            println!(
                "Entry[{}]: TS={}, CPU={}, ID={}, Flags=0x{:x}, Data1={}, Data2={}",
                current_tail, // Use non-masked tail for sequence
                entry_data.timestamp,
                entry_data.cpu_id,
                entry_data.event_id,
                entry_data.flags,
                entry_data.data1,
                entry_data.data2
            );
            // --- End Processing ---

            // Optional: Clear the VALID flag (relaxed store is fine)
            // unsafe {
            //     let flags_ptr = entry_ptr as *mut std::sync::atomic::AtomicU16;
            //     (*flags_ptr).store(entry_data.flags & !LOG_FLAG_VALID, Ordering::Relaxed);
            // }
        }


        // Advance tail pointer (Release semantics make space available)
        current_tail = current_tail.wrapping_add(1); // Handle wrap-around
        unsafe { ProfilerConnection::write_tail_release(buffer_ptr, current_tail) };

        // Add a small yield/sleep here too if consumer is much faster than producer
        // to prevent unnecessary busy-polling of the head pointer.
        // thread::yield_now();

        // Add termination condition (e.g., Ctrl+C handler)
        // For simplicity, this loop runs forever. Use signal handling (e.g., ctrlc crate)
        // in a real application.
    }

    // Cleanup is handled by ProfilerConnection's Drop impl when it goes out of scope
    // println!("Consumer shutting down.");
    // Ok(())
}