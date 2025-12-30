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

TEST(MemoryMapTest, IwramDoesNotAliasArbitrary03RegionAddresses) {
  GBAMemory mem;
  mem.Reset();

  // GBATEK: IWRAM is mapped at 0x03000000-0x03007FFF.
  // Only the last 32KB of the 0x03xxxxxx region (0x03FF8000-0x03FFFFFF)
  // mirrors the 32KB chip RAM. Arbitrary 0x03xxxxxx addresses do not alias.
  constexpr uint32_t kBase = 0x03001234u;
  constexpr uint32_t kAliasLike = 0x03009234u;

  mem.Write32(kBase, 0x33222223u);
  EXPECT_EQ(mem.Read32(kBase), 0x33222223u);
  EXPECT_EQ(mem.Read32(kAliasLike), 0u);

  mem.Write32(kAliasLike, 0xA5A5A5A5u);
  EXPECT_EQ(mem.Read32(kBase), 0x33222223u);
  EXPECT_EQ(mem.Read32(kAliasLike), 0u);
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

TEST(MemoryMapTest, PaletteByteWritesDuplicateToHalfword) {
  GBAMemory mem;
  mem.Reset();

  // Allow Palette writes during this unit test.
  mem.Write16(0x04000000u, 0x0080u);

  // GBATEK: Palette RAM is a 16-bit bus; byte writes store the same byte into
  // both halves of the addressed halfword.
  mem.Write8(0x05000001u, 0x7Au);
  EXPECT_EQ(mem.Read16(0x05000000u), 0x7A7Au);

  mem.Write8(0x05000000u, 0x3Cu);
  EXPECT_EQ(mem.Read16(0x05000000u), 0x3C3Cu);
}

TEST(MemoryMapTest, OamByteWritesAreIgnored) {
  GBAMemory mem;
  mem.Reset();

  // Allow OAM setup writes during this unit test.
  mem.Write16(0x04000000u, 0x0080u);

  // Seed OAM and ensure byte writes do not modify it.
  mem.Write16(0x07000000u, 0x1234u);
  EXPECT_EQ(mem.Read16(0x07000000u), 0x1234u);

  mem.Write8(0x07000000u, 0xFFu);
  mem.Write8(0x07000001u, 0xEEu);
  EXPECT_EQ(mem.Read16(0x07000000u), 0x1234u);
}
