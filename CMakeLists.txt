cmake_minimum_required(VERSION 3.16)
project(vyn_parser LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find Catch2
find_package(Catch2 3 REQUIRED)
if(NOT Catch2_FOUND)
    message(FATAL_ERROR "Catch2 not found. Please install Catch2 v3.8.1 (e.g., via vcpkg, apt, or brew).")
endif()

# Add include directories
include_directories(${CMAKE_SOURCE_DIR}/src)

# Add executable
add_executable(vyn_parser src/vyn_parser.cpp)

# Link libraries
target_link_libraries(vyn_parser PRIVATE Catch2::Catch2WithMain)

# Enable testing
enable_testing()
add_test(NAME ParserTests COMMAND vyn_parser --test)

# Optional: Installation
# install(TARGETS vyn_parser DESTINATION bin)
