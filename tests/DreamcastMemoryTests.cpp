#include <gtest/gtest.h>
#include "emulator/dreamcast/Dreamcast.h"
#include "emulator/dreamcast/DreamcastConstants.h"

using namespace DreamcastEmulator;

// Write and read back a byte in system RAM
TEST(DreamcastMemoryTests, MainRamWriteRead8) {
    Dreamcast dc;
    dc.Reset();
    DreamcastMemory* mem = dc.GetMemory();

    mem->Write8(kRamBase, 0xABU);
    EXPECT_EQ(mem->Read8(kRamBase), 0xABU);

    mem->Write8(kRamBase + 1, 0xCDU);
    EXPECT_EQ(mem->Read8(kRamBase + 1), 0xCDU);
}

// Write and read back a 32-bit word in system RAM
TEST(DreamcastMemoryTests, MainRamWrite32Read32) {
    Dreamcast dc;
    dc.Reset();
    DreamcastMemory* mem = dc.GetMemory();

    mem->Write32(kRamBase + 0x100, 0x12345678U);
    EXPECT_EQ(mem->Read32(kRamBase + 0x100), 0x12345678U);

    mem->Write32(kRamBase + 0x200, 0xDEADC0DEU);
    EXPECT_EQ(mem->Read32(kRamBase + 0x200), 0xDEADC0DEU);
}

// SaveState/LoadState on DreamcastMemory restores RAM contents
TEST(DreamcastMemoryTests, MemorySaveStateRoundTrip) {
    Dreamcast dc;
    dc.Reset();
    DreamcastMemory* mem = dc.GetMemory();

    mem->Write8(kRamBase + 0x10, 0x77U);
    auto state = mem->SaveState();

    mem->Write8(kRamBase + 0x10, 0x99U);  // mutate after save
    EXPECT_EQ(mem->Read8(kRamBase + 0x10), 0x99U);

    mem->LoadState(state);
    EXPECT_EQ(mem->Read8(kRamBase + 0x10), 0x77U);
}
