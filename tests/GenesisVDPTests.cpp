#include "emulator/genesis/GenesisVDP.h"
#include <gtest/gtest.h>

using AIO::Emulator::Genesis::GenesisVDP;

TEST(GenesisVDP, DataPortRoundTripVRAM) {
    GenesisVDP vdp;

    // Set VRAM address 0x0000, write one word.
    vdp.WriteCtrl(0x4000);
    vdp.WriteCtrl(0x0000);
    vdp.WriteData(0xABCD);

    // Reset address and read back.
    vdp.WriteCtrl(0x0000);
    vdp.WriteCtrl(0x0000);
    const uint16_t value = vdp.ReadData();

    EXPECT_EQ(value, 0xABCDu);
}

TEST(GenesisVDP, TickAdvancesFrameCounter) {
    GenesisVDP vdp;

    const uint64_t before = vdp.FrameCount();
    // Enough cycles for a full NTSC frame at scaffold timing.
    vdp.Tick(488u * 262u);
    const uint64_t after = vdp.FrameCount();

    EXPECT_GT(after, before);
}

TEST(GenesisVDP, HVCounterChangesWithTick) {
    GenesisVDP vdp;

    const uint16_t before = vdp.ReadHVCounter();
    vdp.Tick(1000);
    const uint16_t after = vdp.ReadHVCounter();

    EXPECT_NE(after, before);
}
