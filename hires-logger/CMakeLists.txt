cmake_minimum_required(VERSION 3.16)

project(hires-logger LANGUAGES CXX)

# Set C++ standard to C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Optional: disable compiler-specific extensions

# Enable generation of compile_commands.json for tooling support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Define the library target
# Add all your .cpp files from the src directory here
add_library(hires_logger src/cycles.cpp) # Add other source files as needed

# Specify include directories for the library
target_include_directories(hires_logger PUBLIC inc)

# Optional: Add compiler flags if needed
# target_compile_options(hires_logger PRIVATE -Wall -Wextra -pedantic)

# Optional: Define TESTING preprocessor definition if needed for testing builds
# target_compile_definitions(hires_logger PRIVATE TESTING=1)
