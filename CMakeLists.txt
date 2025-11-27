cmake_minimum_required(VERSION 3.16)

project(AIOServer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Find Qt6
set(CMAKE_PREFIX_PATH "/opt/homebrew/opt/qt")
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Add source directory
add_subdirectory(src)

# Testing
enable_testing()
add_subdirectory(tests)
