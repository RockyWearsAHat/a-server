#include "emulator/snes/SNESPPU.h"

#include <gtest/gtest.h>

using AIO::Emulator::SNES::SNESPPU;

TEST(SNESPPU, RegisterWriteReadRoundTrip) {
    SNESPPU ppu;

    ppu.WriteReg(0x21, 0xA5);

    EXPECT_EQ(ppu.ReadReg(0x21), 0xA5u);
}

TEST(SNESPPU, TickAdvancesFrameCounter) {
    SNESPPU ppu;

    const uint64_t before = ppu.FrameCount();
    ppu.Tick(341u * 262u);
    const uint64_t after = ppu.FrameCount();

    EXPECT_GT(after, before);
}

TEST(SNESPPU, FramebufferNotNull) {
    SNESPPU ppu;

    ppu.Tick(1000);

    ASSERT_NE(ppu.GetFramebuffer(), nullptr);
}
