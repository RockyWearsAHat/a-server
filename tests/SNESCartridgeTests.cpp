#include "emulator/common/SaveState.h"
#include "emulator/snes/SNESCartridge.h"

#include <gtest/gtest.h>
#include <vector>

using AIO::Emulator::Common::SaveStateReader;
using AIO::Emulator::Common::SaveStateWriter;
using AIO::Emulator::SNES::SNESCartridge;

namespace {

std::vector<uint8_t> MakeRom(size_t size = 1024 * 1024) {
    std::vector<uint8_t> rom(size, 0);
    for (size_t i = 0; i < size; ++i) {
        rom[i] = static_cast<uint8_t>(i & 0xFF);
    }
    return rom;
}

} // namespace

TEST(SNESCartridge, LoadAndRead8RoundTrip) {
    SNESCartridge cart;
    const auto rom = MakeRom();

    cart.Load(rom);

    EXPECT_EQ(cart.Read8(0x1234), rom[0x1234]);
}

TEST(SNESCartridge, SRAMWritePersistsThroughSaveState) {
    SNESCartridge cart;
    const auto rom = MakeRom();

    cart.Load(rom);
    cart.Write8(0x700010, 0x5A);

    SaveStateWriter writer;
    cart.SaveState(writer);

    SNESCartridge restored;
    restored.Load(rom);
    SaveStateReader reader(writer.Buffer());
    restored.LoadState(reader);

    EXPECT_TRUE(restored.HasSram());
}

TEST(SNESCartridge, SmallRomThrows) {
    SNESCartridge cart;
    const std::vector<uint8_t> rom(1024, 0);

    EXPECT_THROW(cart.Load(rom), std::runtime_error);
}
