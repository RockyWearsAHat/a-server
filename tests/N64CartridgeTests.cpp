#include <gtest/gtest.h>
#include "emulator/n64/N64.h"
#include <cstdlib>
#include <cstdio>

using namespace N64Emulator;

class N64CartridgeTests : public ::testing::Test {
 protected:
  N64 system;

  std::string CreateTestROM(size_t size) {
    const char* tmp = std::getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";
    std::string path = std::string(tmp) + "/test_n64_rom_XXXXXX";
    int fd = mkstemp(const_cast<char*>(path.c_str()));
    EXPECT_GE(fd, 0);

    std::vector<uint8_t> rom(size, 0x00);

    // Insert valid .z64 magic header
    if (rom.size() >= 4) {
      rom[0] = 0x80; rom[1] = 0x37; rom[2] = 0x12; rom[3] = 0x40;
    }
    // Entry point at 0x08 (big-endian): 0x80001000
    if (rom.size() > 0x0B) {
      rom[0x08] = 0x80; rom[0x09] = 0x00; rom[0x0A] = 0x10; rom[0x0B] = 0x00;
    }

    auto written = write(fd, rom.data(), size);
    (void)written;
    close(fd);
    return path;
  }
};

TEST_F(N64CartridgeTests, LoadAndRead32RoundTrip) {
  std::string path = CreateTestROM(0x100000);  // 1 MB
  EXPECT_NO_THROW(system.Load(path));

  // Entry point should be decoded from header
  EXPECT_EQ(system.GetCartridge()->GetEntryPoint(), 0x80001000U);

  std::remove(path.c_str());
}

TEST_F(N64CartridgeTests, SmallRomThrows) {
  // ROM below 1 MB minimum should throw
  const char* tmp = std::getenv("TMPDIR");
  if (!tmp) tmp = "/tmp";
  std::string path = std::string(tmp) + "/n64_small_XXXXXX";
  int fd = mkstemp(const_cast<char*>(path.c_str()));
  std::vector<uint8_t> rom(0x1000, 0x00);  // 4 KB — too small
  auto written = write(fd, rom.data(), rom.size());
  (void)written;
  close(fd);

  EXPECT_THROW(system.Load(path), std::runtime_error);
  std::remove(path.c_str());
}

TEST_F(N64CartridgeTests, SaveStateRoundTrip) {
  std::string path = CreateTestROM(0x100000);
  system.Load(path);

  N64::State state = system.SaveState();
  N64 system2;
  system2.LoadState(state);

  EXPECT_EQ(system2.GetCartridge()->GetEntryPoint(),
            system.GetCartridge()->GetEntryPoint());

  std::remove(path.c_str());
}
