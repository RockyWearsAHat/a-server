#include <gtest/gtest.h>
#include "emulator/gamecube/GameCube.h"
#include "emulator/gamecube/GameCubeConstants.h"

using namespace GameCubeEmulator;

// ── GekkoResetSetsValidPC ────────────────────────────────────────────
// The Gekko CPU must reset to the PPC hardware reset vector 0xFFF00100.
TEST(GekkoTests, ResetSetsValidPC) {
    GameCube gc;
    gc.Reset();
    EXPECT_EQ(gc.GetCPU()->GetPC(), kBiosBase);
}

// ── GekkoStepAdvancesProgramCounter ──────────────────────────────────
// After Reset the PC is at the BIOS ROM. Executing one NOP (0x60000000)
// written into RAM should advance PC by 4. We map a NOP at RAM base and
// point the CPU there to exercise the step path.
TEST(GekkoTests, StepAdvancesProgramCounter) {
    GameCube gc;
    gc.Reset();

    // Write a single NOP (ori 0,0,0 == 0x60000000) to RAM base in
    // big-endian form, then force PC there.
    gc.GetMemory()->Write32(0x00000000, 0x60000000U);
    gc.GetCPU()->SetPC(0x00000000);

    uint32_t pc_before = gc.GetCPU()->GetPC();
    gc.GetCPU()->Step();
    uint32_t pc_after = gc.GetCPU()->GetPC();

    EXPECT_EQ(pc_after, pc_before + 4);
}

// ── RunFrameAdvancesFrameCounter ──────────────────────────────────────
// RunFrame must complete a full NTSC frame and increment the Flipper
// frame counter by exactly 1. We flood RAM with NOPs so the CPU can
// execute until the scanline counter wraps.
TEST(GekkoTests, RunFrameAdvancesFrameCounter) {
    GameCube gc;
    gc.Reset();

    // Fill first 4 MB of RAM with NOP instructions (0x60000000 BE)
    for (uint32_t addr = 0; addr < 0x00400000U; addr += 4) {
        gc.GetMemory()->Write32(addr, 0x60000000U);
    }

    // Point CPU at RAM so NOPs execute forever
    gc.GetCPU()->SetPC(0x00000000);

    uint32_t frame_before = gc.GetFlipper()->GetFrameCount();
    gc.RunFrame();
    uint32_t frame_after = gc.GetFlipper()->GetFrameCount();

    EXPECT_EQ(frame_after, frame_before + 1);
}
