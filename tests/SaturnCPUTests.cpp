#include <gtest/gtest.h>
#include "emulator/saturn/Saturn.h"

using namespace SaturnEmulator;

class SaturnCPUTests : public ::testing::Test {
 protected:
  Saturn system;
};

TEST_F(SaturnCPUTests, ResetSetsValidPC) {
  system.Reset();
  // After reset, master SH-2 PC should be non-zero (Work RAM-H scaffold entry)
  EXPECT_NE(system.GetMasterSH2()->GetPC(), 0U);
}

TEST_F(SaturnCPUTests, StepAdvancesProgramCounter) {
  system.Reset();
  uint32_t initial_pc = system.GetMasterSH2()->GetPC();

  system.Step();

  // PC should have advanced by 2 bytes (SH-2 has 16-bit fixed-length instructions)
  EXPECT_EQ(system.GetMasterSH2()->GetPC(), initial_pc + 2);
}

TEST_F(SaturnCPUTests, RunFrameAdvancesFrameCounter) {
  system.Reset();
  uint32_t initial_frame = system.GetFrameCount();

  system.RunFrame();

  EXPECT_GT(system.GetFrameCount(), initial_frame);
}
