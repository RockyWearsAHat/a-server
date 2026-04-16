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

add_executable(QssValidator ${PROJECT_ROOT}/tests/QssValidator.cpp)
set_target_properties(QssValidator PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/QssValidator"
)
target_link_libraries(QssValidator PRIVATE Qt6::Widgets)
add_test(
  NAME QssValidation
  COMMAND ${BUILD_ROOT}/bin/QssValidator ${PROJECT_ROOT}/assets/qss
)
set_tests_properties(QssValidation PROPERTIES TIMEOUT 20)

# ─── PS1 Emulator Tests ────────────────────────────────────────────────

add_executable(PS1CPUTests ${PROJECT_ROOT}/tests/PS1CPUTests.cpp)
set_target_properties(PS1CPUTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/PS1CPUTests"
)
target_link_libraries(PS1CPUTests PRIVATE GTest::gtest_main PS1Emulator)

add_executable(PS1MemoryTests ${PROJECT_ROOT}/tests/PS1MemoryTests.cpp)
set_target_properties(PS1MemoryTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/PS1MemoryTests"
)
target_link_libraries(PS1MemoryTests PRIVATE GTest::gtest_main PS1Emulator)

add_executable(PS1GPUTests ${PROJECT_ROOT}/tests/PS1GPUTests.cpp)
set_target_properties(PS1GPUTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/PS1GPUTests"
)
target_link_libraries(PS1GPUTests PRIVATE GTest::gtest_main PS1Emulator)

add_executable(PS1DMATests ${PROJECT_ROOT}/tests/PS1DMATests.cpp)
set_target_properties(PS1DMATests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/PS1DMATests"
)
target_link_libraries(PS1DMATests PRIVATE GTest::gtest_main PS1Emulator)

add_executable(PS1TimerTests ${PROJECT_ROOT}/tests/PS1TimerTests.cpp)
set_target_properties(PS1TimerTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/PS1TimerTests"
)
target_link_libraries(PS1TimerTests PRIVATE GTest::gtest_main PS1Emulator)

add_executable(PS1SPUTests ${PROJECT_ROOT}/tests/PS1SPUTests.cpp)
set_target_properties(PS1SPUTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/PS1SPUTests"
)
target_link_libraries(PS1SPUTests PRIVATE GTest::gtest_main PS1Emulator)

add_executable(PS1InterruptTests ${PROJECT_ROOT}/tests/PS1InterruptTests.cpp)
set_target_properties(PS1InterruptTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/PS1InterruptTests"
)
target_link_libraries(PS1InterruptTests PRIVATE GTest::gtest_main PS1Emulator)

add_executable(PS1GTETests ${PROJECT_ROOT}/tests/PS1GTETests.cpp)
set_target_properties(PS1GTETests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/PS1GTETests"
)
target_link_libraries(PS1GTETests PRIVATE GTest::gtest_main PS1Emulator)

add_executable(PS1ControllerTests ${PROJECT_ROOT}/tests/PS1ControllerTests.cpp)
set_target_properties(PS1ControllerTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/PS1ControllerTests"
)
target_link_libraries(PS1ControllerTests PRIVATE GTest::gtest_main PS1Emulator)

add_executable(PS1IntegrationTests ${PROJECT_ROOT}/tests/PS1IntegrationTests.cpp)
set_target_properties(PS1IntegrationTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/PS1IntegrationTests"
)
target_link_libraries(PS1IntegrationTests PRIVATE GTest::gtest_main PS1Emulator)


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

# Graphics corruption tests
if(EXISTS "${PROJECT_ROOT}/tests/GraphicsCorruptionTests.cpp")
  add_executable(GraphicsCorruptionTests ${PROJECT_ROOT}/tests/GraphicsCorruptionTests.cpp)
  set_target_properties(GraphicsCorruptionTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/GraphicsCorruptionTests"
  )
  target_link_libraries(GraphicsCorruptionTests PRIVATE GTest::gtest_main GBAEmulator)

  gtest_discover_tests(GraphicsCorruptionTests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TEST_DISCOVERY_TIMEOUT 60
    DISCOVERY_MODE PRE_TEST
  )
endif()

