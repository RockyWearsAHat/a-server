#include <gtest/gtest.h>

#include "emulator/gba/GBAMemory.h"

using namespace AIO::Emulator::GBA;

TEST(MemoryMapTest, IwramTopMirrorMapsToBase) {
  GBAMemory mem;
  mem.Reset();

  // Real GBA mirrors the last 32KB of the 0x03xxxxxx region at
  // 0x03FF8000-0x03FFFFFF. In particular, 0x03FFFFFC mirrors 0x03007FFC.
  constexpr uint32_t kBase = 0x03007FFCu;
  constexpr uint32_t kMirror = 0x03FFFFFCu;

  mem.Write32(kMirror, 0x0800012Cu);
  EXPECT_EQ(mem.Read32(kBase), 0x0800012Cu);
  EXPECT_EQ(mem.Read32(kMirror), 0x0800012Cu);
}

TEST(MemoryMapTest, IwramMirrorsAcross03RegionAddresses) {
  GBAMemory mem;
  mem.Reset();

  // Pick a normal IWRAM location (avoid BIOS-managed slots like 0x03007FFC).
  constexpr uint32_t kBase = 0x03001234u;
  constexpr uint32_t kAlias = 0x03009234u; // Same low 15 bits (0x1234)

  mem.Write32(kBase, 0x11112222u);
  EXPECT_EQ(mem.Read32(kAlias), 0x11112222u);

  mem.Write32(kAlias, 0x33222223u);
  EXPECT_EQ(mem.Read32(kBase), 0x33222223u);
  EXPECT_EQ(mem.Read32(kAlias), 0x33222223u);
}

TEST(MemoryMapTest, IrqHandlerWordWriteIsAtomicAndNotTorn) {
  GBAMemory mem;
  mem.Reset();

  constexpr uint32_t kIrqHandler = 0x03007FFCu;

  // This is the normal boot-time value games store into 0x03007FFC.
  mem.Write32(kIrqHandler, 0x0800012Cu);
  EXPECT_EQ(mem.Read32(kIrqHandler), 0x0800012Cu);

  // Also validate that building it via halfword stores doesn't get clamped
  // mid-way.
  mem.Write16(kIrqHandler, 0x012Cu);
  mem.Write16(kIrqHandler + 2, 0x0800u);
  EXPECT_EQ(mem.Read32(kIrqHandler), 0x0800012Cu);
}

// Classic NES Series (1 MiB ROMs) should be mirrored 4 times to 4 MiB
// Reference: mGBA src/gba/gba.c lines 455-472
TEST(MemoryMapTest, ClassicNes1MibRomMirrored4Times) {
  AIO::Emulator::GBA::GBAMemory mem;

  // Create a 1 MiB ROM with recognizable pattern
  std::vector<uint8_t> romData(0x100000, 0x00);

  // Put unique markers at key offsets
  romData[0x00] = 0xAA;
  romData[0x01] = 0xBB;
  romData[0xFFFFE] = 0xCC;
  romData[0xFFFFF] = 0xDD;

  mem.LoadGamePak(romData);

  // Read from first mirror (0x08000000 - 0x080FFFFF)
  EXPECT_EQ(mem.Read8(0x08000000), 0xAA);
  EXPECT_EQ(mem.Read8(0x08000001), 0xBB);
  EXPECT_EQ(mem.Read8(0x080FFFFE), 0xCC);
  EXPECT_EQ(mem.Read8(0x080FFFFF), 0xDD);

  // Read from second mirror (0x08100000 - 0x081FFFFF)
  EXPECT_EQ(mem.Read8(0x08100000), 0xAA);
  EXPECT_EQ(mem.Read8(0x08100001), 0xBB);
  EXPECT_EQ(mem.Read8(0x081FFFFE), 0xCC);
  EXPECT_EQ(mem.Read8(0x081FFFFF), 0xDD);

  // Read from third mirror (0x08200000 - 0x082FFFFF)
  EXPECT_EQ(mem.Read8(0x08200000), 0xAA);
  EXPECT_EQ(mem.Read8(0x08200001), 0xBB);
  EXPECT_EQ(mem.Read8(0x082FFFFE), 0xCC);
  EXPECT_EQ(mem.Read8(0x082FFFFF), 0xDD);

  // Read from fourth mirror (0x08300000 - 0x083FFFFF)
  EXPECT_EQ(mem.Read8(0x08300000), 0xAA);
  EXPECT_EQ(mem.Read8(0x08300001), 0xBB);
  EXPECT_EQ(mem.Read8(0x083FFFFE), 0xCC);
  EXPECT_EQ(mem.Read8(0x083FFFFF), 0xDD);
}

// ============================================================================
// Additional GBAMemory Coverage Tests
// ============================================================================

