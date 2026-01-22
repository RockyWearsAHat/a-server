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
