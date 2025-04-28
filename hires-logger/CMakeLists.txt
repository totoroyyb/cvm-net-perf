cmake_minimum_required(VERSION 3.16)
project(HiResLogger LANGUAGES CXX C)

# Set C++ standard to C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Optional: disable compiler-specific extensions

# Enable generation of compile_commands.json for tooling support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add all your .cpp files from the src directory here
# add_library(hires_logger src/cycles.cpp) # Add other source files as needed

# target_include_directories(hires_logger PUBLIC inc)

# Optional: Add compiler flags if needed
# target_compile_options(hires_logger PRIVATE -Wall -Wextra -pedantic)

# Optional: Define TESTING preprocessor definition if needed for testing builds
# target_compile_definitions(hires_logger PRIVATE TESTING=1)

# --- Runtime Library (rt) ---
add_subdirectory(rt)

# ... existing code ...

# --- Examples ---
option(BUILD_EXAMPLES "Build the example programs" ON) # Define the option with default ON

if(BUILD_EXAMPLES) # Check the option value
    add_executable(producer examples/producer.cpp)
    target_link_libraries(producer PRIVATE hires_rt)
    target_include_directories(producer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/rt/include)

    # Add the consumer example
    add_executable(consumer examples/consumer.cpp)
    target_link_libraries(consumer PRIVATE hires_rt)
    target_include_directories(consumer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/rt/include)

endif()
