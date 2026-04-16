#include <gtest/gtest.h>
#include "emulator/ps2/PS2.h"
#include "emulator/ps2/PS2Constants.h"

using namespace PS2Emulator;

TEST(PS2MemoryTests, EERamWriteRead8) {
    PS2 ps2;
    ps2.Reset();
    PS2Memory* mem = ps2.GetMemory();

    mem->Write8(kEERamBase + 0x00, 0xAAU);
    EXPECT_EQ(mem->Read8(kEERamBase + 0x00), 0xAAU);

    mem->Write8(kEERamBase + 0x01, 0x55U);
    EXPECT_EQ(mem->Read8(kEERamBase + 0x01), 0x55U);
}

TEST(PS2MemoryTests, EERamWrite32Read32) {
    PS2 ps2;
    ps2.Reset();
    PS2Memory* mem = ps2.GetMemory();

    mem->Write32(kEERamBase + 0x100, 0xDEADC0DEU);
    EXPECT_EQ(mem->Read32(kEERamBase + 0x100), 0xDEADC0DEU);
}

TEST(PS2MemoryTests, MemorySaveStateRoundTrip) {
    PS2 ps2;
    ps2.Reset();
    PS2Memory* mem = ps2.GetMemory();

    mem->Write8(kEERamBase + 0x50, 0xBBU);
    auto state = mem->SaveState();

    mem->Write8(kEERamBase + 0x50, 0xFFU);
    EXPECT_EQ(mem->Read8(kEERamBase + 0x50), 0xFFU);

    mem->LoadState(state);
    EXPECT_EQ(mem->Read8(kEERamBase + 0x50), 0xBBU);
}
