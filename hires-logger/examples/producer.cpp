#include <iostream>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <cstdint>

#include "../rt/include/rt.hpp"

int main() {
    try {
        // Connect to the logger device
        HiResLogger::HiResConn connection; // Uses default "/dev/khires"
        std::cout << "Producer connected successfully." << std::endl;

        uint64_t counter = 0;
        uint32_t event_id = 1001; // Example event ID

        while (true) {
            // Log an event
            bool success = connection.log(event_id, counter, counter * 2);

            if (success) {
                std::cout << "Logged event: ID=" << event_id << ", data1=" << counter << std::endl;
            } else {
                std::cerr << "Failed to log event (buffer full?). Dropped count might increase." << std::endl;
                // Optional: Add a longer sleep or different handling if buffer is consistently full
            }

            counter++;

            // Wait for a short period before logging the next event
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }

    } catch (const HiResLogger::HiResError& e) {
        std::cerr << "HiResLogger Error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Standard Exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception occurred." << std::endl;
        return 1;
    }

    return 0; // Should not be reached in the infinite loop
}