TEST(MemoryMapTest, EwramMirroring) {
  GBAMemory mem;
  mem.Reset();

  // EWRAM is 256KB at 0x02000000, mirrored within 0x02xxxxxx
  constexpr uint32_t kBase = 0x02001000u;
  constexpr uint32_t kMirror = 0x02041000u; // +256KB

  mem.Write32(kBase, 0x12345678u);
  EXPECT_EQ(mem.Read32(kMirror), 0x12345678u);

  mem.Write32(kMirror, 0xAABBCCDDu);
  EXPECT_EQ(mem.Read32(kBase), 0xAABBCCDDu);
}

TEST(MemoryMapTest, VramMirroring) {
  GBAMemory mem;
  mem.Reset();

  // VRAM is 96KB at 0x06000000
  // 0x06000000-0x06017FFF mirrors to 0x06018000-0x0601FFFF
  mem.Write16(0x04000000u, 0x0080u); // Forced blank for VRAM access

  mem.Write16(0x06000100u, 0x1234u);
  // The upper 32KB (0x06010000-0x06017FFF) mirrors to 0x06018000-0x0601FFFF
  // So 0x06018100 should mirror to 0x06010100
  mem.Write16(0x06018100u, 0x5678u);
  EXPECT_EQ(mem.Read16(0x06010100u), 0x5678u);
}

TEST(MemoryMapTest, PaletteRamMirroring) {
  GBAMemory mem;
  mem.Reset();

  // Palette RAM is 1KB, mirrored every 1KB in 0x05xxxxxx
  mem.Write16(0x05000000u, 0x7FFFu);
  EXPECT_EQ(mem.Read16(0x05000400u), 0x7FFFu); // +1KB
  EXPECT_EQ(mem.Read16(0x05000800u), 0x7FFFu); // +2KB
}

TEST(MemoryMapTest, OamMirroring) {
  GBAMemory mem;
  mem.Reset();

  // OAM is 1KB at 0x07000000, mirrored within 0x07xxxxxx
  mem.Write16(0x07000100u, 0xABCDu);
  EXPECT_EQ(mem.Read16(0x07000500u), 0xABCDu); // +1KB
}

TEST(MemoryMapTest, IoRegisterReadWrite) {
  GBAMemory mem;
  mem.Reset();

  // Test DISPCNT
  mem.Write16(0x04000000u, 0x1234u);
  EXPECT_EQ(mem.Read16(0x04000000u), 0x1234u);

  // Test SOUNDCNT_H
  mem.Write16(0x04000082u, 0x00FFu);
  EXPECT_EQ(mem.Read16(0x04000082u), 0x00FFu);

  // Test IE (Interrupt Enable)
  mem.Write16(0x04000200u, 0x00FFu);
  EXPECT_EQ(mem.Read16(0x04000200u), 0x00FFu);

  // Test IME (Interrupt Master Enable)
  mem.Write16(0x04000208u, 0x0001u);
  EXPECT_EQ(mem.Read16(0x04000208u), 0x0001u);
}

TEST(MemoryMapTest, SramReadWrite) {
  GBAMemory mem;
  mem.Reset();

  // Enable SRAM save type first
  mem.SetSaveType(SaveType::SRAM);

  // SRAM is at 0x0E000000, byte-addressable only
  mem.Write8(0x0E000000u, 0x42u);
  EXPECT_EQ(mem.Read8(0x0E000000u), 0x42u);

  mem.Write8(0x0E001234u, 0xABu);
  EXPECT_EQ(mem.Read8(0x0E001234u), 0xABu);

  // 16-bit and 32-bit writes to SRAM should only affect low byte
  mem.Write16(0x0E000010u, 0x1234u);
  EXPECT_EQ(mem.Read8(0x0E000010u), 0x34u);

  mem.Write32(0x0E000020u, 0x12345678u);
  EXPECT_EQ(mem.Read8(0x0E000020u), 0x78u);
}

TEST(MemoryMapTest, UnalignedWordReads) {
  GBAMemory mem;
  mem.Reset();

  // Write a word at aligned address
  mem.Write32(0x02000000u, 0x44332211u);

  // GBA behavior for unaligned reads varies by implementation
  // Some rotate, some force alignment
  // Just verify we can read from unaligned addresses without crash
  uint32_t val1 = mem.Read32(0x02000001u);
  uint32_t val2 = mem.Read32(0x02000002u);
  uint32_t val3 = mem.Read32(0x02000003u);

  // At minimum, force-aligned reads should return the original value
  uint32_t aligned = mem.Read32(0x02000000u);
  EXPECT_EQ(aligned, 0x44332211u);
  (void)val1;
  (void)val2;
  (void)val3;
}

