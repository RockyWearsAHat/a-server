#include "emulator/snes/SNES.h"

#include <gtest/gtest.h>
#include <vector>

using AIO::Emulator::SNES::SNES;

namespace {

std::vector<uint8_t> MakeSnesRom() {
    std::vector<uint8_t> rom(1024 * 1024, 0xEA); // NOP stream

    // 65816 reset vector at 00:FFFC -> 0x8000.
    rom[0xFFFC] = 0x00;
    rom[0xFFFD] = 0x80;

    return rom;
}

} // namespace

TEST(SNESCPU, ResetLoadsVector) {
    SNES snes;
    const auto rom = MakeSnesRom();

    snes.Load(rom);

    EXPECT_EQ(snes.GetCPU().GetPC(), 0x8000u);
}

TEST(SNESCPU, StepAdvancesMasterCycles) {
    SNES snes;
    const auto rom = MakeSnesRom();

    snes.Load(rom);

    const uint64_t before = snes.GetMasterCycles();
    static_cast<void>(snes.Step());
    const uint64_t after = snes.GetMasterCycles();

    EXPECT_GT(after, before);
}

TEST(SNESCPU, RunFrameAdvancesPPUFrameCounter) {
    SNES snes;
    const auto rom = MakeSnesRom();

    snes.Load(rom);

    const uint64_t before = snes.GetPPU().FrameCount();
    snes.RunFrame();
    const uint64_t after = snes.GetPPU().FrameCount();

    EXPECT_GT(after, before);
}
