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
#include <arpa/inet.h> // For inet_pton
#include <netdb.h> // For host resolution (though not strictly needed for IP)
#include <cerrno> // For errno
#include <atomic> // For atomic flag
#include <vector> // For storing latencies
#include <numeric> // For accumulate (optional)
#include <algorithm> // For sort, percentile calculation

#define CLOSE_SOCKET close

const char* HOST = "127.0.0.1"; // Server IP address (localhost)
const int PORT = 65432;         // Server port (must match server)
const int BUFFER_SIZE = 1024;
const int NUM_CLIENTS = 5;      // Number of concurrent client threads
const double ARRIVAL_RATE_HZ = 10.0; // Target requests per second per client
const int RUN_DURATION_SECONDS = 10; // How long the test should run

// Global flag to signal threads to stop
std::atomic<bool> keep_running(true);

// Helper function to print errors and exit (modified for threads)
void error(const char *msg, int thread_id = -1) {
    char error_buf[256];
    snprintf(error_buf, sizeof(error_buf), "Thread %d: ERROR %s", thread_id, msg);
    perror(error_buf); // Print error message based on errno (POSIX)
    // In a multithreaded scenario, exiting the whole process might not be ideal.
    // Consider throwing an exception or signaling failure. For simplicity, we still exit here.
    if (thread_id != -1) {
         std::cerr << "Thread " << thread_id << " exiting due to error." << std::endl;
         // std::terminate(); // Or use a thread-specific exit mechanism if needed
    }
    exit(1); // Exit the entire process on critical error
}


