include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(CPUTests CPUTests.cpp)
target_link_libraries(CPUTests PRIVATE GTest::gtest_main GBAEmulator)

add_executable(HeadlessTest HeadlessTest.cpp)
target_link_libraries(HeadlessTest PRIVATE GBAEmulator)

include(GoogleTest)
gtest_discover_tests(CPUTests)
