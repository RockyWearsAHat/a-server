cmake_minimum_required(VERSION 3.16)

# Prevent in-source builds
if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "In-source builds are not allowed. Please create a build directory (e.g., build/) and run CMake from there.")
endif()


project(AIOServer VERSION 0.1.0 LANGUAGES CXX)

# Since CMakeLists.txt is in cmake/, go up one level to project root
set(PROJECT_ROOT "${CMAKE_SOURCE_DIR}/..")

# BUILD_ROOT is two levels up from CMAKE_BINARY_DIR (build/generated/cmake -> build)
get_filename_component(BUILD_ROOT "${CMAKE_BINARY_DIR}/../.." ABSOLUTE)

# Ensure all source and header directories are included
include_directories(
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src/gui
    ${PROJECT_ROOT}/src/input
    /opt/homebrew/opt/sdl2/include
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# Centralize all Qt/CMake-generated files
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
# Place all _autogen folders in build/generated/autogen/
foreach(dir AUTOGEN_BUILD MOC_OUTPUT UIC_OUTPUT RCC_OUTPUT)
    set(CMAKE_${dir}_DIRECTORY "${BUILD_ROOT}/generated/autogen")
endforeach()
set(CMAKE_AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen")
set(CMAKE_AUTOGEN_OUTPUT_DIR "${BUILD_ROOT}/generated/autogen")

# Output directories (use BUILD_ROOT which is build/, not build/generated/cmake/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BUILD_ROOT}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${BUILD_ROOT}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${BUILD_ROOT}/lib")

# Also set the per-configuration output directories to prevent duplicates
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${BUILD_ROOT}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${BUILD_ROOT}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${BUILD_ROOT}/lib")
endforeach()

# Find Qt6
set(CMAKE_PREFIX_PATH "/opt/homebrew/opt/qt")
find_package(Qt6 REQUIRED COMPONENTS Widgets WebEngineWidgets Network)

# Find SDL2
find_package(SDL2 REQUIRED)

# Optional: libcurl (used by YouTube service skeleton)
find_package(CURL REQUIRED)


# Centralized build logic
include(core.cmake)
include(tests.cmake)
