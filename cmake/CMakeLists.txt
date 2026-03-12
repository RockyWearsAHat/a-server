cmake_minimum_required(VERSION 3.16)

# Prevent in-source builds
if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "In-source builds are not allowed. Please create a build directory (e.g., build/) and run CMake from there.")
endif()


project(AIOServer VERSION 0.1.0 LANGUAGES CXX)

# Since CMakeLists.txt is in cmake/, go up one level to project root.
set(PROJECT_ROOT "${CMAKE_SOURCE_DIR}/..")

# Keep all generated binaries and autogen output under the repository-local
# build/ directory regardless of which binary directory a tool configures.
# Some tools configure from build/generated/cmake, others from build/ directly.
# Deriving BUILD_ROOT from CMAKE_BINARY_DIR made the latter case escape to
# ../Desktop, which is not acceptable for this project.
set(AIO_BUILD_ROOT "${PROJECT_ROOT}/build" CACHE PATH
    "Repository-local root for generated binaries, libraries, and autogen output")
get_filename_component(BUILD_ROOT "${AIO_BUILD_ROOT}" ABSOLUTE)

# Ensure all source and header directories are included
include_directories(
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/src/gui
    ${PROJECT_ROOT}/src/input
    /opt/homebrew/opt/sdl2/include
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use ccache if available to speed up incremental builds
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "ccache found: ${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Code coverage option (use -DENABLE_COVERAGE=ON)
option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)
if(ENABLE_COVERAGE OR CMAKE_BUILD_TYPE STREQUAL "Coverage")
  message(STATUS "Code coverage enabled")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --coverage")
endif()

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
find_package(Qt6 REQUIRED COMPONENTS Widgets WebEngineWidgets Network Multimedia MultimediaWidgets)

# Find SDL2
find_package(SDL2 REQUIRED)

# Optional: libcurl (used by YouTube service skeleton)
find_package(CURL REQUIRED)


# Helps clangd/VSCode index the *same* flags CMake builds with.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Centralized build logic
include(core.cmake)
include(tests.cmake)
