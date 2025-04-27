# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread
LDFLAGS = -lstdc++fs # Link against the filesystem library

# Build directory
BUILD_DIR = build

# Target executable name
TARGET_NAME = dummy_writer
TARGET = $(BUILD_DIR)/$(TARGET_NAME)

# Source files
SRCS = dummy_writer.cpp

# Object files (derived from source files, placed in build dir)
OBJS = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SRCS))

# Default target
all: $(TARGET)

# Rule to link the executable
$(TARGET): $(OBJS) | $(BUILD_DIR) # Add build dir as order-only prerequisite
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

# Rule to compile source files into object files in the build directory
$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR) # Add build dir as order-only prerequisite
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Rule to create the build directory
$(BUILD_DIR):
	mkdir -p $@

# Clean rule
clean:
	rm -rf $(BUILD_DIR) # Remove the entire build directory

# Phony targets
.PHONY: all clean
