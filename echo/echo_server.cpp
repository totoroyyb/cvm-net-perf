#include <iostream>
#include <string>
#include <cstring> // For memset, strerror
#include <cstdlib> // For exit, atoi
#include <unistd.h> // For read, write, close
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> // For inet_ntop
#include <cerrno> // For errno

#define CLOSE_SOCKET close

const int PORT = 65432;
const int BUFFER_SIZE = 1024;

// Helper function to print errors and exit
void error(const char *msg) {
    perror(msg); // Print error message based on errno (POSIX)
    exit(1);
}

int main() {
    int sockfd, newsockfd;
    socklen_t clilen;
    char buffer[BUFFER_SIZE];
    struct sockaddr_in serv_addr, cli_addr;
    int n; // For return value of read/write

    // 1. Create socket
    // AF_INET: IPv4, SOCK_STREAM: TCP, 0: Default protocol (TCP)
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        error("ERROR opening socket");
    }
    std::cout << "Socket created successfully." << std::endl;

    // Allow socket descriptor to be reusable (optional but good practice)
    int enable = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
         error("setsockopt(SO_REUSEADDR) failed");
    }

    // Zero out the server address structure
    memset((char *) &serv_addr, 0, sizeof(serv_addr));

    // 2. Bind socket to an address and port
    serv_addr.sin_family = AF_INET;         // Address family (IPv4)
    serv_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any local address
    serv_addr.sin_port = htons(PORT);       // Port number (convert to network byte order)

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        error("ERROR on binding");
    }
    std::cout << "Binding successful on port " << PORT << "." << std::endl;

    // 3. Listen for incoming connections
    // The second argument is the backlog queue size (max pending connections)
    if (listen(sockfd, 5) < 0) {
         error("ERROR on listen");
    }
    std::cout << "Server listening for connections..." << std::endl;

    clilen = sizeof(cli_addr);

    // 4. Accept a connection
    // This is a blocking call, waits until a client connects
    newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
    if (newsockfd < 0) {
        error("ERROR on accept");
    }

    // Get client IP address and port for logging
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cli_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(cli_addr.sin_port);
    std::cout << "Connection accepted from " << client_ip << ":" << client_port << std::endl;

    // 5. Communication loop (read and echo back)
    while (true) {
        memset(buffer, 0, BUFFER_SIZE); // Clear the buffer

        // Read data from the client socket using POSIX read
        n = read(newsockfd, buffer, BUFFER_SIZE - 1); // Leave space for null terminator

        if (n < 0) {
            // Handle read error
            error("ERROR reading from socket");
        } else if (n == 0) {
            // Connection closed by client
            std::cout << "Client " << client_ip << ":" << client_port << " disconnected." << std::endl;
            break; // Exit the loop
        }

        // Print received data (as raw bytes converted to string)
        std::cout << "Received from client: " << std::string(buffer, n) << std::endl;

        // Write (echo) the data back to the client using POSIX write
        n = write(newsockfd, buffer, n); // Send back exactly n bytes received
        if (n < 0) {
            // Handle write error
            error("ERROR writing to socket");
        }
         std::cout << "Echoed " << n << " bytes back to client." << std::endl;
    }

    // 6. Close sockets
    CLOSE_SOCKET(newsockfd); // Close the connection socket
    CLOSE_SOCKET(sockfd);    // Close the listening socket

    std::cout << "Server shutting down." << std::endl;

    return 0;
}
