use clap::Parser;
use rt::{HiResConn, LOG_FLAG_VALID, log_entry_t};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::thread;
use std::time::Duration;

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

const MAX_EVENT_BUCKET_SIZE: usize = 256;
const DEFAULT_DATA_CAPACITY: usize = 1 << 25; // 32MB

#[repr(align(64))]
#[derive(Default)]
struct Event {
    id: u64,
    count: u64,
    data: Vec<u64>,
}

impl Event {
    fn new(id: u64) -> Self {
        Event {
            id,
            count: 0,
            data: Vec::with_capacity(DEFAULT_DATA_CAPACITY),
        }
    }

    fn add_data(&mut self, data: u64) {
        if self.data.len() < DEFAULT_DATA_CAPACITY {
            self.count += 1;
            self.data.push(data);
        } else {
            eprintln!("Warning: Data capacity exceeded for event ID {}", self.id);
        }
    }

    fn avg(&self) -> f32 {
        if self.count > 0 {
            let sum: u64 = self.data.iter().sum();
            let avg = (sum as f32) / (self.count as f32);
            return avg;
        }
        return 0.0;
    }

    fn summary(&self) -> EventResult {
        EventResult {
            id: self.id,
            count: self.count,
            avg: self.avg(),
        }
    }
}

struct Benchmarks {
    event_bucket: [Event; MAX_EVENT_BUCKET_SIZE],
}

impl Benchmarks {
    fn new() -> Self {
        let event_bucket = std::array::from_fn(|i| Event {
            id: i as u64,
            count: 0,
            data: Vec::with_capacity(DEFAULT_DATA_CAPACITY),
        });
        Benchmarks { event_bucket }
    }

    fn summary(&self) -> Vec<EventResult> {
        self
            .event_bucket
            .iter()
            .map(|e| e.summary())
            .filter(|e| e.count > 0)
            .collect::<Vec<EventResult>>()
        // for entry in result.iter() {
        //     println!(
        //         "Event ID: {}, Count: {}, Average: {}",
        //         entry.id, entry.count, entry.avg
        //     );
        // }
    }
}

struct EventResult {
    id: u64,
    count: u64,
    avg: f32,
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args = Args::parse();

    let mut bench = Benchmarks::new();

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

    // --- Setup Ctrl+C Handler ---
    let running = Arc::new(AtomicBool::new(true));
    let r = running.clone();

    ctrlc::set_handler(move || {
        println!("\nCtrl+C received, shutting down...");
        r.store(false, Ordering::SeqCst);
    })?;

    println!("Ctrl+C handler set. Press Ctrl+C to stop.");

    // --- Consumer Loop ---
    let mut entries_processed: u64 = 0;
    let mut last_dropped_count: u64 = 0;

    println!("Starting consumer loop...");

    while running.load(Ordering::SeqCst) {
        let entry = connection.pop();

        if let Some(entry) = entry {
            if entry.flags & (LOG_FLAG_VALID as u16) != 0 {
                // println!("Entry: {:?}", entry);
                entries_processed += 1;
                let e_id = entry.event_id;
                let b_entry = &mut bench.event_bucket[e_id as usize];
                b_entry.add_data(entry.data1);
            } else {
                println!("Invalid entry received.");
            }
        } else {
            if args.poll_interval_ms > 0 {
                if running.load(Ordering::SeqCst) {
                    thread::sleep(Duration::from_millis(args.poll_interval_ms));
                }
            } else {
                // we want to burn the CPU to get the fastest possible consume rate.
                // thread::yield_now();
            }
        }
        // Optional: Check for dropped count if needed
        // let current_dropped = connection.get_dropped_count();
        // if current_dropped > last_dropped_count {
        //     println!("Warning: {} entries dropped.", current_dropped - last_dropped_count);
        //     last_dropped_count = current_dropped;
        // }
    }
    
    // --- Summary ---
    println!("---- Summary ----");
    let cycle_rate = connection.get_cycles_per_us();
    let result = bench.summary();
    for entry in result.iter() {
        println!(
            "Event ID: {}, Count: {}, Average: {}, Duration: {} us",
            entry.id, entry.count, entry.avg, entry.avg / (cycle_rate as f32)
        );
    }
    println!();
    
    let drop_num = connection.get_drop_num();
    println!(
        "Total entries processed: {}, Total entries dropped: {}",
        entries_processed, drop_num
    );

    Ok(())
}
