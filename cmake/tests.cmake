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

add_executable(DMATests ${PROJECT_ROOT}/tests/DMATests.cpp)
set_target_properties(DMATests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/DMATests"
)
target_link_libraries(DMATests PRIVATE GTest::gtest_main GBAEmulator)

add_executable(BIOSTests ${PROJECT_ROOT}/tests/BIOSTests.cpp)
set_target_properties(BIOSTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/BIOSTests"
)
target_link_libraries(BIOSTests PRIVATE GTest::gtest_main GBAEmulator)

add_executable(ROMMetadataTests ${PROJECT_ROOT}/tests/ROMMetadataTests.cpp)
set_target_properties(ROMMetadataTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/ROMMetadataTests"
)
target_link_libraries(ROMMetadataTests PRIVATE GTest::gtest_main GBAEmulator)

add_executable(InputLogicTests ${PROJECT_ROOT}/tests/InputLogicTests.cpp)
set_target_properties(InputLogicTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/InputLogicTests"
)
target_link_libraries(InputLogicTests PRIVATE GTest::gtest_main)

add_executable(PPUTests ${PROJECT_ROOT}/tests/PPUTests.cpp)
set_target_properties(PPUTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/PPUTests"
)
target_link_libraries(PPUTests PRIVATE GTest::gtest_main GBAEmulator)

add_executable(MemoryMapTests ${PROJECT_ROOT}/tests/MemoryMapTests.cpp)
set_target_properties(MemoryMapTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/MemoryMapTests"
)
target_link_libraries(MemoryMapTests PRIVATE GTest::gtest_main GBAEmulator)

add_executable(InterruptTests ${PROJECT_ROOT}/tests/InterruptTests.cpp)
set_target_properties(InterruptTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/InterruptTests"
)
target_link_libraries(InterruptTests PRIVATE GTest::gtest_main GBAEmulator)

add_executable(BlendTests ${PROJECT_ROOT}/tests/BlendTests.cpp)
set_target_properties(BlendTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/BlendTests"
)
target_link_libraries(BlendTests PRIVATE GTest::gtest_main GBAEmulator)

add_executable(WindowTests ${PROJECT_ROOT}/tests/WindowTests.cpp)
set_target_properties(WindowTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/WindowTests"
)
target_link_libraries(WindowTests PRIVATE GTest::gtest_main GBAEmulator)

add_executable(TimerTests ${PROJECT_ROOT}/tests/TimerTests.cpp)
set_target_properties(TimerTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/TimerTests"
)
target_link_libraries(TimerTests PRIVATE GTest::gtest_main GBAEmulator)

add_executable(AffineBgTests ${PROJECT_ROOT}/tests/AffineBgTests.cpp)
set_target_properties(AffineBgTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/AffineBgTests"
)
target_link_libraries(AffineBgTests PRIVATE GTest::gtest_main GBAEmulator)

add_executable(DMAEdgeTests ${PROJECT_ROOT}/tests/DMAEdgeTests.cpp)
set_target_properties(DMAEdgeTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/DMAEdgeTests"
)
target_link_libraries(DMAEdgeTests PRIVATE GTest::gtest_main GBAEmulator)

add_executable(DisplayInterruptTests ${PROJECT_ROOT}/tests/DisplayInterruptTests.cpp)
set_target_properties(DisplayInterruptTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/DisplayInterruptTests"
)
target_link_libraries(DisplayInterruptTests PRIVATE GTest::gtest_main GBAEmulator)

add_executable(APUTests ${PROJECT_ROOT}/tests/APUTests.cpp)
set_target_properties(APUTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/APUTests"
)
target_link_libraries(APUTests PRIVATE GTest::gtest_main GBAEmulator)


# Headless SMA2 investigation harness (not a unit test).
add_executable(SMA2Harness ${PROJECT_ROOT}/test_sma2_10s.cpp)
set_target_properties(SMA2Harness PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/SMA2Harness"
)
target_link_libraries(SMA2Harness PRIVATE GBAEmulator)

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

gtest_discover_tests(DMATests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

gtest_discover_tests(BIOSTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

gtest_discover_tests(ROMMetadataTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

gtest_discover_tests(InputLogicTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

gtest_discover_tests(PPUTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

gtest_discover_tests(MemoryMapTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

gtest_discover_tests(InterruptTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

gtest_discover_tests(BlendTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

gtest_discover_tests(WindowTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

gtest_discover_tests(TimerTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

gtest_discover_tests(AffineBgTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

gtest_discover_tests(DMAEdgeTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

gtest_discover_tests(DisplayInterruptTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

gtest_discover_tests(APUTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)