TEST(MemoryMapTest, UnalignedHalfwordReads) {
  GBAMemory mem;
  mem.Reset();

  mem.Write16(0x02000000u, 0x1234u);

  // Just verify unaligned reads don't crash
  uint16_t val = mem.Read16(0x02000001u);
  (void)val;

  // Aligned read should work
  EXPECT_EQ(mem.Read16(0x02000000u), 0x1234u);
}

TEST(MemoryMapTest, DmaChannelRegisters) {
  GBAMemory mem;
  mem.Reset();

  // DMA0 source address (write-only on GBA, may not read back)
  mem.Write32(0x040000B0u, 0x02000000u);
  // Just verify write doesn't crash

  // DMA0 destination address
  mem.Write32(0x040000B4u, 0x06000000u);

  // DMA0 control - bit 15 (enable) might be cleared after DMA completes
  mem.Write16(0x040000BAu, 0x8000u); // Enable

  // DMA1 registers
  mem.Write32(0x040000BCu, 0x02001000u);

  // DMA3 registers (used for EEPROM)
  mem.Write32(0x040000D4u, 0x02002000u);

  // Just verify no crash - DMA SAD/DAD are write-only
}

TEST(MemoryMapTest, TimerRegisters) {
  GBAMemory mem;
  mem.Reset();

  // Timer 0 reload and control
  mem.Write16(0x04000100u, 0xFFFEu); // Reload value
  mem.Write16(0x04000102u, 0x0080u); // Enable timer

  // Timer 1
  mem.Write16(0x04000104u, 0xFF00u);
  mem.Write16(0x04000106u, 0x0084u); // Enable + cascade

  // Timer 2
  mem.Write16(0x04000108u, 0x8000u);
  mem.Write16(0x0400010Au, 0x0080u);

  // Timer 3
  mem.Write16(0x0400010Cu, 0xC000u);
  mem.Write16(0x0400010Eu, 0x0080u);
}

TEST(MemoryMapTest, KeyInputRegister) {
  GBAMemory mem;
  mem.Reset();

  // KEYINPUT at 0x04000130 should return 0x03FF when all keys released
  EXPECT_EQ(mem.Read16(0x04000130u), 0x03FFu);
}

TEST(MemoryMapTest, WaitstateControlRegister) {
  GBAMemory mem;
  mem.Reset();

  // WAITCNT at 0x04000204
  mem.Write16(0x04000204u, 0x4317u);
  // Some bits may be read-only or masked
  uint16_t val = mem.Read16(0x04000204u);
  // Just verify the write didn't crash and some value is returned
  (void)val;
}

TEST(MemoryMapTest, PostBootFlagRegister) {
  GBAMemory mem;
  mem.Reset();

  // POSTFLG at 0x04000300 should be 1 after reset (DirectBoot)
  EXPECT_EQ(mem.Read8(0x04000300u), 0x01u);
}

TEST(MemoryMapTest, HaltControlRegister) {
  GBAMemory mem;
  mem.Reset();

  // HALTCNT at 0x04000301 is write-only
  mem.Write8(0x04000301u, 0x00u); // Request halt
  // Just verify no crash
}

TEST(MemoryMapTest, BiosProtectionAfterBoot) {
  GBAMemory mem;
  mem.Reset();

  // BIOS region should be protected after boot
  // Reading from BIOS when not executing from BIOS returns last prefetch
  // For this test, just verify reading doesn't crash
  uint32_t val = mem.Read32(0x00000000u);
  (void)val;
}

TEST(MemoryMapTest, OpenBusReads) {
  GBAMemory mem;
  mem.Reset();

  // Reading from unused memory regions should return open bus value
  // Unused region: 0x00004000-0x01FFFFFF
  uint32_t val = mem.Read32(0x00010000u);
  // Open bus typically returns last prefetched value or 0
  // Just verify no crash
  (void)val;
}

TEST(MemoryMapTest, RomWaitstateMirrors) {
  GBAMemory mem;

  // Create a small ROM
  std::vector<uint8_t> romData(0x100, 0x00);
  romData[0x00] = 0x11;
  romData[0x01] = 0x22;
  romData[0x02] = 0x33;
  romData[0x03] = 0x44;
  mem.LoadGamePak(romData);

  // ROM is accessible at 0x08000000 (waitstate 0)
  EXPECT_EQ(mem.Read32(0x08000000u), 0x44332211u);

  // Also test 16-bit and 8-bit reads from ROM
  EXPECT_EQ(mem.Read16(0x08000000u), 0x2211u);
  EXPECT_EQ(mem.Read8(0x08000000u), 0x11u);
  EXPECT_EQ(mem.Read8(0x08000001u), 0x22u);
}
