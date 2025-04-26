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
#include <deque> // For storing pending request start times
#include <fcntl.h> // For fcntl (non-blocking socket)
#include <mutex>   // For std::mutex
#include <condition_variable> // Potentially useful, but sticking to atomics/mutex for now
#include <unordered_map> // For request ID mapping

#define CLOSE_SOCKET close

// --- Configuration ---
const char* HOST = "127.0.0.1"; // Server IP address (localhost)
const int PORT = 65432;         // Server port (must match server)
const int BUFFER_SIZE = 1024;
const int NUM_CLIENTS = 5;      // Number of concurrent client threads
const double ARRIVAL_RATE_HZ = 1000.0; // Target requests per second per client (Primary Tuning Parameter)
const int RUN_DURATION_SECONDS = 10; // How long the test should run
// --- End Configuration ---

// Global flag to signal threads to stop
std::atomic<bool> keep_running(true);

// Shared data structure for pending requests between send/receive threads
struct PendingRequestData {
    // Map Request ID (msg_count) to its send time
    std::unordered_map<long long, std::chrono::time_point<std::chrono::high_resolution_clock>> times;
    std::mutex mtx; // Mutex to protect the map
};


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


// --- Send Loop ---
void send_loop(int sockfd, int thread_id, PendingRequestData& pending_requests,
               const std::chrono::microseconds inter_arrival_time,
               std::atomic<bool>& connection_active, const std::atomic<bool>& global_running_flag)
{
    std::string message_base = "Hello from client thread " + std::to_string(thread_id) + " msg: ";
    long long msg_count = 0;
    auto next_send_time = std::chrono::high_resolution_clock::now();
    int n;

    while (global_running_flag.load() && connection_active.load()) {
        auto current_time = std::chrono::high_resolution_clock::now();

        if (current_time >= next_send_time) {
            // Include msg_count in the message
            std::string message = message_base + std::to_string(msg_count);
            auto send_start_time = current_time; // Record time just before write attempt
            long long current_msg_id = msg_count; // Capture ID before potential increment

            n = write(sockfd, message.c_str(), message.length());

            if (n >= 0) { // Successful write (or partial write)
                if (n < message.length()) {
                    std::cerr << "Thread " << thread_id << ": WARNING: Partial write occurred for msg " << current_msg_id << "." << std::endl;
                    // Simplistic handling: Assume sent for latency tracking
                }
                { // Lock scope
                    std::lock_guard<std::mutex> lock(pending_requests.mtx);
                    // Store the timestamp associated with this specific message ID
                    pending_requests.times[current_msg_id] = send_start_time;
                }
                msg_count++; // Increment only after successful send attempt
                next_send_time += inter_arrival_time; // Schedule the next send

            } else { // n < 0
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Send buffer full. Yield/sleep briefly to avoid busy-wait.
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                } else if (errno == EPIPE) {
                    std::cerr << "Thread " << thread_id << ": Send loop detected Broken pipe." << std::endl;
                    connection_active.store(false); // Signal receiver to stop
                    break;
                } else {
                    char error_msg[100];
                    snprintf(error_msg, sizeof(error_msg), "Thread %d: ERROR writing to socket", thread_id);
                    perror(error_msg);
                    connection_active.store(false); // Signal receiver to stop
                    break;
                }
            }
        } else {
            // Not time to send yet, yield/sleep briefly
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    }
     // std::cout << "Thread " << thread_id << ": Send loop exiting." << std::endl;
}

// Helper function to parse request ID from response (simple implementation)
long long parse_request_id(const char* buffer, int length) {
    // Assumes ID is the last part after the last space
    const char* last_space = nullptr;
    for (int i = length - 1; i >= 0; --i) {
        if (buffer[i] == ' ') {
            last_space = buffer + i;
            break;
        }
    }

    if (last_space && (last_space + 1 < buffer + length)) {
        try {
            return std::stoll(last_space + 1);
        } catch (const std::exception& e) {
            // Handle conversion error (e.g., non-numeric data)
            std::cerr << "WARNING: Could not parse request ID from response part: '" << (last_space + 1) << "'" << std::endl;
            return -1; // Indicate failure
        }
    }
    std::cerr << "WARNING: Could not find space before request ID in response: '" << std::string(buffer, length) << "'" << std::endl;
    return -1; // Indicate failure
}


