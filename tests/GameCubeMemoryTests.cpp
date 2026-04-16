#include <gtest/gtest.h>
#include "emulator/gamecube/GameCubeMemory.h"
#include "emulator/gamecube/Flipper.h"
using namespace GameCubeEmulator;

// ── MainRamWriteRead8 ─────────────────────────────────────────────────
// An 8-bit write to main RAM must be readable back at the same address.
TEST(GameCubeMemoryTests, MainRamWriteRead8) {
    GameCubeMemory mem;
    Flipper flipper(&mem);
    mem.Init(&flipper);

    constexpr uint32_t kAddr = 0x00001234U;
    constexpr uint8_t  kVal  = 0xABU;
    mem.Write8(kAddr, kVal);
    EXPECT_EQ(mem.Read8(kAddr), kVal);
}

// ── MainRamWrite32Read32 ──────────────────────────────────────────────
// A 32-bit write to main RAM must round-trip correctly (big-endian).
TEST(GameCubeMemoryTests, MainRamWrite32Read32) {
    GameCubeMemory mem;
    Flipper flipper(&mem);
    mem.Init(&flipper);

    constexpr uint32_t kAddr = 0x00008000U;
    constexpr uint32_t kVal  = 0x12345678U;
    mem.Write32(kAddr, kVal);
    EXPECT_EQ(mem.Read32(kAddr), kVal);
}

// ── MemorySaveStateRoundTrip ──────────────────────────────────────────
// SaveState / LoadState must preserve RAM contents exactly.
TEST(GameCubeMemoryTests, MemorySaveStateRoundTrip) {
    GameCubeMemory mem;
    Flipper flipper(&mem);
    mem.Init(&flipper);

    constexpr uint32_t kAddr = 0x00004000U;
    constexpr uint32_t kVal  = 0xCAFEBABEU;
    mem.Write32(kAddr, kVal);

    GameCubeMemory::State state = mem.SaveState();
    mem.Write32(kAddr, 0x00000000U);  // corrupt

    mem.LoadState(state);
    EXPECT_EQ(mem.Read32(kAddr), kVal);
}
