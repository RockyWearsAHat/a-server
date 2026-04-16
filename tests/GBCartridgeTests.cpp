#include <gtest/gtest.h>
#include "emulator/gb/GB.h"
#include <cstdio>
#include <cstdlib>

using namespace GBEmulator;

class GBCartridgeTests : public ::testing::Test {
 protected:
  GB system;

  // Helper: create a temporary test ROM file
  std::string CreateTestROM(size_t size) {
    const char* temp_dir = std::getenv("TMPDIR");
    if (!temp_dir) temp_dir = "/tmp";
    
    std::string rom_path = std::string(temp_dir) + "/test_gb_rom_XXXXXX";
    
    // Create a temporary file
    int fd = mkstemp(const_cast<char*>(rom_path.c_str()));
    if (fd < 0) {
      throw std::runtime_error("Failed to create temp ROM");
    }

    // Write dummy ROM data (minimum valid structure)
    std::vector<uint8_t> rom_data(size, 0x00);
    // Set cartridge type at 0x0147 to Simple (no MBC)
    if (rom_data.size() > 0x0147) {
      rom_data[0x0147] = 0x00;
    }
    // Set RAM size at 0x0149
    if (rom_data.size() > 0x0149) {
      rom_data[0x0149] = 0x02;  // 8 KB
    }

    if (write(fd, rom_data.data(), size) != static_cast<ssize_t>(size)) {
      close(fd);
      throw std::runtime_error("Failed to write temp ROM");
    }

    close(fd);
    return rom_path;
  }
};

TEST_F(GBCartridgeTests, LoadAndRead8RoundTrip) {
  // Create a 32 KB ROM (minimum size)
  std::string rom_path = CreateTestROM(0x8000);

  EXPECT_NO_THROW(system.Load(rom_path));

  // Read from ROM should succeed
  uint8_t value = system.GetCartridge()->Read8(0x0000);
  EXPECT_EQ(value, 0x00);

  std::remove(rom_path.c_str());
}

TEST_F(GBCartridgeTests, SRAMWritePersistsThroughSaveState) {
  std::string rom_path = CreateTestROM(0x8000);
  system.Load(rom_path);

  // Enable cartridge RAM
  system.GetMemory()->Write8(0x0000, 0x0A);

  // Write to cartridge RAM
  system.GetMemory()->Write8(0xA000, 0x42);

  // Save state
  GB::State state = system.SaveState();

  // Create a new system and load state
  GB system2;
  system2.GetMemory()->Init(system2.GetCartridge(), system2.GetPPU(), system2.GetAPU());
  system2.LoadState(state);

  // RAM should persist
  EXPECT_EQ(system2.GetMemory()->Read8(0xA000), 0x42);

  std::remove(rom_path.c_str());
}

TEST_F(GBCartridgeTests, SmallRomThrows) {
  // Create a 1 KB ROM (too small)
  std::string rom_path = CreateTestROM(0x400);

  EXPECT_THROW(system.Load(rom_path), std::runtime_error);

  std::remove(rom_path.c_str());
}