// --- Receive Loop ---
void receive_loop(int sockfd, int thread_id, PendingRequestData& pending_requests,
                  std::vector<std::chrono::microseconds>& latencies,
                  std::atomic<bool>& connection_active, const std::atomic<bool>& global_running_flag)
{
    char buffer[BUFFER_SIZE];
    int n;

    while (global_running_flag.load() && connection_active.load()) {
        memset(buffer, 0, BUFFER_SIZE);
        // Note: Non-blocking read might only read a partial message.
        // A robust implementation would buffer reads until a complete message is received.
        // For simplicity here, we assume read() gets the full response or fails.
        n = read(sockfd, buffer, BUFFER_SIZE - 1);

        if (n > 0) { // Data received
            auto end_time = std::chrono::high_resolution_clock::now();
            long long request_id = parse_request_id(buffer, n);

            if (request_id != -1) { // Successfully parsed an ID
                std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
                bool found_pending = false;
                { // Lock scope
                    std::lock_guard<std::mutex> lock(pending_requests.mtx);
                    auto it = pending_requests.times.find(request_id);
                    if (it != pending_requests.times.end()) {
                        start_time = it->second; // Get the start time
                        pending_requests.times.erase(it); // Remove the entry
                        found_pending = true;
                    }
                } // Mutex released

                if (found_pending) {
                    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
                    latencies.push_back(duration);
                } else {
                    // This can happen if the response arrives after a timeout,
                    // or if the server sends unexpected/duplicate IDs.
                    std::cerr << "Thread " << thread_id << ": WARNING: Received response for ID " << request_id << " which was not pending or already processed." << std::endl;
                }
            } else {
                 std::cerr << "Thread " << thread_id << ": WARNING: Failed to parse request ID from received data." << std::endl;
                 // Handle case where parsing failed - data might be corrupted or not follow expected format.
            }
        } else if (n == 0) { // Connection closed by peer
            // std::cerr << "Thread " << thread_id << ": Server closed connection (read returned 0)." << std::endl;
            connection_active.store(false); // Signal sender to stop
            break;
        } else { // n < 0
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available, yield/sleep briefly before trying again
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            } else { // Actual read error
                char error_msg[100];
                snprintf(error_msg, sizeof(error_msg), "Thread %d: ERROR reading from socket", thread_id);
                perror(error_msg);
                connection_active.store(false); // Signal sender to stop
                break;
            }
        }
    }
     // std::cout << "Thread " << thread_id << ": Receive loop exiting." << std::endl;
}


