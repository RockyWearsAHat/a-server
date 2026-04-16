#include <gtest/gtest.h>
#include "emulator/n64/N64.h"

using namespace N64Emulator;

class N64CPUTests : public ::testing::Test {
 protected:
  N64 system;
};

TEST_F(N64CPUTests, ResetLoadsBootAddress) {
  system.Reset();
  // After reset, PC should be at the MIPS boot vector (KSEG1: 0xBFC00000)
  // As signed 64-bit: 0xFFFFFFFFBFC00000
  EXPECT_EQ(system.GetCPU()->GetPC(), 0xFFFFFFFF80000000ULL);
}

TEST_F(N64CPUTests, StepAdvancesProgramCounter) {
  system.Reset();
  uint64_t initial_pc = system.GetCPU()->GetPC();

  system.Step();

  // After one Step(), PC should have advanced by 4 bytes
  EXPECT_EQ(system.GetCPU()->GetPC(), initial_pc + 4);
}

TEST_F(N64CPUTests, RunFrameAdvancesFrameCounter) {
  system.Reset();
  uint32_t initial_frame = system.GetFrameCount();

  system.RunFrame();

  EXPECT_GT(system.GetFrameCount(), initial_frame);
}
