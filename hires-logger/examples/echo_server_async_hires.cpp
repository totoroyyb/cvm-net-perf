#include <arpa/inet.h> // For inet_ntop
#include <cerrno>      // For errno
#include <chrono>      // Required for high_resolution_clock
#include <cstdint>
#include <cstdlib> // For exit, atoi
#include <cstring> // For memset, strerror
#include <ctime>   // For std::time_t, std::tm, std::localtime_r
#include <fcntl.h> // For fcntl (non-blocking sockets)
#include <iostream>
#include <netinet/in.h>
#include <sys/epoll.h> // For epoll
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>   // For std::thread
#include <unistd.h> // For read, write, close
#include <vector>   // For epoll_event array and thread vector

#include "rt.hpp"

#define CLOSE_SOCKET close
#define MAX_EVENTS 64
#define NUM_WORKER_THREADS 5

const int PORT = 65432;
const int BUFFER_SIZE = 1024;

void error(const char *msg) {
  perror(msg); // Print error message based on errno (POSIX)
  exit(1);
}

bool set_nonblocking(int sockfd) {
  int flags = fcntl(sockfd, F_GETFL, 0);
  if (flags == -1) {
    perror("fcntl F_GETFL");
    return false;
  }
  if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl F_SETFL O_NONBLOCK");
    return false;
  }
  return true;
}

// Worker thread function containing the event loop
void worker_loop(int epollfd, int sockfd, HiResLogger::HiResConn &hires_conn) {
  struct epoll_event events[MAX_EVENTS];
  char buffer[BUFFER_SIZE]; // Each thread needs its own buffer
  socklen_t clilen;
  struct sockaddr_in cli_addr;

  while (true) {
    // Wait indefinitely
    int n_events = epoll_wait(epollfd, events, MAX_EVENTS, -1);
    if (n_events == -1) {
      if (errno == EINTR) {
        continue;
      }
      // Log error but let the thread continue if possible, or exit based on
      // severity
      perror("WARN: epoll_wait error in worker");
      std::this_thread::sleep_for(
          std::chrono::milliseconds(100)); // Avoid busy-looping on error
      continue;
      // error("FATAL: epoll_wait error"); // Alternative: exit on error
    }

    for (int i = 0; i < n_events; ++i) {
      // Check for errors first (recommended with EPOLLET)
      if (events[i].events & (EPOLLERR | EPOLLHUP)) {
        int error_fd = events[i].data.fd;
        std::cerr << "Epoll error/hangup on fd " << error_fd << " in thread "
                  << std::this_thread::get_id() << std::endl;
        // Error handling might try to remove from epoll, but another thread
        // might too. Simple close is often sufficient as kernel cleans up epoll
        // entries on close. epoll_ctl(epollfd, EPOLL_CTL_DEL, error_fd, NULL);
        // // Optional explicit removal
        CLOSE_SOCKET(error_fd); // Close the socket
        continue;               // Skip further processing for this event
      }

      if (events[i].data.fd == sockfd) {
        // --- New connection on listening socket ---
        // EPOLLET requires us to accept until EAGAIN
        while (true) {
          clilen = sizeof(cli_addr);
          int newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
          if (newsockfd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              // No more incoming connections for now
              break;
            } else {
              perror("ERROR on accept");
              break; // Error accepting
            }
          }

          // Get client info and log
          char client_ip[INET_ADDRSTRLEN];
          inet_ntop(AF_INET, &cli_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
          int client_port = ntohs(cli_addr.sin_port);
          // std::cout << "Thread " << std::this_thread::get_id() << ":
          // Connection accepted from " << client_ip << ":" << client_port << "
          // on fd " << newsockfd << std::endl;

          // Make the new socket non-blocking
          if (!set_nonblocking(newsockfd)) {
            close(newsockfd);
            continue; // Skip adding if failed
          }

          // Add the new socket to epoll (EPOLLIN + EPOLLET)
          struct epoll_event event;
          event.events = EPOLLIN | EPOLLET; // Monitor for read, edge-triggered
          event.data.fd = newsockfd;
          if (epoll_ctl(epollfd, EPOLL_CTL_ADD, newsockfd, &event) == -1) {
            perror("ERROR adding client socket to epoll");
            close(newsockfd);
          }
        }
      } else if (events[i].events & EPOLLIN) {
        int client_fd = events[i].data.fd;
        while (true) {
          uint64_t receive_start = HiResLogger::rdtscp(NULL);
          memset(buffer, 0, BUFFER_SIZE);
          int n = read(client_fd, buffer, BUFFER_SIZE - 1);

          if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              break;
            } else {
              perror("ERROR reading from socket");
              CLOSE_SOCKET(client_fd);
              std::cerr << "Thread " << std::this_thread::get_id()
                        << ": Closed connection on fd " << client_fd
                        << " due to read error." << std::endl;
              break;
            }
          } else if (n == 0) {
            std::cout << "Thread " << std::this_thread::get_id()
                      << ": Client on fd " << client_fd << " disconnected."
                      << std::endl;
            CLOSE_SOCKET(client_fd);
            break;
          } else {
            uint64_t receive_end = HiResLogger::rdtscp(NULL);
            // Log the read event
            hires_conn.log(1, receive_end - receive_start, 0);

            // Write data back (simple blocking write for now)
            // TODO: Implement non-blocking write with EPOLLET/EPOLLOUT for
            // production
            uint64_t send_sum = 0;
            int total_written = 0;
            while (total_written < n) {
              uint64_t send_start = HiResLogger::rdtscp(NULL);
              int bytes_written =
                  write(client_fd, buffer + total_written, n - total_written);
              if (bytes_written < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                  // Write buffer full, should ideally register for EPOLLOUT
                  std::cerr << "Thread " << std::this_thread::get_id()
                            << ": Write would block on fd " << client_fd
                            << ". Waiting briefly (simple echo)." << std::endl;
                  send_sum += HiResLogger::rdtscp(NULL) - send_start;
                  std::this_thread::sleep_for(std::chrono::milliseconds(1));
                  // Simple backoff, NOT ideal
                } else {
                  perror("ERROR writing to socket");
                  // epoll_ctl(epollfd, EPOLL_CTL_DEL, client_fd, NULL); //
                  // Optional removal
                  CLOSE_SOCKET(client_fd);
                  std::cerr << "Thread " << std::this_thread::get_id()
                            << ": Closed connection on fd " << client_fd
                            << " due to write error." << std::endl;
                  goto next_event; // Exit write loop and outer read loop for
                                   // this fd
                }
              } else if (bytes_written == 0) {
                std::cerr << "Thread " << std::this_thread::get_id()
                          << ": Wrote 0 bytes to fd " << client_fd
                          << ". Closing." << std::endl;
                CLOSE_SOCKET(client_fd);
                goto next_event;
              } else {
                send_sum += HiResLogger::rdtscp(NULL) - send_start;
                total_written += bytes_written;
              }
            }

            hires_conn.log(2, send_sum, 0); // Log the write event
          }
        } // End of read loop for this client_fd
      } // End of handling client socket event
    next_event:; // Label to jump to for error handling within loops
    } // End of loop through events
  } // End of main while(true) loop in worker
}

