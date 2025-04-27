cmake_minimum_required(VERSION 3.10)
project(ProfilerRtCpp VERSION 0.1 LANGUAGES CXX)

# Require C++11 for std::atomic, thread_local etc.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Prefer standard features

# --- Library Target ---
# Create a shared library (libprofiler_rt.so)
add_library(profiler_rt SHARED
    src/profiler_rt.cpp
    src/profiler_c_api.cpp
)

# Specify include directories
target_include_directories(profiler_rt PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> # For internal includes
    $<INSTALL_INTERFACE:include> # For installation
    # Also need access to the shared C header
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../shared_include>
)

# Link dependencies (pthread for thread_local, rt for clock_gettime)
target_link_libraries(profiler_rt PRIVATE pthread rt)

# Set properties for installation and potential use by other CMake projects
set_target_properties(profiler_rt PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0 # Major API version
    PUBLIC_HEADER "include/profiler_rt.hpp;include/profiler_c_api.h"
    # Add definition for shared header path if needed by consumers? Usually not.
)

# --- Installation ---
# Install the shared library, C++ header, C API header, and shared C header
include(GNUInstallDirs)
install(TARGETS profiler_rt
    EXPORT ProfilerRtCppTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} # For static linking if built static
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} # For Windows DLLs
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/profiler_rt # Install headers in subdir
)

# Install the shared C header separately
install(FILES ../shared_include/shared_profiler_data.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/profiler_rt # Install alongside library headers
)

# Generate and install the export file for CMake consumers
install(EXPORT ProfilerRtCppTargets
    FILE ProfilerRtCppTargets.cmake
    NAMESPACE ProfilerRtCpp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ProfilerRtCpp
)

# Generate and install a config file for find_package()
include(CMakePackageConfigHelpers)
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ProfilerRtCppConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ProfilerRtCpp
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ProfilerRtCppConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ProfilerRtCpp
)


# --- Optional: Build Example/Test ---
# add_executable(example main.cpp)
# target_link_libraries(example PRIVATE profiler_rt)

```
*Self-correction:* Added `Config.cmake.in` generation for better CMake integration. Need to create that file.


```cmake
# ProfilerRtCpp CMake config file
#
# This file is configured by CMake and installed into
# ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/cmake/ProfilerRtCpp
# It is used by find_package(ProfilerRtCpp)

@PACKAGE_INIT@

# Find dependencies (optional, if library itself needs them publicly)
# find_dependency(Threads REQUIRED)

# Include the targets file
include("${CMAKE_CURRENT_LIST_DIR}/ProfilerRtCppTargets.cmake")

# Check if targets were imported correctly
check_required_components(ProfilerRtCpp)

# Set include directory variable for convenience
set(PROFILERRTCPP_INCLUDE_DIRS "@PACKAGE_CMAKE_INSTALL_INCLUDEDIR@")
set_and_check(PROFILERRTCPP_INCLUDE_DIR "${PROFILERRTCPP_INCLUDE_DIRS}/profiler_rt") # Specific subdir

# Set library variable (optional, target is preferred)
# find_library(PROFILERRTCPP_LIBRARY NAMES profiler_rt PATHS "@PACKAGE_CMAKE_INSTALL_LIBDIR@")