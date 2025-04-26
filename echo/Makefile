CXX = g++

# Compiler flags
# -Wall: Enable all warnings
# -std=c++11: Use the C++11 standard (or newer if needed, e.g., c++17)
# -o: Specifies the output file name (used in rules)
CXXFLAGS = -Wall -std=c++17

# Source files
SERVER_SRC = echo_server.cpp
CLIENT_SRC = echo_client.cpp
SERVER_ASYNC_SRC = echo_server_async.cpp
CLIENT_ASYNC_SRC = echo_client_async.cpp
CLIENT_CLOSED_SRC = echo_client_closed.cpp
CLIENT_OPEN_SRC = echo_client_open.cpp

# Build directory
BUILD_DIR = build

# Executable names
SERVER_EXEC = $(BUILD_DIR)/echo_server
SERVER_EXEC_ASYNC = $(BUILD_DIR)/echo_server_async
CLIENT_EXEC = $(BUILD_DIR)/echo_client
CLIENT_EXEC_ASYNC = $(BUILD_DIR)/echo_client_async
CLIENT_EXEC_CLOSED = $(BUILD_DIR)/echo_client_closed
CLIENT_EXEC_OPEN = $(BUILD_DIR)/echo_client_open


all: $(BUILD_DIR) $(SERVER_EXEC) $(CLIENT_EXEC) $(SERVER_EXEC_ASYNC) $(CLIENT_EXEC_ASYNC) $(CLIENT_EXEC_CLOSED) $(CLIENT_EXEC_OPEN)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)
	@echo "Created build directory: $(BUILD_DIR)"

# Rule to build the server executable
$(SERVER_EXEC): $(SERVER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SERVER_SRC) -o $(SERVER_EXEC)
	@echo "Built $(SERVER_EXEC) successfully."

# Rule to build the client executable
$(CLIENT_EXEC): $(CLIENT_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CLIENT_SRC) -o $(CLIENT_EXEC)
	@echo "Built $(CLIENT_EXEC) successfully."

$(SERVER_EXEC_ASYNC): $(SERVER_ASYNC_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SERVER_ASYNC_SRC) -o $(SERVER_EXEC_ASYNC)
	@echo "Built $(SERVER_EXEC_ASYNC) successfully."

# Rule to build the async client executable
$(CLIENT_EXEC_ASYNC): $(CLIENT_ASYNC_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CLIENT_ASYNC_SRC) -o $(CLIENT_EXEC_ASYNC)
	@echo "Built $(CLIENT_EXEC_ASYNC) successfully."

# Rule to build the closed client executable
$(CLIENT_EXEC_CLOSED): $(CLIENT_CLOSED_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CLIENT_CLOSED_SRC) -o $(CLIENT_EXEC_CLOSED)
	@echo "Built $(CLIENT_EXEC_CLOSED) successfully."

# Rule to build the open client executable
$(CLIENT_EXEC_OPEN): $(CLIENT_OPEN_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CLIENT_OPEN_SRC) -o $(CLIENT_EXEC_OPEN)
	@echo "Built $(CLIENT_EXEC_OPEN) successfully."

clean:
	rm -rf $(BUILD_DIR) *.o

.PHONY: all clean $(BUILD_DIR)