int main() {
  HiResLogger::HiResConn connection = HiResLogger::HiResConn();

  int sockfd;

  // 1. Create socket
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    error("ERROR opening socket");
  }
  std::cout << "Socket created successfully." << std::endl;

  // Set listening socket to non-blocking
  if (!set_nonblocking(sockfd)) {
    close(sockfd);
    exit(1);
  }

  // Allow socket descriptor to be reusable
  int enable = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
    error("setsockopt(SO_REUSEADDR) failed");
  }

  // Zero out the server address structure
  struct sockaddr_in serv_addr; // Define here for bind/listen
  memset((char *)&serv_addr, 0, sizeof(serv_addr));

  // 2. Bind socket
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(PORT);

  if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    error("ERROR on binding");
  }
  std::cout << "Binding successful on port " << PORT << "." << std::endl;

  // 3. Listen
  if (listen(sockfd, SOMAXCONN) < 0) { // Use SOMAXCONN for backlog
    error("ERROR on listen");
  }
  std::cout << "Server listening for connections..." << std::endl;

  // 4. Create epoll instance
  int epollfd = epoll_create1(0);
  if (epollfd == -1) {
    error("ERROR creating epoll instance");
  }

  struct epoll_event event;
  event.events = EPOLLIN | EPOLLET; // Monitor for read events, edge-triggered
  event.data.fd = sockfd;           // Associate event with listening socket

  // 5. Add listening socket to epoll
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &event) == -1) {
    error("ERROR adding listening socket to epoll");
  }

  // 6. Create and launch worker threads
  std::vector<std::thread> threads;
  std::cout << "Launching " << NUM_WORKER_THREADS << " worker threads..."
            << std::endl;
  for (int i = 0; i < NUM_WORKER_THREADS; ++i) {
    threads.emplace_back(worker_loop, epollfd, sockfd, std::ref(connection));
  }

  // 7. Join worker threads (will block indefinitely in this example)
  std::cout << "Main thread waiting for worker threads to join..." << std::endl;
  for (auto &t : threads) {
    if (t.joinable()) {
      t.join();
    }
  }

  // 8. Close sockets (Technically unreachable in this infinite loop)
  CLOSE_SOCKET(sockfd); // Close the listening socket
  close(epollfd);       // Close epoll instance

  std::cout << "Server shutting down." << std::endl; // Unreachable

  return 0;
}
