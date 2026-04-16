#include "emulator/genesis/Genesis.h"
#include <gtest/gtest.h>
#include <vector>

using AIO::Emulator::Genesis::Genesis;

namespace {

std::vector<uint8_t> MakeGenesisRom() {
    // Build a 2 MB ROM image with a valid reset vector and NOP stream.
    std::vector<uint8_t> rom(2 * 1024 * 1024, 0x4E);
    for (size_t i = 1; i < rom.size(); i += 2) {
        rom[i] = 0x71; // 0x4E71 NOP
    }

    // Initial SSP (vector 0): 0x00FF0000
    rom[0x0] = 0x00;
    rom[0x1] = 0xFF;
    rom[0x2] = 0x00;
    rom[0x3] = 0x00;

    // Initial PC (vector 1): 0x00000200 (skip header region at 0x100-0x1FF)
    rom[0x4] = 0x00;
    rom[0x5] = 0x00;
    rom[0x6] = 0x02;
    rom[0x7] = 0x00;

    // HINT autovector (level 4, vector #0x1C @ 0x70) -> 0x00000200
    rom[0x70] = 0x00;
    rom[0x71] = 0x00;
    rom[0x72] = 0x02;
    rom[0x73] = 0x00;

    // VINT autovector (level 6, vector #0x1E @ 0x78) -> 0x00000200
    rom[0x78] = 0x00;
    rom[0x79] = 0x00;
    rom[0x7A] = 0x02;
    rom[0x7B] = 0x00;

    // Header marker so this is recognizable as test ROM
    const char* title = "AIO GENESIS TEST";
    for (int i = 0; title[i] != '\0'; ++i) {
        rom[0x150 + i] = static_cast<uint8_t>(title[i]);
    }

    return rom;
}

} // namespace

TEST(GenesisCPU, ResetLoadsVectors) {
    Genesis gen;
    const auto rom = MakeGenesisRom();
    gen.Load(rom);

    EXPECT_EQ(gen.GetCPU().GetSP(), 0x00FF0000u);
    EXPECT_EQ(gen.GetCPU().GetPC(), 0x00000200u);
}

TEST(GenesisCPU, StepAdvancesMasterCycles) {
    Genesis gen;
    const auto rom = MakeGenesisRom();
    gen.Load(rom);

    const uint64_t before = gen.GetTotalCycles();
    static_cast<void>(gen.Step());
    const uint64_t after = gen.GetTotalCycles();

    EXPECT_GT(after, before);
}

TEST(GenesisCPU, RunFrameAdvancesFrameCounter) {
    Genesis gen;
    const auto rom = MakeGenesisRom();
    gen.Load(rom);

    const uint64_t before = gen.GetVDP().FrameCount();
    gen.RunFrame();
    const uint64_t after = gen.GetVDP().FrameCount();

    EXPECT_GT(after, before);
}
