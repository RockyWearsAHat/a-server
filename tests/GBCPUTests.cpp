#include <gtest/gtest.h>
#include "emulator/gb/GB.h"

using namespace GBEmulator;

class GBCPUTests : public ::testing::Test {
 protected:
  GB system;
};

TEST_F(GBCPUTests, ResetLoadsStartAddress) {
  system.Reset();
  EXPECT_EQ(system.GetCPU()->GetPC(), 0x0100);
}

TEST_F(GBCPUTests, StepAdvancesProgramCounter) {
  system.Reset();
  uint16_t initial_pc = system.GetCPU()->GetPC();

  // Execute a NOP (0x00)
  system.Step();

  // PC should have advanced (NOP is 1 byte)
  EXPECT_GT(system.GetCPU()->GetPC(), initial_pc);
}

TEST_F(GBCPUTests, RunFrameAdvancesPPUFrameCounter) {
  system.Reset();
  uint32_t initial_frame = system.GetFrameCount();

  system.RunFrame();

  EXPECT_GT(system.GetFrameCount(), initial_frame);
}
