# GoogleTest setup (relative, all generated files to ../build/generated)
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Set GoogleTest output directories before making it available
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_INIT "${BUILD_ROOT}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_INIT "${BUILD_ROOT}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_INIT "${BUILD_ROOT}/bin")

FetchContent_MakeAvailable(googletest)

# Force GoogleTest to use our output directories
set_target_properties(gtest gtest_main gmock gmock_main PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${BUILD_ROOT}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${BUILD_ROOT}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${BUILD_ROOT}/bin"
)

enable_testing()


# Test discovery files stay in CMAKE_BINARY_DIR (build/generated/cmake/)
set(CMAKE_TEST_GEN_DIR "${CMAKE_BINARY_DIR}")
file(MAKE_DIRECTORY ${CMAKE_TEST_GEN_DIR})


add_executable(CPUTests ${PROJECT_ROOT}/tests/CPUTests.cpp)
set_target_properties(CPUTests PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/CPUTests"
)
target_link_libraries(CPUTests PRIVATE GTest::gtest_main GBAEmulator)

add_executable(EEPROMTests ${PROJECT_ROOT}/tests/EEPROMTests.cpp)
set_target_properties(EEPROMTests PROPERTIES 
    RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/EEPROMTests"
)
target_link_libraries(EEPROMTests PRIVATE GTest::gtest_main GBAEmulator)

include(GoogleTest)
gtest_discover_tests(CPUTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
  TEST_INCLUDE_FILE ${CMAKE_TEST_GEN_DIR}/CPUTests_include.cmake
  TEST_LIST ${CMAKE_TEST_GEN_DIR}/CPUTests_tests.cmake
)
gtest_discover_tests(EEPROMTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
  TEST_INCLUDE_FILE ${CMAKE_TEST_GEN_DIR}/EEPROMTests_include.cmake
  TEST_LIST ${CMAKE_TEST_GEN_DIR}/EEPROMTests_tests.cmake
)
