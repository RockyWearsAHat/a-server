#include <gtest/gtest.h>

#include "emulator/gba/GBA.h"
#include "emulator/gba/GBAMemory.h"

using namespace AIO::Emulator::GBA;

TEST(GbaTiming, GBAMemoryHasSetGBAAndReadDispstat) {
  GBAMemory mem;

  // Setter exists and accepts a nullptr (compile-time / API verification)
  EXPECT_NO_THROW(mem.SetGBA(nullptr));

  // Smoke: reading DISPSTAT and VCOUNT should not crash (aligned reads)
  EXPECT_NO_THROW(mem.Read16(0x04000004u));
  EXPECT_NO_THROW(mem.Read16(0x04000006u));
}

TEST(GbaTiming, GbaConstructorWiresMemoryAndIOReads) {
  GBA gba;
  GBAMemory &mem = gba.GetMemory();

  // Constructor should have wired the memory (SetGBA called); reading regs is safe
  EXPECT_NO_THROW(mem.Read16(0x04000004u));
  EXPECT_NO_THROW(mem.Read16(0x04000006u));
}

