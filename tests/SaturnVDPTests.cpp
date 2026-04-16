#include <gtest/gtest.h>
#include "emulator/saturn/Saturn.h"

using namespace SaturnEmulator;

class SaturnVDPTests : public ::testing::Test {
 protected:
  Saturn system;
};

TEST_F(SaturnVDPTests, VDP1FramebufferNotNull) {
  system.Reset();
  EXPECT_NE(system.GetFramebuffer(), nullptr);
}

TEST_F(SaturnVDPTests, VDP2RegWriteReadRoundTrip) {
  system.Reset();
  // Write to VDP2 register at offset 0 and read it back
  system.GetVDP2()->WriteReg(0, 0xABCD);
  EXPECT_EQ(system.GetVDP2()->ReadReg(0), 0xABCDU);
}

TEST_F(SaturnVDPTests, FrameCounterSaveStateRoundTrip) {
  system.Reset();
  system.GetVDP2()->IncrementFrame();
  system.GetVDP2()->IncrementFrame();
  uint32_t expected = system.GetFrameCount();

  Saturn::State state = system.SaveState();
  Saturn system2;
  system2.LoadState(state);

  EXPECT_EQ(system2.GetFrameCount(), expected);
}
