#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <string>
#include <stdexcept>
#include <vector>
#include <filesystem> 
#include <atomic> 
#include <csignal> 

const int TARGET_RATE = 50000; // lines per second
const int BATCH_SIZE = 1000;
const std::chrono::seconds RUN_DURATION(10);

std::atomic<bool> keep_running(true);
void signal_handler(int signum) {
    std::cout << "\nInterrupt signal (" << signum << ") received.\n";
    keep_running = false;
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Calculate the target time duration for writing one batch
    const std::chrono::duration<double> target_batch_duration(static_cast<double>(BATCH_SIZE) / TARGET_RATE);

    // --- Temporary File Setup (C++17) ---
    std::filesystem::path temp_file_path;
    try {
        temp_file_path = std::filesystem::temp_directory_path() / "dummy_writer_output.tmp";
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error getting temporary directory path: " << e.what() << std::endl;
        return 1;
    }

    std::string tmp_filename_str = temp_file_path.string(); // Convert path to string for ofstream

    std::cout << "Attempting to write to temporary file: " << tmp_filename_str << std::endl;
    std::cout << "Target rate: " << TARGET_RATE << " lines/sec" << std::endl;
    std::cout << "Batch size: " << BATCH_SIZE << " lines" << std::endl;
    std::cout << "Running for " << RUN_DURATION.count() << " seconds." << std::endl; // Updated message


    std::ofstream outfile(tmp_filename_str); // Use the string representation of the path
    if (!outfile) {
        // perror provides more system-specific error info
        std::perror(("Error opening file: " + tmp_filename_str).c_str());
        return 1;
    }

    // --- Writing Loop ---
    long long total_lines_written = 0;
    auto loop_start_time = std::chrono::steady_clock::now();
    auto last_status_time = loop_start_time;
    long long lines_since_last_status = 0;

    try {
        // Loop condition checks elapsed time and optional keep_running flag
        while (std::chrono::steady_clock::now() - loop_start_time < RUN_DURATION /* && keep_running */ ) {
            auto batch_start_time = std::chrono::steady_clock::now();

            // Write a batch of lines
            for (int i = 0; i < BATCH_SIZE; ++i) {
                // Construct the line content
                outfile << "Line " << (total_lines_written + i) << ": This is dummy log line number " << (total_lines_written + i) << " with some payload data.\n";
                // Basic check after writing a line (more robust checks could be added)
                if (!outfile) {
                    throw std::runtime_error("Error during file write operation.");
                }
            }
            // Flush the buffer periodically or at the end if needed, though ofstream handles this.
            outfile.flush();

            total_lines_written += BATCH_SIZE;
            lines_since_last_status += BATCH_SIZE;

            auto batch_end_time = std::chrono::steady_clock::now();
            std::chrono::duration<double> batch_elapsed = batch_end_time - batch_start_time;

            auto sleep_duration = target_batch_duration - batch_elapsed;

            if (sleep_duration > std::chrono::duration<double>::zero() &&
                (std::chrono::steady_clock::now() + sleep_duration) - loop_start_time < RUN_DURATION)
             {
                std::this_thread::sleep_for(sleep_duration);
            }
            // If batch_elapsed >= target_batch_duration, we don't sleep and try to catch up.
            // If sleeping would exceed RUN_DURATION, skip the sleep.

            // --- Status Update ---
            auto now = std::chrono::steady_clock::now();
            std::chrono::duration<double> time_since_last_status = now - last_status_time;

            // Print status roughly every second
            if (time_since_last_status.count() >= 1.0) {
                double current_rate = lines_since_last_status / time_since_last_status.count();
                std::cout << "Rate (last second): ~" << static_cast<long long>(current_rate)
                          << " lines/sec. Total lines written: " << total_lines_written
                          << ". Time elapsed: " << std::chrono::duration_cast<std::chrono::seconds>(now - loop_start_time).count() << "s"
                          << std::endl;

                // Reset status tracking
                last_status_time = now;
                lines_since_last_status = 0;
            }

            // --- Check remaining time ---
            // Break early if the next batch write + potential sleep might exceed the duration significantly
            // This is optional, the main while condition handles the primary exit.
            // if (std::chrono::steady_clock::now() - loop_start_time >= RUN_DURATION) {
            //     break;
            // }
        }
    } catch (const std::exception& e) {
        std::cerr << "\nRuntime error: " << e.what() << std::endl;
        outfile.close();
        std::filesystem::remove(temp_file_path);
        return 1;
    } catch (...) {
        std::cerr << "\nAn unknown error occurred." << std::endl;
        outfile.close();
        std::filesystem::remove(temp_file_path); 
        return 1;
    }

    // --- Cleanup and Final Stats ---
    outfile.close();
    auto loop_end_time = std::chrono::steady_clock::now();
    std::chrono::duration<double> total_duration = loop_end_time - loop_start_time;

    std::cout << "\nFinished writing." << std::endl;
    std::cout << "Target duration: " << RUN_DURATION.count() << " seconds." << std::endl;
    std::cout << "Actual duration: " << total_duration.count() << " seconds." << std::endl;
    std::cout << "Total lines written: " << total_lines_written << std::endl;
    if (total_duration.count() > 0) {
        double average_rate = total_lines_written / total_duration.count();
        std::cout << "Average rate: " << static_cast<long long>(average_rate) << " lines/sec." << std::endl;
    }
    std::cout << "Temporary file '" << tmp_filename_str << "' created." << std::endl;

    try {
         std::cout << "Removing temporary file: " << tmp_filename_str << std::endl;
         std::filesystem::remove(temp_file_path);
    } catch (const std::filesystem::filesystem_error& e) {
         std::cerr << "Warning: Could not remove temporary file '" << tmp_filename_str << "': " << e.what() << std::endl;
    }

    return 0;
}