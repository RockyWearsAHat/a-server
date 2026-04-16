#include "emulator/nes/NES.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

using namespace AIO::Emulator::NES;

namespace {

// Build a minimal NROM ROM (1 PRG bank, CHR RAM).
std::vector<uint8_t> MakeNromRom(const std::vector<uint8_t>& prog, uint16_t resetVec = 0x8000) {
    std::vector<uint8_t> rom(16 + 16384, 0); // header zeroed, PRG zeroed
    rom[0]='N'; rom[1]='E'; rom[2]='S'; rom[3]=0x1A;
    rom[4]=1; rom[5]=0;
    // Fill PRG with NOP ($EA) as default instruction
    std::fill(rom.begin() + 16, rom.end(), static_cast<uint8_t>(0xEA));
    for (size_t i=0; i<prog.size() && i<16383; ++i) rom[16+i]=prog[i];
    rom[16+0x3FFC] = static_cast<uint8_t>(resetVec & 0xFF);
    rom[16+0x3FFD] = static_cast<uint8_t>(resetVec >> 8);
    return rom;
}

} // namespace

// ── PPU register write/read ─────────────────────────────────────────────────

TEST(NESPPUTests, PPUADDR_PPUDATA_ReadWrite) {
    // Write to PPUADDR then PPUDATA; read back via PPUDATA
    // We use a simple NES with an infinite loop program so we can control PPU
    const std::vector<uint8_t> prog = { 0x4C, 0x00, 0x80 }; // JMP $8000
    const auto rom = MakeNromRom(prog, 0x8000);

    NES nes;
    nes.Load(rom);

    // Write 0xAB to PPU address 0x0010 (pattern table CHR RAM)
    // PPUADDR ($2006) requires two writes (high byte then low byte)
    nes.GetPPU().WriteRegister(0x06, 0x00); // high byte of $0010
    nes.GetPPU().WriteRegister(0x06, 0x10); // low byte of $0010
    nes.GetPPU().WriteRegister(0x07, 0xAB); // write PPUDATA

    // Read back: PPUADDR to same location, read PPUDATA (first read is buffered)
    nes.GetPPU().WriteRegister(0x06, 0x00);
    nes.GetPPU().WriteRegister(0x06, 0x10);
    static_cast<void>(nes.GetPPU().ReadRegister(0x07)); // discard buffered read
    nes.GetPPU().WriteRegister(0x06, 0x00);
    nes.GetPPU().WriteRegister(0x06, 0x10);
    const uint8_t val = nes.GetPPU().ReadRegister(0x07);

    EXPECT_EQ(val, 0xABu);
}

// ── OAM DMA copies 256 bytes ────────────────────────────────────────────────

TEST(NESPPUTests, OamDma_Copies256Bytes) {
    std::array<uint8_t, 256> page{};
    for (int i = 0; i < 256; ++i) page[i] = static_cast<uint8_t>(i);

    const std::vector<uint8_t> prog = { 0x4C, 0x00, 0x80 };
    const auto rom = MakeNromRom(prog, 0x8000);

    NES nes;
    nes.Load(rom);

    nes.GetPPU().OamDma(page.data());

    // Read back: OAMDATA reads are non-advancing (hardware-accurate).
    // Set OAMADDR before each read.
    for (int i = 0; i < 256; ++i) {
        nes.GetPPU().WriteRegister(0x03, static_cast<uint8_t>(i)); // OAMADDR = i
        const uint8_t got = nes.GetPPU().ReadRegister(0x04);
        EXPECT_EQ(got, static_cast<uint8_t>(i)) << "at OAM index " << i;
    }
}

// ── VBlank / NMI fires when PPUCTRL bit 7 is set ───────────────────────────

TEST(NESPPUTests, VBlank_NmiCallback_Fires) {
    // This test runs the NES for a full frame and checks that NMI was triggered.
    // NES frame = 89,342 CPU cycles (NTSC). We track NMI via a counter.
    const std::vector<uint8_t> prog = { 0x4C, 0x00, 0x80 }; // tight loop
    const auto rom = MakeNromRom(prog, 0x8000);

    NES nes;
    nes.Load(rom);

    int nmiCount = 0;
    nes.GetPPU().SetNmiCallback([&nmiCount]() { ++nmiCount; });

    // Enable NMI generation: PPUCTRL bit 7
    nes.GetPPU().WriteRegister(0x00, 0x80);

    // Run slightly more than one full frame (89343 CPU cycles in NTSC)
    for (int i = 0; i < 90000; ++i) {
        static_cast<void>(nes.Step());
    }

    EXPECT_GE(nmiCount, 1) << "Expected at least one NMI in one NES frame";
}
