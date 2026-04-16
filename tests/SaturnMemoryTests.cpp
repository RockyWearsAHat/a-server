#include <gtest/gtest.h>
#include "emulator/saturn/Saturn.h"

using namespace SaturnEmulator;

class SaturnMemoryTests : public ::testing::Test {
 protected:
  Saturn system;
};

TEST_F(SaturnMemoryTests, WorkRamLowWriteRead) {
  system.Reset();
  // Work RAM-L: 0x00200000
  system.GetMemory()->Write8(0x00200000, 0x42);
  EXPECT_EQ(system.GetMemory()->Read8(0x00200000), 0x42U);
}

TEST_F(SaturnMemoryTests, WorkRamHighWrite32Read32) {
  system.Reset();
  // Work RAM-H: 0x06000000
  system.GetMemory()->Write32(0x06000000, 0xDEADBEEFU);
  EXPECT_EQ(system.GetMemory()->Read32(0x06000000), 0xDEADBEEFU);
}

TEST_F(SaturnMemoryTests, MemorySaveStateRoundTrip) {
  system.Reset();
  system.GetMemory()->Write8(0x06000010, 0x99);

  Saturn::State state = system.SaveState();
  Saturn system2;
  system2.LoadState(state);

  EXPECT_EQ(system2.GetMemory()->Read8(0x06000010), 0x99U);
}
