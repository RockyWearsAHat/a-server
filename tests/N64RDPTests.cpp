#include <gtest/gtest.h>
#include "emulator/n64/N64.h"

using namespace N64Emulator;

class N64RDPTests : public ::testing::Test {
 protected:
  N64 system;
};

TEST_F(N64RDPTests, FramebufferNotNull) {
  system.Reset();
  EXPECT_NE(system.GetFramebuffer(), nullptr);
}

TEST_F(N64RDPTests, SyncFullCommandIncrementsFrameCount) {
  system.Reset();
  uint32_t initial_frame = system.GetRDP()->GetFrameCount();

  // Send SYNC_FULL command (0x27 in bits 61:56)
  uint64_t sync_full = static_cast<uint64_t>(0x27) << 56;
  system.GetRDP()->ProcessCommand(sync_full);

  EXPECT_EQ(system.GetRDP()->GetFrameCount(), initial_frame + 1);
}

TEST_F(N64RDPTests, FrameCounterSaveStateRoundTrip) {
  system.Reset();

  // Increment frame count
  system.GetRDP()->IncrementFrame();
  system.GetRDP()->IncrementFrame();
  uint32_t expected_frame = system.GetRDP()->GetFrameCount();

  // Save and reload state
  N64::State state = system.SaveState();
  N64 system2;
  system2.LoadState(state);

  EXPECT_EQ(system2.GetRDP()->GetFrameCount(), expected_frame);
}
