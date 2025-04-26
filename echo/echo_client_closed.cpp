#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring> // For memset, strlen, strerror
#include <cstdlib> // For exit, atoi
#include <unistd.h> // For read, write, close
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // For TCP_NODELAY
#include <arpa/inet.h> // For inet_pton
#include <netdb.h> // For host resolution (though not strictly needed for IP)
#include <cerrno> // For errno
#include <atomic> // For atomic flag
#include <vector> // For storing latencies
#include <numeric> // For accumulate (optional)
#include <algorithm> // For sort, percentile calculation
#include <cmath> // For std::ceil

#define CLOSE_SOCKET close

// --- Configuration ---
const char* HOST = "127.0.0.1"; // Server IP address (localhost)
const int PORT = 65432;         // Server port (must match server)
const int BUFFER_SIZE = 1024;
const int NUM_CLIENTS = 20;      // Number of concurrent client threads (Primary Tuning Parameter)
const int RUN_DURATION_SECONDS = 10; // How long the test should run
// --- End Configuration ---

// Global flag to signal threads to stop
std::atomic<bool> keep_running(true);

// Helper function to print errors and exit (modified for threads)
void error(const char *msg, int thread_id = -1) {
    char error_buf[256];
    snprintf(error_buf, sizeof(error_buf), "Thread %d: ERROR %s", thread_id, msg);
    perror(error_buf); // Print error message based on errno (POSIX)
    if (thread_id != -1) {
         std::cerr << "Thread " << thread_id << " exiting due to error." << std::endl;
    }
    exit(1); // Exit the entire process on critical error
}

// Function executed by each client thread (Closed-Loop)
void client_thread_func(int thread_id, std::atomic<bool>& running_flag, std::vector<std::chrono::microseconds>& latencies) {
    int sockfd;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    int n;
    // Pre-allocate based on estimated max possible rate (adjust multiplier as needed)
    latencies.reserve(static_cast<size_t>(100000 * RUN_DURATION_SECONDS * 1.2)); 

    // 1. Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        error("opening socket", thread_id);
    }

    int flag = 1;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int)) < 0) {
        char error_msg[100];
        snprintf(error_msg, sizeof(error_msg), "Thread %d: WARNING setsockopt(TCP_NODELAY) failed", thread_id);
        perror(error_msg);
    }

    // Prepare the server address structure
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, HOST, &serv_addr.sin_addr) <= 0) {
        error("Invalid address/ Address not supported", thread_id);
    }

    // 2. Connect to the server
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
         if (errno == ECONNREFUSED) {
             std::cerr << "Thread " << thread_id << ": Connection failed. Server potentially down (" << HOST << ":" << PORT << ")." << std::endl;
         } else {
             char error_msg[100];
             snprintf(error_msg, sizeof(error_msg), "connecting (errno %d)", errno);
             perror(error_msg);
             std::cerr << "Thread " << thread_id << " failed to connect." << std::endl;
         }
         CLOSE_SOCKET(sockfd);
         return; // Exit this thread if connection fails
    }

    // 3. Communication loop (Closed-Loop: Send -> Receive -> Send ...)
    std::string message_base = "Hello from client thread " + std::to_string(thread_id) + " msg: ";
    long long msg_count = 0;

    while (running_flag.load()) { // Check the flag before starting a new request
        auto start_time = std::chrono::high_resolution_clock::now();

        std::string message = message_base + std::to_string(msg_count++);

        // Send the message
        n = write(sockfd, message.c_str(), message.length());
        if (n < 0) {
             if (errno == EPIPE) {
                 std::cerr << "Thread " << thread_id << ": Server closed connection (Broken pipe)." << std::endl;
             } else {
                 char error_msg[100];
                 snprintf(error_msg, sizeof(error_msg), "Thread %d: ERROR writing to socket", thread_id);
                 perror(error_msg);
             }
             break; // Exit loop on error
        }
         if (n < message.length()) {
            std::cerr << "Thread " << thread_id << ": WARNING: Not all data written to socket." << std::endl;
            // Potentially retry or handle partial write
        }

        // Receive the echo back from the server using POSIX read (blocking)
        memset(buffer, 0, BUFFER_SIZE);
        n = read(sockfd, buffer, BUFFER_SIZE - 1);
        if (n < 0) {
             char error_msg[100];
             snprintf(error_msg, sizeof(error_msg), "Thread %d: ERROR reading from socket", thread_id);
             perror(error_msg);
             break; // Exit loop on error
        } else if (n == 0) {
             std::cerr << "Thread " << thread_id << ": Server closed connection." << std::endl;
             break; // Exit loop
        }

        // Calculate and record latency
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        latencies.push_back(duration);

        // No artificial delay in closed-loop mode, loop continues immediately
    }

    // 4. Close the socket
    CLOSE_SOCKET(sockfd);
}

