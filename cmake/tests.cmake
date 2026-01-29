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

# GBA timing/light smoke tests
add_executable(GbaTests ${PROJECT_ROOT}/tests/GbaTimingTests.cpp)
set_target_properties(GbaTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/GbaTests"
)
target_link_libraries(GbaTests PRIVATE GTest::gtest_main GBAEmulator)

if(EXISTS "${CMAKE_BINARY_DIR}/bin/GbaTests")
  add_test(NAME GbaTests COMMAND "${CMAKE_BINARY_DIR}/bin/GbaTests" --gtest_filter="*")
  set_tests_properties(GbaTests PROPERTIES TIMEOUT 60)
else()
  # Fall back to discovery if GoogleTest discovery is available later
endif()

if(EXISTS "${PROJECT_ROOT}/tests/APUTests.cpp")
  add_executable(APUTests ${PROJECT_ROOT}/tests/APUTests.cpp)
  set_target_properties(APUTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/APUTests"
  )
  target_link_libraries(APUTests PRIVATE GTest::gtest_main GBAEmulator)

  include(GoogleTest)
  if(EXISTS "${CMAKE_BINARY_DIR}/bin/APUTests")
    gtest_discover_tests(APUTests
      TEST_EXECUTABLE "${CMAKE_BINARY_DIR}/bin/APUTests"
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      TEST_DISCOVERY_TIMEOUT 60
      DISCOVERY_MODE PRE_TEST
    )
  else()
    # Fall back to default discovery logic (CMake will try to infer path)
    gtest_discover_tests(APUTests
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      TEST_DISCOVERY_TIMEOUT 60
      DISCOVERY_MODE PRE_TEST
    )
  endif()

  # As a robust fallback (handles weird output dir resolution), add a simple
  # CTest entry that runs the binary directly if it exists in the build bin
  # directory. This ensures CTest will list/run the tests even when discovery
  # plumbing has trouble.
  if(EXISTS "${CMAKE_BINARY_DIR}/bin/APUTests")
    add_test(NAME APUTests COMMAND "${CMAKE_BINARY_DIR}/bin/APUTests" --gtest_filter="*")
    set_tests_properties(APUTests PROPERTIES TIMEOUT 60)
  endif()
else()
  message(WARNING "tests/APUTests.cpp not found; skipping APUTests target.")
endif()

# Headless SMA2 investigation harness (not a unit test).
if(EXISTS "${PROJECT_ROOT}/test_sma2_10s.cpp")
  add_executable(SMA2Harness ${PROJECT_ROOT}/test_sma2_10s.cpp)
  set_target_properties(SMA2Harness PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/SMA2Harness"
  )
  target_link_libraries(SMA2Harness PRIVATE GBAEmulator)
else()
  message(WARNING "test_sma2_10s.cpp not found; skipping SMA2Harness target.")
endif()

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

# Logger tests (documentation-driven)
add_executable(LoggerTests ${PROJECT_ROOT}/tests/LoggerTests.cpp)
set_target_properties(LoggerTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/LoggerTests"
)
target_link_libraries(LoggerTests PRIVATE GTest::gtest_main GBAEmulator)

gtest_discover_tests(LoggerTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)
# GBA integration tests (full coverage of GBA class)
add_executable(GBAIntegrationTests ${PROJECT_ROOT}/tests/GBATests.cpp)
set_target_properties(GBAIntegrationTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/GBAIntegrationTests"
)
target_link_libraries(GBAIntegrationTests PRIVATE GTest::gtest_main GBAEmulator)

gtest_discover_tests(GBAIntegrationTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)