# Audio corruption tests
if(EXISTS "${PROJECT_ROOT}/tests/AudioCorruptionTests.cpp")
  add_executable(AudioCorruptionTests ${PROJECT_ROOT}/tests/AudioCorruptionTests.cpp)
  set_target_properties(AudioCorruptionTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/AudioCorruptionTests"
  )
  target_link_libraries(AudioCorruptionTests PRIVATE GTest::gtest_main GBAEmulator)

  gtest_discover_tests(AudioCorruptionTests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TEST_DISCOVERY_TIMEOUT 60
    DISCOVERY_MODE PRE_TEST
  )
endif()

# DMA timing wait state tests
if(EXISTS "${PROJECT_ROOT}/tests/DMATimingTests.cpp")
  add_executable(DMATimingTests ${PROJECT_ROOT}/tests/DMATimingTests.cpp)
  set_target_properties(DMATimingTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
    AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/DMATimingTests"
  )
  target_link_libraries(DMATimingTests PRIVATE GTest::gtest_main)

  gtest_discover_tests(DMATimingTests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TEST_DISCOVERY_TIMEOUT 60
    DISCOVERY_MODE PRE_TEST
  )
endif()

# ─── PS1 Test Discovery ────────────────────────────────────────────────
gtest_discover_tests(PS1CPUTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)
gtest_discover_tests(PS1MemoryTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)
gtest_discover_tests(PS1GPUTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)
gtest_discover_tests(PS1DMATests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)
gtest_discover_tests(PS1TimerTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)
gtest_discover_tests(PS1SPUTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)
gtest_discover_tests(PS1InterruptTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)
gtest_discover_tests(PS1GTETests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)
gtest_discover_tests(PS1ControllerTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)
gtest_discover_tests(PS1IntegrationTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

# ─── Screen Mirror Tests ───────────────────────────────────────────────

add_executable(ScreenMirrorTests
  ${PROJECT_ROOT}/tests/ScreenMirrorTests.cpp
  ${PROJECT_ROOT}/src/screenmirror/AirPlayReceiver.cpp
  ${PROJECT_ROOT}/src/screenmirror/AirPlayPairing.cpp
  ${PROJECT_ROOT}/src/screenmirror/MirrorSessionManager.cpp
  ${PROJECT_ROOT}/include/screenmirror/AirPlayReceiver.h
  ${PROJECT_ROOT}/include/screenmirror/AirPlayPairing.h
  ${PROJECT_ROOT}/include/screenmirror/AirPlayTlv.h
  ${PROJECT_ROOT}/include/screenmirror/MirrorSessionManager.h
)
set_target_properties(ScreenMirrorTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/ScreenMirrorTests"
)
target_include_directories(ScreenMirrorTests PRIVATE
  ${PROJECT_ROOT}/include
  ${OPENSSL_INCLUDE_DIR}
)
target_link_libraries(ScreenMirrorTests PRIVATE
  GTest::gtest_main
  Qt6::Network
  Qt6::Widgets
  Qt6::Test
  Qt6::Multimedia
  OpenSSL::SSL
  OpenSSL::Crypto
  $<$<BOOL:${APPLE}>:${VIDEOTOOLBOX_FRAMEWORK}>
  $<$<BOOL:${APPLE}>:${COREVIDEO_FRAMEWORK}>
  $<$<BOOL:${APPLE}>:${COREMEDIA_FRAMEWORK}>
)

gtest_discover_tests(ScreenMirrorTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)
# ─── Infrastructure Tests ────────────────────────────────────────────────────

add_executable(InfrastructureTests ${PROJECT_ROOT}/tests/InfrastructureTests.cpp)
set_target_properties(InfrastructureTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/InfrastructureTests"
)
target_link_libraries(InfrastructureTests PRIVATE GTest::gtest_main EmulatorCommon)
gtest_discover_tests(InfrastructureTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

# ─── NES Emulator Tests ─────────────────────────────────────────────────────

add_executable(NESCPUTests ${PROJECT_ROOT}/tests/NESCPUTests.cpp)
set_target_properties(NESCPUTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/NESCPUTests"
)
target_link_libraries(NESCPUTests PRIVATE GTest::gtest_main NESEmulator)
gtest_discover_tests(NESCPUTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(NESPPUTests ${PROJECT_ROOT}/tests/NESPPUTests.cpp)
set_target_properties(NESPPUTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/NESPPUTests"
)
target_link_libraries(NESPPUTests PRIVATE GTest::gtest_main NESEmulator)
gtest_discover_tests(NESPPUTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(NESCartridgeTests ${PROJECT_ROOT}/tests/NESCartridgeTests.cpp)
set_target_properties(NESCartridgeTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/NESCartridgeTests"
)
target_link_libraries(NESCartridgeTests PRIVATE GTest::gtest_main NESEmulator)
gtest_discover_tests(NESCartridgeTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

# ─── Genesis Emulator Tests ───────────────────────────────────────────────

add_executable(GenesisCPUTests ${PROJECT_ROOT}/tests/GenesisCPUTests.cpp)
set_target_properties(GenesisCPUTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/GenesisCPUTests"
)
target_link_libraries(GenesisCPUTests PRIVATE GTest::gtest_main GenesisEmulator)
gtest_discover_tests(GenesisCPUTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(GenesisVDPTests ${PROJECT_ROOT}/tests/GenesisVDPTests.cpp)
set_target_properties(GenesisVDPTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/GenesisVDPTests"
)
target_link_libraries(GenesisVDPTests PRIVATE GTest::gtest_main GenesisEmulator)
gtest_discover_tests(GenesisVDPTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(GenesisCartridgeTests ${PROJECT_ROOT}/tests/GenesisCartridgeTests.cpp)
set_target_properties(GenesisCartridgeTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/GenesisCartridgeTests"
)
target_link_libraries(GenesisCartridgeTests PRIVATE GTest::gtest_main GenesisEmulator)
gtest_discover_tests(GenesisCartridgeTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

# ─── SNES Emulator Tests ──────────────────────────────────────────────────

add_executable(SNESCPUTests ${PROJECT_ROOT}/tests/SNESCPUTests.cpp)
set_target_properties(SNESCPUTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/SNESCPUTests"
)
target_link_libraries(SNESCPUTests PRIVATE GTest::gtest_main SNESEmulator)
gtest_discover_tests(SNESCPUTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(SNESPPUTests ${PROJECT_ROOT}/tests/SNESPPUTests.cpp)
set_target_properties(SNESPPUTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/SNESPPUTests"
)
target_link_libraries(SNESPPUTests PRIVATE GTest::gtest_main SNESEmulator)
gtest_discover_tests(SNESPPUTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(SNESCartridgeTests ${PROJECT_ROOT}/tests/SNESCartridgeTests.cpp)
set_target_properties(SNESCartridgeTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/SNESCartridgeTests"
)
target_link_libraries(SNESCartridgeTests PRIVATE GTest::gtest_main SNESEmulator)
gtest_discover_tests(SNESCartridgeTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

# ─── GB/GBC Emulator Tests ────────────────────────────────────────────────

add_executable(GBCPUTests ${PROJECT_ROOT}/tests/GBCPUTests.cpp)
set_target_properties(GBCPUTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/GBCPUTests"
)
target_link_libraries(GBCPUTests PRIVATE GTest::gtest_main GBEmulator)
gtest_discover_tests(GBCPUTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(GBPPUTests ${PROJECT_ROOT}/tests/GBPPUTests.cpp)
set_target_properties(GBPPUTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/GBPPUTests"
)
target_link_libraries(GBPPUTests PRIVATE GTest::gtest_main GBEmulator)
gtest_discover_tests(GBPPUTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(GBCartridgeTests ${PROJECT_ROOT}/tests/GBCartridgeTests.cpp)
set_target_properties(GBCartridgeTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/GBCartridgeTests"
)
target_link_libraries(GBCartridgeTests PRIVATE GTest::gtest_main GBEmulator)
gtest_discover_tests(GBCartridgeTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

# ─── N64 Emulator Tests ───────────────────────────────────────────────────

add_executable(N64CPUTests ${PROJECT_ROOT}/tests/N64CPUTests.cpp)
set_target_properties(N64CPUTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/N64CPUTests"
)
target_link_libraries(N64CPUTests PRIVATE GTest::gtest_main N64Emulator)
gtest_discover_tests(N64CPUTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(N64RDPTests ${PROJECT_ROOT}/tests/N64RDPTests.cpp)
set_target_properties(N64RDPTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/N64RDPTests"
)
target_link_libraries(N64RDPTests PRIVATE GTest::gtest_main N64Emulator)
gtest_discover_tests(N64RDPTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(N64CartridgeTests ${PROJECT_ROOT}/tests/N64CartridgeTests.cpp)
set_target_properties(N64CartridgeTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/N64CartridgeTests"
)
target_link_libraries(N64CartridgeTests PRIVATE GTest::gtest_main N64Emulator)
gtest_discover_tests(N64CartridgeTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

# ─── Saturn Emulator Tests ────────────────────────────────────────────────

add_executable(SaturnCPUTests ${PROJECT_ROOT}/tests/SaturnCPUTests.cpp)
set_target_properties(SaturnCPUTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/SaturnCPUTests"
)
target_link_libraries(SaturnCPUTests PRIVATE GTest::gtest_main SaturnEmulator)
gtest_discover_tests(SaturnCPUTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(SaturnVDPTests ${PROJECT_ROOT}/tests/SaturnVDPTests.cpp)
set_target_properties(SaturnVDPTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/SaturnVDPTests"
)
target_link_libraries(SaturnVDPTests PRIVATE GTest::gtest_main SaturnEmulator)
gtest_discover_tests(SaturnVDPTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(SaturnMemoryTests ${PROJECT_ROOT}/tests/SaturnMemoryTests.cpp)
set_target_properties(SaturnMemoryTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/SaturnMemoryTests"
)
target_link_libraries(SaturnMemoryTests PRIVATE GTest::gtest_main SaturnEmulator)
gtest_discover_tests(SaturnMemoryTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

# ──────────────────────────── Dreamcast ────────────────────────────
add_executable(DreamcastCPUTests ${PROJECT_ROOT}/tests/DreamcastCPUTests.cpp)
set_target_properties(DreamcastCPUTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/DreamcastCPUTests"
)
target_link_libraries(DreamcastCPUTests PRIVATE GTest::gtest_main DreamcastEmulator)
gtest_discover_tests(DreamcastCPUTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(DreamcastGPUTests ${PROJECT_ROOT}/tests/DreamcastGPUTests.cpp)
set_target_properties(DreamcastGPUTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/DreamcastGPUTests"
)
target_link_libraries(DreamcastGPUTests PRIVATE GTest::gtest_main DreamcastEmulator)
gtest_discover_tests(DreamcastGPUTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(DreamcastMemoryTests ${PROJECT_ROOT}/tests/DreamcastMemoryTests.cpp)
set_target_properties(DreamcastMemoryTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/DreamcastMemoryTests"
)
target_link_libraries(DreamcastMemoryTests PRIVATE GTest::gtest_main DreamcastEmulator)
gtest_discover_tests(DreamcastMemoryTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

# ──────────────────────────── PS2 ────────────────────────────
add_executable(PS2CPUTests ${PROJECT_ROOT}/tests/PS2CPUTests.cpp)
set_target_properties(PS2CPUTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/PS2CPUTests"
)
target_link_libraries(PS2CPUTests PRIVATE GTest::gtest_main PS2Emulator)
gtest_discover_tests(PS2CPUTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(PS2GSTests ${PROJECT_ROOT}/tests/PS2GSTests.cpp)
set_target_properties(PS2GSTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/PS2GSTests"
)
target_link_libraries(PS2GSTests PRIVATE GTest::gtest_main PS2Emulator)
gtest_discover_tests(PS2GSTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(PS2MemoryTests ${PROJECT_ROOT}/tests/PS2MemoryTests.cpp)
set_target_properties(PS2MemoryTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/PS2MemoryTests"
)
target_link_libraries(PS2MemoryTests PRIVATE GTest::gtest_main PS2Emulator)
gtest_discover_tests(PS2MemoryTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(GekkoTests ${PROJECT_ROOT}/tests/GekkoTests.cpp)
set_target_properties(GekkoTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/GekkoTests"
)
target_link_libraries(GekkoTests PRIVATE GTest::gtest_main GameCubeEmulator)
gtest_discover_tests(GekkoTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(FlipperTests ${PROJECT_ROOT}/tests/FlipperTests.cpp)
set_target_properties(FlipperTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/FlipperTests"
)
target_link_libraries(FlipperTests PRIVATE GTest::gtest_main GameCubeEmulator)
gtest_discover_tests(FlipperTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)

add_executable(GameCubeMemoryTests ${PROJECT_ROOT}/tests/GameCubeMemoryTests.cpp)
set_target_properties(GameCubeMemoryTests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${BUILD_ROOT}/bin
  AUTOGEN_BUILD_DIR "${BUILD_ROOT}/generated/autogen/GameCubeMemoryTests"
)
target_link_libraries(GameCubeMemoryTests PRIVATE GTest::gtest_main GameCubeEmulator)
gtest_discover_tests(GameCubeMemoryTests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  TEST_DISCOVERY_TIMEOUT 60
  DISCOVERY_MODE PRE_TEST
)