// Helper function to calculate percentiles
long long calculate_percentile(const std::vector<std::chrono::microseconds>& sorted_latencies, double percentile) {
    if (sorted_latencies.empty()) {
        return 0;
    }
    size_t index = static_cast<size_t>(std::ceil(percentile / 100.0 * sorted_latencies.size())) - 1;
    index = std::min(index, sorted_latencies.size() - 1); // Clamp index to valid range
    return sorted_latencies[index].count();
}

int main() {
    std::cout << "Starting Closed-Loop Test..." << std::endl;
    std::cout << "Number of client threads: " << NUM_CLIENTS << std::endl;
    std::cout << "Running for " << RUN_DURATION_SECONDS << " seconds." << std::endl;
    std::cout << "Target Server: " << HOST << ":" << PORT << std::endl;

    std::vector<std::thread> client_threads;
    std::vector<std::vector<std::chrono::microseconds>> all_latencies(NUM_CLIENTS);
    client_threads.reserve(NUM_CLIENTS);

    // Launch threads
    for (int i = 0; i < NUM_CLIENTS; ++i) {
        all_latencies[i].clear();
        client_threads.emplace_back(client_thread_func, i, std::ref(keep_running), std::ref(all_latencies[i]));
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Small startup delay
    }

    std::cout << "All client threads launched. Running workload..." << std::endl;

    // Wait for the specified duration
    std::this_thread::sleep_for(std::chrono::seconds(RUN_DURATION_SECONDS));

    // Signal threads to stop
    std::cout << "Time limit reached. Signaling threads to stop..." << std::endl;
    keep_running.store(false);

    // Join threads
    std::cout << "Waiting for client threads to finish..." << std::endl;
    for (auto& th : client_threads) {
        if (th.joinable()) {
            th.join();
        }
    }

    std::cout << "All client threads have finished." << std::endl;

    // Combine and process latencies
    std::vector<std::chrono::microseconds> combined_latencies;
    size_t total_requests = 0;
    for (const auto& thread_latencies : all_latencies) {
        combined_latencies.insert(combined_latencies.end(), thread_latencies.begin(), thread_latencies.end());
        total_requests += thread_latencies.size();
    }

    std::cout << "Processing results for " << total_requests << " completed requests..." << std::endl;

    if (combined_latencies.empty()) {
        std::cout << "No requests completed successfully." << std::endl;
    } else {
        std::sort(combined_latencies.begin(), combined_latencies.end());

        long long p50 = calculate_percentile(combined_latencies, 50.0);
        long long p90 = calculate_percentile(combined_latencies, 90.0);
        long long p95 = calculate_percentile(combined_latencies, 95.0);
        long long p99 = calculate_percentile(combined_latencies, 99.0);

        long long sum_us = 0;
        for(const auto& lat : combined_latencies) {
            sum_us += lat.count();
        }
        double avg_us = static_cast<double>(sum_us) / combined_latencies.size();
        double throughput_rps = static_cast<double>(total_requests) / RUN_DURATION_SECONDS;

        std::cout << "-------------------- Results --------------------" << std::endl;
        std::cout << "Mode:                     Closed Loop" << std::endl;
        std::cout << "Clients:                  " << NUM_CLIENTS << std::endl;
        std::cout << "Total Requests Completed: " << total_requests << std::endl;
        std::cout << "Test Duration:            " << RUN_DURATION_SECONDS << " seconds" << std::endl;
        std::cout << "Achieved Throughput:      " << throughput_rps << " req/sec" << std::endl;
        std::cout << "Latency (microseconds):" << std::endl;
        std::cout << "  Average: " << avg_us << std::endl;
        std::cout << "  p50: " << p50 << std::endl;
        std::cout << "  p90:          " << p90 << std::endl;
        std::cout << "  p95:          " << p95 << std::endl;
        std::cout << "  p99:          " << p99 << std::endl;
        std::cout << "-------------------------------------------------" << std::endl;
    }

    return 0;
}
