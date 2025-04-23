#include <iostream>
#include <string>
#include <cstring> // For memset, strlen, strerror
#include <cstdlib> // For exit, atoi
#include <unistd.h> // For read, write, close
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> // For inet_pton
#include <netdb.h> // For host resolution (though not strictly needed for IP)
#include <cerrno> // For errno

#define CLOSE_SOCKET close

const char* HOST = "127.0.0.1"; // Server IP address (localhost)
const int PORT = 65432;         // Server port (must match server)
const int BUFFER_SIZE = 1024;

// Helper function to print errors and exit
void error(const char *msg) {
    perror(msg); // Print error message based on errno (POSIX)
    exit(1);
}

int main() {
    int sockfd;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];
    int n; // For return value of read/write

    // 1. Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        error("ERROR opening socket");
    }
     std::cout << "Socket created successfully." << std::endl;

    // Zero out the server address structure
    memset((char *) &serv_addr, 0, sizeof(serv_addr));

    // Prepare the server address structure
    serv_addr.sin_family = AF_INET; // Address family (IPv4)
    serv_addr.sin_port = htons(PORT); // Server port (network byte order)

    // Convert IPv4 address from text to binary form using POSIX inet_pton
    if (inet_pton(AF_INET, HOST, &serv_addr.sin_addr) <= 0) {
        error("Invalid address/ Address not supported");
    }

    // 2. Connect to the server
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        // Check specific error for connection refused
         if (errno == ECONNREFUSED) {
             std::cerr << "Connection failed. Is the server running on " << HOST << ":" << PORT << "?" << std::endl;
             CLOSE_SOCKET(sockfd); // Close socket before exiting
             exit(1); // Exit cleanly
         } else {
             error("ERROR connecting"); // Handle other connection errors
         }
    }
    std::cout << "Connected to server at " << HOST << ":" << PORT << std::endl;


    // 3. Communication loop (send message, receive echo)
    while (true) {
        std::cout << "Enter message to send (or type 'quit' to exit): ";
        std::string message;
        std::getline(std::cin, message); // Read whole line including spaces

        if (message == "quit") {
            break; // Exit loop if user types quit
        }

        if (message.empty()) {
            continue; // Skip sending empty messages
        }

        // Send the message to the server using POSIX write
        // Use message.c_str() and message.length() for C++ string
        n = write(sockfd, message.c_str(), message.length());
        if (n < 0) {
             error("ERROR writing to socket");
        }
         if (n < message.length()) {
            std::cerr << "WARNING: Not all data written to socket." << std::endl;
            // You might want more robust handling here, like retrying
        }


        // Receive the echo back from the server using POSIX read
        memset(buffer, 0, BUFFER_SIZE); // Clear buffer before reading
        n = read(sockfd, buffer, BUFFER_SIZE - 1); // Read up to BUFFER_SIZE - 1 bytes
        if (n < 0) {
             error("ERROR reading from socket");
        } else if (n == 0) {
             // Server likely closed the connection unexpectedly
             std::cerr << "Server closed connection." << std::endl;
             break;
        }

        // Print the received echo
        // Use std::string constructor that takes length to handle potential null bytes
        std::cout << "Received echo: " << std::string(buffer, n) << std::endl;
    }

    // 4. Close the socket
    CLOSE_SOCKET(sockfd);
    std::cout << "Connection closed." << std::endl;

    return 0;
}
