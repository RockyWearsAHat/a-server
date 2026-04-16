#include "emulator/common/SaveState.h"
#include "emulator/genesis/GenesisCartridge.h"
#include <gtest/gtest.h>
#include <vector>

using AIO::Emulator::Common::SaveStateReader;
using AIO::Emulator::Common::SaveStateWriter;
using AIO::Emulator::Genesis::GenesisCartridge;

namespace {

std::vector<uint8_t> MakeRom(size_t size = 1024 * 1024) {
    std::vector<uint8_t> rom(size, 0);
    for (size_t i = 0; i < size; ++i) {
        rom[i] = static_cast<uint8_t>(i & 0xFF);
    }
    return rom;
}

} // namespace

TEST(GenesisCartridge, LoadAndReadWord) {
    GenesisCartridge cart;
    const auto rom = MakeRom();

    cart.Load(rom);

    EXPECT_EQ(cart.Read8(0x10), rom[0x10]);
    EXPECT_EQ(cart.Read16(0x20), static_cast<uint16_t>((rom[0x20] << 8) | rom[0x21]));
}

TEST(GenesisCartridge, SaveLoadStatePreservesSram) {
    GenesisCartridge cart;
    auto rom = MakeRom();

    // Enable SRAM in header.
    rom[0x1B0] = 'R';
    rom[0x1B1] = 'A';
    rom[0x1B2] = 0x40;
    rom[0x1B4] = 0x00;
    rom[0x1B5] = 0x20;
    rom[0x1B6] = 0x00;
    rom[0x1B7] = 0x00;

    cart.Load(rom);
    cart.WriteBankReg(0, 0x01); // SRAM enable
    cart.Write8(0x00200000, 0x5A);

    SaveStateWriter w;
    cart.SaveState(w);
    const auto buffer = w.Buffer();

    GenesisCartridge restored;
    restored.Load(rom);
    SaveStateReader r(buffer);
    restored.LoadState(r);

    EXPECT_EQ(restored.Read8(0x00200000), 0x5Au);
}

TEST(GenesisCartridge, InvalidSmallRomThrows) {
    GenesisCartridge cart;
    const std::vector<uint8_t> rom(16, 0);
    EXPECT_THROW(cart.Load(rom), std::runtime_error);
}
