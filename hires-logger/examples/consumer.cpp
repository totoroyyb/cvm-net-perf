#include <iostream>
#include <thread>
#include <chrono>
#include <stdexcept>
#include <cstdint>
#include <optional>
#include <iomanip>

#include "rt.hpp"

int main() {
    try {
        // Connect to the logger device
        HiResLogger::HiResConn connection; // Uses default "/dev/khires"
        std::cout << "Consumer connected successfully." << std::endl;

        while (true) {
            // Attempt to pop an entry
            std::optional<log_entry_t> entry_opt = connection.pop();

            if (entry_opt.has_value()) {
                const log_entry_t& entry = entry_opt.value();
                // Print the received entry details
                std::cout << "Popped Entry: "
                          << "TS=" << entry.timestamp << ", "
                          << "EventID=" << entry.event_id << ", "
                          << "CPU=" << entry.cpu_id << ", "
                          << "Flags=0x" << std::hex << entry.flags << std::dec << ", " // Print flags in hex
                          << "Data1=" << entry.data1 << ", "
                          << "Data2=" << entry.data2
                          << std::endl;
            } else {
                // Buffer is empty or entry wasn't ready
                std::cout << "Buffer empty or entry not ready. Waiting..." << std::endl;
                // Wait for a bit before trying again
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
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