// Function executed by each client thread (Manages connection and launches send/receive loops)
void client_connection_handler(int thread_id, std::atomic<bool>& global_running_flag, std::vector<std::chrono::microseconds>& latencies) {
    int sockfd;
    struct sockaddr_in serv_addr;
    const std::chrono::microseconds inter_arrival_time(
        static_cast<long long>(1'000'000.0 / ARRIVAL_RATE_HZ)
    );
    if (inter_arrival_time.count() <= 0 && ARRIVAL_RATE_HZ > 0) {
        std::cerr << "Warning: Calculated inter_arrival_time is zero or negative. Rate might be too high." << std::endl;
    }

    latencies.reserve(static_cast<size_t>(ARRIVAL_RATE_HZ * RUN_DURATION_SECONDS * 1.5));

    // Shared data for this connection's send/receive threads
    PendingRequestData pending_requests_data;
    std::atomic<bool> connection_active(true); // Flag specific to this connection

    // 1. Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error("opening socket", thread_id);

    // Set TCP_NODELAY
    int flag = 1;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int)) < 0) {
        char error_msg[100];
        snprintf(error_msg, sizeof(error_msg), "Thread %d: WARNING setsockopt(TCP_NODELAY) failed", thread_id);
        perror(error_msg);
    }

    // Set socket to non-blocking
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) error("fcntl F_GETFL", thread_id);
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) error("fcntl F_SETFL O_NONBLOCK", thread_id);

    // Prepare the server address structure
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, HOST, &serv_addr.sin_addr) <= 0) error("Invalid address/ Address not supported", thread_id);

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
         return; // Cannot proceed if connection failed
    }

    // 3. Launch Send and Receive Threads
    std::thread sender_thread(send_loop, sockfd, thread_id, std::ref(pending_requests_data),
                              inter_arrival_time, std::ref(connection_active), std::ref(global_running_flag));

    std::thread receiver_thread(receive_loop, sockfd, thread_id, std::ref(pending_requests_data),
                                std::ref(latencies), std::ref(connection_active), std::ref(global_running_flag));

    // 4. Wait for threads to complete
    // They will exit either when global_running_flag becomes false or connection_active becomes false
    if (sender_thread.joinable()) {
        sender_thread.join();
    }
    if (receiver_thread.joinable()) {
        receiver_thread.join();
    }

    // 5. Close the socket
    CLOSE_SOCKET(sockfd);

    size_t pending_count = 0;
    {
        std::lock_guard<std::mutex> lock(pending_requests_data.mtx);
        // Check the size of the map for pending requests
        pending_count = pending_requests_data.times.size();
    }
    if (pending_count > 0) {
         std::cerr << "Thread " << thread_id << ": " << pending_count << " requests still pending in map at exit." << std::endl;
    }
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
    // No changes needed in main itself, just ensure the server echoes the ID.
    std::cout << "Starting Open-Loop Test..." << std::endl;
    if (ARRIVAL_RATE_HZ <= 0) {
         std::cerr << "Error: ARRIVAL_RATE_HZ must be positive for open-loop mode." << std::endl;
         return 1;
    }
    std::cout << "Number of client threads: " << NUM_CLIENTS << std::endl;
    std::cout << "Target arrival rate per client: " << ARRIVAL_RATE_HZ << " Hz" << std::endl;
    std::cout << "Running for " << RUN_DURATION_SECONDS << " seconds." << std::endl;
    std::cout << "Target Server: " << HOST << ":" << PORT << std::endl;


    std::vector<std::thread> client_threads;
    std::vector<std::vector<std::chrono::microseconds>> all_latencies(NUM_CLIENTS);
    client_threads.reserve(NUM_CLIENTS);

    // Launch threads (each managing a connection and its send/receive pair)
    for (int i = 0; i < NUM_CLIENTS; ++i) {
        all_latencies[i].clear();
        // Launch the connection handler, which internally creates send/receive threads
        client_threads.emplace_back(client_connection_handler, i, std::ref(keep_running), std::ref(all_latencies[i]));
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Small startup delay
    }

    std::cout << "All client threads launched. Running workload..." << std::endl;

    // Wait for the specified duration
    std::this_thread::sleep_for(std::chrono::seconds(RUN_DURATION_SECONDS));

    // Signal threads to stop
    std::cout << "Time limit reached. Signaling threads to stop..." << std::endl;
    keep_running.store(false); // Global flag signals all connection handlers

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
    size_t total_requests = 0; // Note: This counts *completed* requests (received responses)
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
        // Throughput is based on completed requests within the duration
        double throughput_rps = static_cast<double>(total_requests) / RUN_DURATION_SECONDS;

        std::cout << "-------------------- Results --------------------" << std::endl;
        std::cout << "Mode:                     Open Loop (2 Threads/Client, ID Matching)" << std::endl; // Updated Mode description
        std::cout << "Clients:                  " << NUM_CLIENTS << std::endl;
        std::cout << "Target Rate (per client): " << ARRIVAL_RATE_HZ << " Hz" << std::endl;
        std::cout << "Target Rate (total):      " << ARRIVAL_RATE_HZ * NUM_CLIENTS << " Hz" << std::endl;
        std::cout << "Total Requests Completed: " << total_requests << std::endl;
        std::cout << "Test Duration:            " << RUN_DURATION_SECONDS << " seconds" << std::endl;
        std::cout << "Achieved Throughput:      " << throughput_rps << " req/sec" << std::endl;
        std::cout << "Latency (microseconds) for completed requests:" << std::endl;
        std::cout << "  Average: " << avg_us << std::endl;
        std::cout << "  p50: " << p50 << std::endl;
        std::cout << "  p90:          " << p90 << std::endl;
        std::cout << "  p95:          " << p95 << std::endl;
        std::cout << "  p99:          " << p99 << std::endl;
        std::cout << "-------------------------------------------------" << std::endl;
    }

    return 0;
}
