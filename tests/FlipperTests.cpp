#include <gtest/gtest.h>
#include "emulator/gamecube/Flipper.h"
#include "emulator/gamecube/GameCubeMemory.h"

using namespace GameCubeEmulator;

// ── FlipperFramebufferNotNull ─────────────────────────────────────────
// The EFB (Embedded FrameBuffer) pointer must be non-null after
// construction and Reset, and the first pixel must equal the clear colour
// (0xFF000000 = opaque black in RGBA8).
TEST(FlipperTests, FramebufferNotNull) {
    GameCubeMemory mem;
    Flipper flipper(&mem);
    mem.Init(&flipper);

    flipper.Reset();
    const uint32_t* fb = flipper.GetFramebuffer();
    ASSERT_NE(fb, nullptr);
    EXPECT_EQ(fb[0], 0xFF000000U);
}

// ── FlipperRegWriteReadRoundTrip ──────────────────────────────────────
// A 32-bit write to a Flipper register must be readable back at the
// same offset without corruption.
TEST(FlipperTests, RegWriteReadRoundTrip) {
    GameCubeMemory mem;
    Flipper flipper(&mem);
    mem.Init(&flipper);

    constexpr uint32_t kOffset = 0x0020U;  // arbitrary Flipper register
    constexpr uint32_t kValue  = 0xDEADBEEFU;
    flipper.WriteReg(kOffset, kValue);
    EXPECT_EQ(flipper.ReadReg(kOffset), kValue);
}

// ── FlipperFrameCounterSaveStateRoundTrip ─────────────────────────────
// SaveState / LoadState must restore the frame counter exactly.
TEST(FlipperTests, FrameCounterSaveStateRoundTrip) {
    GameCubeMemory mem;
    Flipper flipper(&mem);
    mem.Init(&flipper);

    flipper.IncrementFrame();
    flipper.IncrementFrame();
    flipper.IncrementFrame();  // frame_count == 3

    Flipper::State state = flipper.SaveState();

    flipper.Reset();           // back to 0
    ASSERT_EQ(flipper.GetFrameCount(), 0U);

    flipper.LoadState(state);
    EXPECT_EQ(flipper.GetFrameCount(), 3U);
}
