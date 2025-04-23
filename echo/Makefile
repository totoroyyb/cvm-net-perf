CXX = g++

# Compiler flags
# -Wall: Enable all warnings
# -std=c++11: Use the C++11 standard (or newer if needed, e.g., c++17)
# -o: Specifies the output file name (used in rules)
CXXFLAGS = -Wall -std=c++17

# Source files
SERVER_SRC = echo_server.cpp
CLIENT_SRC = echo_client.cpp

# Build directory
BUILD_DIR = build

# Executable names
SERVER_EXEC = $(BUILD_DIR)/echo_server
CLIENT_EXEC = $(BUILD_DIR)/echo_client


all: $(BUILD_DIR) $(SERVER_EXEC) $(CLIENT_EXEC)

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

clean:
	rm -rf $(BUILD_DIR) *.o

.PHONY: all clean $(BUILD_DIR)
