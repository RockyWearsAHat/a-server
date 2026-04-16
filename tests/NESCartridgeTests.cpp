#include "emulator/nes/NESCartridge.h"
#include "emulator/common/SaveState.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include <stdexcept>

using namespace AIO::Emulator::NES;

namespace {

// Build a minimal valid NROM ROM.
// prg16Count: number of 16KB PRG banks; chrCount: number of 8KB CHR banks (0 = CHR RAM).
std::vector<uint8_t> MakeRom(uint8_t prg16Count = 1, uint8_t chrCount = 0,
                              uint8_t mapper = 0) {
    const size_t prgSize = static_cast<size_t>(prg16Count) * 16384;
    const size_t chrSize = static_cast<size_t>(chrCount) * 8192;
    std::vector<uint8_t> rom(16 + prgSize + chrSize, 0xEA);
    rom[0] = 'N'; rom[1] = 'E'; rom[2] = 'S'; rom[3] = 0x1A;
    rom[4] = prg16Count;
    rom[5] = chrCount;
    rom[6] = static_cast<uint8_t>((mapper & 0x0F) << 4);
    rom[7] = static_cast<uint8_t>(mapper & 0xF0);
    return rom;
}

} // namespace

// ── Load ────────────────────────────────────────────────────────────────────

TEST(NESCartridgeTests, NromLoad_Valid) {
    const auto rom = MakeRom(1, 0, 0);
    NESCartridge cart;
    EXPECT_NO_THROW(cart.Load(rom));
}

TEST(NESCartridgeTests, InvalidHeader_Throws) {
    std::vector<uint8_t> garbage(256, 0xFF);
    NESCartridge cart;
    EXPECT_THROW(cart.Load(garbage), std::invalid_argument);
}

TEST(NESCartridgeTests, TooShortHeader_Throws) {
    std::vector<uint8_t> tiny(8, 0);
    NESCartridge cart;
    EXPECT_THROW(cart.Load(tiny), std::invalid_argument);
}

// ── CHR RAM when no CHR ROM ──────────────────────────────────────────────────

TEST(NESCartridgeTests, ChrRam_WhenNoChrRom) {
    const auto rom = MakeRom(1, 0, 0); // 0 CHR banks → CHR RAM
    NESCartridge cart;
    cart.Load(rom);

    // Write to CHR address and read it back
    cart.PpuWrite(0x0000, 0xBE);
    EXPECT_EQ(cart.PpuRead(0x0000), 0xBEu);
}

// ── NROM mirroring — 32KB maps both banks ────────────────────────────────────

TEST(NESCartridgeTests, Nrom_PrgMirror_32K) {
    // 2× PRG banks → fill bank 0 with 0xAA, bank 1 with 0xBB
    const size_t prgSize = 32768;
    std::vector<uint8_t> rom(16 + prgSize, 0);
    rom[0]='N'; rom[1]='E'; rom[2]='S'; rom[3]=0x1A;
    rom[4]=2; rom[5]=0; // 2 PRG banks
    for (size_t i = 0; i < 16384; ++i) rom[16 + i] = 0xAA;
    for (size_t i = 0; i < 16384; ++i) rom[16 + 16384 + i] = 0xBB;

    NESCartridge cart;
    cart.Load(rom);

    EXPECT_EQ(cart.CpuRead(0x8000), 0xAAu); // bank 0
    EXPECT_EQ(cart.CpuRead(0xC000), 0xBBu); // bank 1
}

// ── Save / Load state ────────────────────────────────────────────────────────

TEST(NESCartridgeTests, SaveLoadState_RoundTrip) {
    using namespace AIO::Emulator::Common;

    const auto rom = MakeRom(1, 0, 0);
    NESCartridge cart;
    cart.Load(rom);
    cart.PpuWrite(0x0010, 0x55);

    std::vector<uint8_t> buf;
    {
        SaveStateWriter w;
        cart.SaveState(w);
        buf = w.Buffer();
    }
    {
        // Reset cart
        NESCartridge cart2;
        cart2.Load(rom);

        SaveStateReader r(buf);
        cart2.LoadState(r);

        EXPECT_EQ(cart2.PpuRead(0x0010), 0x55u);
    }
}
