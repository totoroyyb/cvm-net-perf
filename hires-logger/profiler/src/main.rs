use rt::{HiResConn, log_entry_t, LOG_FLAG_VALID};
use std::ptr;
use std::thread;
use std::time::Duration;
use std::sync::atomic::Ordering;
use clap::Parser; // For command-line args

#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    /// Path to the profiler device node
    #[arg(short, long, default_value = "/dev/khires")]
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
    let connection = HiResConn::connect(Some(args.device.as_ref()))?;
    println!("Connected successfully.");

    // Get the raw buffer pointer (requires unsafe block to use)
    // let buffer_ptr = unsafe { connection.get_raw_buffer() };
    // if buffer_ptr.is_null() {
    //     eprintln!("Error: Failed to get shared buffer pointer.");
    //     return Ok(()); // Or return an error
    // }

    let size = connection.get_rb_capacity();
    let mask = connection.get_rb_idx_mask();
    
     println!("Buffer Size: {}, Mask: 0x{:x}", size, mask);
     if size == 0 || (size & mask) != 0 {
         eprintln!("Error: Invalid buffer size/mask read from shared memory.");
         return Ok(());
     }

    // --- Consumer Loop ---
    let mut entries_processed: u64 = 0;
    let mut last_dropped_count: u64 = 0;

    println!("Starting consumer loop...");

    loop {
        let entry = connection.pop();
        
        if let Some(entry) = entry {
            // Process the log entry
            if entry.flags & (LOG_FLAG_VALID as u16) != 0 {
                // Valid entry, process it
                entries_processed += 1;
                println!("Entry: {:?}", entry);
            } else {
                // Invalid entry, handle accordingly
                println!("Invalid entry received.");
            }
        } else {
            // Buffer is empty, sleep for the specified interval
            thread::sleep(Duration::from_millis(args.poll_interval_ms));
        }

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