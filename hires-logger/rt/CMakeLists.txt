cmake_minimum_required(VERSION 3.10)
project(HiResLoggerRT VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Prefer standard features

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Library Target ---
# Create a shared library (libhireslogger.so)
add_library(hireslogger SHARED
    src/rt.cpp
    src/rt_c.cpp
)

# Specify include directories
target_include_directories(hireslogger PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> # For internal includes
    $<INSTALL_INTERFACE:include> # For installation
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../shared>
)

# Link dependencies (pthread for thread_local, rt for clock_gettime)
target_link_libraries(hireslogger PRIVATE pthread rt) # TODO: considering remove it to favor raw RDTSC.

# Set properties for installation and potential use by other CMake projects
set_target_properties(hireslogger PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0 # Major API version
    PUBLIC_HEADER "include/rt.hpp;include/rt_c.h"
)

# --- Installation ---
# Install the shared library, C++ header, C API header, and shared C header
# include(GNUInstallDirs)
# install(TARGETS hireslogger
#     EXPORT ProfilerRtCppTargets
#     LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
#     ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} # For static linking if built static
#     RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} # For Windows DLLs
#     PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hireslogger # Install headers in subdir
# )

# # Install the shared C header separately
# install(FILES ../shared/common.h
#     DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hireslogger # Install alongside library headers
# )

# # Generate and install the export file for CMake consumers
# install(EXPORT ProfilerRtCppTargets
#     FILE ProfilerRtCppTargets.cmake
#     NAMESPACE HiResLoggerRT::
#     DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HiResLoggerRT
# )

# # Generate and install a config file for find_package()
# include(CMakePackageConfigHelpers)
# configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in
#     ${CMAKE_CURRENT_BINARY_DIR}/ProfilerRtCppConfig.cmake
#     INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HiResLoggerRT
#     PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR
# )
# install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ProfilerRtCppConfig.cmake
#     DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HiResLoggerRT
# )


# --- Optional: Build Example/Test ---
# add_executable(example main.cpp)
# target_link_libraries(example PRIVATE profiler_rt)



# ```
# *Self-correction:* Added `Config.cmake.in` generation for better CMake integration. Need to create that file.


# ```cmake
# # ProfilerRtCpp CMake config file
# #
# # This file is configured by CMake and installed into
# # ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/cmake/ProfilerRtCpp
# # It is used by find_package(ProfilerRtCpp)

# @PACKAGE_INIT@

# # Find dependencies (optional, if library itself needs them publicly)
# # find_dependency(Threads REQUIRED)

# # Include the targets file
# include("${CMAKE_CURRENT_LIST_DIR}/ProfilerRtCppTargets.cmake")

# # Check if targets were imported correctly
# check_required_components(ProfilerRtCpp)

# # Set include directory variable for convenience
# set(PROFILERRTCPP_INCLUDE_DIRS "@PACKAGE_CMAKE_INSTALL_INCLUDEDIR@")
# set_and_check(PROFILERRTCPP_INCLUDE_DIR "${PROFILERRTCPP_INCLUDE_DIRS}/profiler_rt") # Specific subdir

# Set library variable (optional, target is preferred)
# find_library(PROFILERRTCPP_LIBRARY NAMES profiler_rt PATHS "@PACKAGE_CMAKE_INSTALL_LIBDIR@")