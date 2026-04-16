#include <gtest/gtest.h>
#include "emulator/ps2/PS2.h"
#include "emulator/ps2/GS.h"

using namespace PS2Emulator;

TEST(PS2GSTests, FramebufferNotNull) {
    PS2 ps2;
    ps2.Reset();
    EXPECT_NE(ps2.GetFramebuffer(), nullptr);
}

TEST(PS2GSTests, PrivRegWriteReadRoundTrip) {
    PS2 ps2;
    ps2.Reset();
    GS* gs = ps2.GetGS();

    gs->WritePrivReg(0x00, 0xDEADBEEFCAFEBABEULL);
    EXPECT_EQ(gs->ReadPrivReg(0x00), 0xDEADBEEFCAFEBABEULL);

    gs->WritePrivReg(0x08, 0x0123456789ABCDEFULL);
    EXPECT_EQ(gs->ReadPrivReg(0x08), 0x0123456789ABCDEFULL);
}

TEST(PS2GSTests, FrameCounterSaveStateRoundTrip) {
    PS2 ps2;
    ps2.Reset();
    GS* gs = ps2.GetGS();

    gs->IncrementFrame();
    gs->IncrementFrame();
    EXPECT_EQ(gs->GetFrameCount(), 2U);

    auto state = gs->SaveState();
    gs->IncrementFrame();
    EXPECT_EQ(gs->GetFrameCount(), 3U);

    gs->LoadState(state);
    EXPECT_EQ(gs->GetFrameCount(), 2U);
}
