#include <gtest/gtest.h>
#include "emulator/gb/GB.h"

using namespace GBEmulator;

class GBPPUTests : public ::testing::Test {
 protected:
  GB system;
};

TEST_F(GBPPUTests, RegisterWriteReadRoundTrip) {
  system.Reset();
  GBPPU* ppu = system.GetPPU();

  // Write to LCDC (0xFF40)
  ppu->WriteReg(0x00, 0x91);
  EXPECT_EQ(ppu->ReadReg(0x00), 0x91);

  // Write to BGP (0xFF47)
  ppu->WriteReg(0x07, 0xFC);
  EXPECT_EQ(ppu->ReadReg(0x07), 0xFC);
}

TEST_F(GBPPUTests, TickAdvancesFrameCounter) {
  system.Reset();
  uint32_t initial_frame = system.GetPPU()->GetFrameCount();

  // Tick PPU enough times to complete a frame
  // 154 lines * 456 cycles/line = 70,224 cycles per frame
  for (int i = 0; i < 70224; ++i) {
    system.GetPPU()->Tick();
  }

  EXPECT_GT(system.GetPPU()->GetFrameCount(), initial_frame);
}

TEST_F(GBPPUTests, FramebufferNotNull) {
  system.Reset();
  const uint32_t* framebuffer = system.GetFramebuffer();

  EXPECT_NE(framebuffer, nullptr);
  // Framebuffer is 160x144 pixels
  EXPECT_EQ(framebuffer[0], 0xFFFFFFFFU);  // Scaffold gradient color
}
