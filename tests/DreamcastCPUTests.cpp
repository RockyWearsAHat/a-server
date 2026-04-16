#include <gtest/gtest.h>
#include "emulator/dreamcast/Dreamcast.h"

using namespace DreamcastEmulator;

// SH-4 boots to P2 uncached mirror of Area 0 (boot ROM)
TEST(DreamcastCPUTests, ResetSetsValidPC) {
    Dreamcast dc;
    dc.Reset();
    EXPECT_EQ(dc.GetCPU()->GetPC(), 0xA0000000U);
}

// Every Step() advances PC by at least 2 (minimum SH-4 instruction size)
TEST(DreamcastCPUTests, StepAdvancesProgramCounter) {
    Dreamcast dc;
    dc.Reset();
    // Plant a NOP (0x0009) in RAM at the boot vector physical address 0x00000000
    dc.GetMemory()->Write16(0x00000000U, 0x0009U);  // physical address
    // CPU PC is 0xA0000000 → physical 0x00000000
    uint32_t pc_before = dc.GetCPU()->GetPC();
    dc.Step();
    EXPECT_EQ(dc.GetCPU()->GetPC(), pc_before + 2);
}

// Running a full frame increments the frame counter
TEST(DreamcastCPUTests, RunFrameAdvancesFrameCounter) {
    Dreamcast dc;
    dc.Reset();
    // Plant a tight NOP loop so RunFrame() doesn't stall
    // Just check the frame lands
    uint32_t before = dc.GetFrameCount();
    dc.RunFrame();
    EXPECT_GT(dc.GetFrameCount(), before);
}