// Function executed by each client thread
void client_thread_func(int thread_id, std::atomic<bool>& running_flag, std::vector<std::chrono::microseconds>& latencies) {
    int sockfd;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    int n;
    const std::chrono::microseconds inter_arrival_time(
        static_cast<long long>(1'000'000.0 / ARRIVAL_RATE_HZ) // Calculate interval in microseconds
    );
    latencies.reserve(static_cast<size_t>(ARRIVAL_RATE_HZ * RUN_DURATION_SECONDS * 1.2)); // Pre-allocate approximate space

    // 1. Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        error("opening socket", thread_id);
    }
    // std::cout << "Thread " << thread_id << ": Socket created." << std::endl; // Optional: Verbose logging

    // Zero out the server address structure
    memset((char *) &serv_addr, 0, sizeof(serv_addr));

    // Prepare the server address structure
    serv_addr.sin_family = AF_INET; // Address family (IPv4)
    serv_addr.sin_port = htons(PORT); // Server port (network byte order)

    // Convert IPv4 address from text to binary form using POSIX inet_pton
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
             // error(error_msg, thread_id); // error() exits process, maybe just log and return
             perror(error_msg);
             std::cerr << "Thread " << thread_id << " failed to connect." << std::endl;
         }
         CLOSE_SOCKET(sockfd);
         return; // Exit this thread if connection fails
    }
    // std::cout << "Thread " << thread_id << ": Connected to server at " << HOST << ":" << PORT << std::endl; // Less verbose during run


    // 3. Communication loop (close-loop with rate limiting and duration check)
    std::string message_base = "Hello from client thread " + std::to_string(thread_id) + " msg: ";
    long long msg_count = 0;

    while (running_flag.load()) { // Check the flag before starting a new request
        auto start_time = std::chrono::high_resolution_clock::now();

        std::string message = message_base + std::to_string(msg_count++);

        // Send the message
        n = write(sockfd, message.c_str(), message.length());
        if (n < 0) {
             // Handle specific errors like EPIPE (broken pipe)
             if (errno == EPIPE) {
                 std::cerr << "Thread " << thread_id << ": Server closed connection (Broken pipe)." << std::endl;
             } else {
                 // error("writing to socket", thread_id); // Avoid process exit
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

        // Receive the echo back from the server using POSIX read
        memset(buffer, 0, BUFFER_SIZE); // Clear buffer before reading
        n = read(sockfd, buffer, BUFFER_SIZE - 1); // Read up to BUFFER_SIZE - 1 bytes
        if (n < 0) {
             // error("reading from socket", thread_id); // Avoid process exit
             char error_msg[100];
             snprintf(error_msg, sizeof(error_msg), "Thread %d: ERROR reading from socket", thread_id);
             perror(error_msg);
             break; // Exit loop on error
        } else if (n == 0) {
             // Server closed the connection gracefully
             std::cerr << "Thread " << thread_id << ": Server closed connection." << std::endl;
             break; // Exit loop
        }

        // Calculate and record latency
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        latencies.push_back(duration);

        // Sleep to maintain arrival rate
        if (duration < inter_arrival_time) {
            // Check flag again before sleeping, in case duration expired during request
            if (!running_flag.load()) break;
            std::this_thread::sleep_for(inter_arrival_time - duration);
        }
        // else: If processing took longer than inter_arrival_time, send the next request immediately if still running
    }

    // 4. Close the socket
    CLOSE_SOCKET(sockfd);
    // std::cout << "Thread " << thread_id << ": Connection closed." << std::endl; // Less verbose
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
    std::cout << "Starting " << NUM_CLIENTS << " client threads..." << std::endl;
    if (ARRIVAL_RATE_HZ <= 0) {
         std::cerr << "Error: ARRIVAL_RATE_HZ must be positive." << std::endl;
         return 1;
    }
    std::cout << "Target arrival rate per client: " << ARRIVAL_RATE_HZ << " Hz" << std::endl;
    std::cout << "Running for " << RUN_DURATION_SECONDS << " seconds." << std::endl;


    std::vector<std::thread> client_threads;
    std::vector<std::vector<std::chrono::microseconds>> all_latencies(NUM_CLIENTS); // Store latencies per thread
    client_threads.reserve(NUM_CLIENTS);

    // Launch threads
    for (int i = 0; i < NUM_CLIENTS; ++i) {
        all_latencies[i].clear(); // Ensure vector is empty before passing
        client_threads.emplace_back(client_thread_func, i, std::ref(keep_running), std::ref(all_latencies[i]));
         // Small delay between starting threads to potentially avoid overwhelming
         // the server's accept queue or causing connection storms.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "All client threads launched. Running workload for " << RUN_DURATION_SECONDS << " seconds..." << std::endl;

    // Wait for the specified duration
    std::this_thread::sleep_for(std::chrono::seconds(RUN_DURATION_SECONDS));

    // Signal threads to stop
    std::cout << "Time limit reached. Signaling threads to stop..." << std::endl;
    keep_running.store(false);

    // Join threads (wait for them to finish their current request and exit)
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
        // Sort latencies for percentile calculation
        std::sort(combined_latencies.begin(), combined_latencies.end());

        // Calculate percentiles (in microseconds)
        long long p50 = calculate_percentile(combined_latencies, 50.0);
        long long p90 = calculate_percentile(combined_latencies, 90.0);
        long long p95 = calculate_percentile(combined_latencies, 95.0);
        long long p99 = calculate_percentile(combined_latencies, 99.0);

        // Calculate average latency (optional)
        long long sum_us = 0;
        for(const auto& lat : combined_latencies) {
            sum_us += lat.count();
        }
        double avg_us = static_cast<double>(sum_us) / combined_latencies.size();


        std::cout << "-------------------- Results --------------------" << std::endl;
        std::cout << "Total Requests Completed: " << total_requests << std::endl;
        std::cout << "Latency (microseconds):" << std::endl;
        std::cout << "  Average: " << avg_us << std::endl;
        std::cout << "  p50 (Median): " << p50 << std::endl;
        std::cout << "  p90:          " << p90 << std::endl;
        std::cout << "  p95:          " << p95 << std::endl;
        std::cout << "  p99:          " << p99 << std::endl;
        std::cout << "-------------------------------------------------" << std::endl;
    }


    return 0;
}
