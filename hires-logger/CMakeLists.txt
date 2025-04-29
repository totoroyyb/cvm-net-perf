cmake_minimum_required(VERSION 3.16)
project(HiResLogger LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# LTO
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)

# add_library(hires_logger src/cycles.cpp) # Add other source files as needed
# target_include_directories(hires_logger PUBLIC inc)

# target_compile_options(hires_logger PRIVATE -Wall -Wextra -pedantic)

# Optional: Define TESTING preprocessor definition if needed for testing builds
# target_compile_definitions(hires_logger PRIVATE TESTING=1)

# --- Runtime Library (rt) ---
add_subdirectory(rt)

# --- Examples ---
option(BUILD_EXAMPLES "Build the example programs" ON)

if(BUILD_EXAMPLES) 
    add_executable(producer examples/producer.cpp)
    target_link_libraries(producer PRIVATE hires_rt)
    target_include_directories(producer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/rt/include)

    add_executable(consumer examples/consumer.cpp)
    target_link_libraries(consumer PRIVATE hires_rt)
    target_include_directories(consumer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/rt/include)
endif()
