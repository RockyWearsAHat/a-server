#include <gtest/gtest.h>
#include "emulator/dreamcast/Dreamcast.h"
#include "emulator/dreamcast/PowerVR2.h"

using namespace DreamcastEmulator;

// Framebuffer pointer must not be null after construction
TEST(DreamcastGPUTests, FramebufferNotNull) {
    Dreamcast dc;
    dc.Reset();
    EXPECT_NE(dc.GetFramebuffer(), nullptr);
}

// Register write followed by read must round-trip
TEST(DreamcastGPUTests, RegWriteReadRoundTrip) {
    Dreamcast dc;
    dc.Reset();
    PowerVR2* pvr = dc.GetPVR();

    pvr->WriteReg(0x00, 0xDEADBEEFU);
    EXPECT_EQ(pvr->ReadReg(0x00), 0xDEADBEEFU);

    pvr->WriteReg(0x04, 0xCAFEBABEU);
    EXPECT_EQ(pvr->ReadReg(0x04), 0xCAFEBABEU);
}

// Frame counter must survive a SaveState/LoadState round-trip
TEST(DreamcastGPUTests, FrameCounterSaveStateRoundTrip) {
    Dreamcast dc;
    dc.Reset();
    PowerVR2* pvr = dc.GetPVR();

    pvr->IncrementFrame();
    pvr->IncrementFrame();
    pvr->IncrementFrame();
    uint32_t saved = pvr->GetFrameCount();
    EXPECT_EQ(saved, 3U);

    auto state = pvr->SaveState();
    pvr->IncrementFrame();  // frame count becomes 4
    EXPECT_EQ(pvr->GetFrameCount(), 4U);

    pvr->LoadState(state);
    EXPECT_EQ(pvr->GetFrameCount(), 3U);
}
