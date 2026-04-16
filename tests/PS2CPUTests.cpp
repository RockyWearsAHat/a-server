#include <gtest/gtest.h>
#include "emulator/ps2/PS2.h"

using namespace PS2Emulator;

// EE boots at BIOS ROM entry (KSEG1 mirror of 0x1FC00000)
TEST(PS2CPUTests, ResetSetsValidPC) {
    PS2 ps2;
    ps2.Reset();
    EXPECT_EQ(ps2.GetEE()->GetPC(), 0xBFC00000U);
}

// IOP also boots at the BIOS vector
TEST(PS2CPUTests, IOPResetSetsValidPC) {
    PS2 ps2;
    ps2.Reset();
    EXPECT_EQ(ps2.GetIOP()->GetPC(), 0xBFC00000U);
}

// Running a frame increments the frame counter
TEST(PS2CPUTests, RunFrameAdvancesFrameCounter) {
    PS2 ps2;
    ps2.Reset();
    uint32_t before = ps2.GetFrameCount();
    ps2.RunFrame();
    EXPECT_GT(ps2.GetFrameCount(), before);
}
