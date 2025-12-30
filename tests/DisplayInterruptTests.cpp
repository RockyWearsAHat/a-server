#include <gtest/gtest.h>

#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/IORegs.h"
#include "emulator/gba/PPU.h"

using namespace AIO::Emulator::GBA;

namespace {
constexpr int kCyclesPerScanline = 1232;

uint16_t ReadDispstat(GBAMemory &mem) {
  return mem.Read16(IORegs::REG_DISPSTAT);
}
uint16_t ReadVcount(GBAMemory &mem) { return mem.Read16(IORegs::REG_VCOUNT); }
} // namespace

TEST(DisplayInterruptTest, Dispstat_VCountFlagSetsOnMatchAndClearsOtherwise) {
  GBAMemory mem;
  mem.Reset();
  PPU ppu(mem);

  // Set VCOUNT compare value to 5 (DISPSTAT bits 8-15).
  const uint16_t dispstat = (uint16_t)(5u << 8);
  mem.Write16(IORegs::REG_DISPSTAT, dispstat);

  // Advance to scanline 5 (end-of-line updates VCOUNT/DISPSTAT).
  ppu.Update(kCyclesPerScanline * 5);

  EXPECT_EQ(ReadVcount(mem), 5u);
  EXPECT_NE((ReadDispstat(mem) & 0x0004u), 0u); // VCOUNT match flag

  // Advance to scanline 6 => should clear match flag.
  ppu.Update(kCyclesPerScanline);
  EXPECT_EQ(ReadVcount(mem), 6u);
  EXPECT_EQ((ReadDispstat(mem) & 0x0004u), 0u);
}

TEST(DisplayInterruptTest, Dispstat_VCountIrqSetsIFOnMatchWhenEnabled) {
  GBAMemory mem;
  mem.Reset();
  PPU ppu(mem);

  // VCOUNT compare=3, enable VCOUNT IRQ (bit 5).
  mem.Write16(IORegs::REG_DISPSTAT, (uint16_t)((3u << 8) | 0x0020u));

  // Move to scanline 3, which should set IF.VCOUNT.
  ppu.Update(kCyclesPerScanline * 3);

  EXPECT_EQ(ReadVcount(mem), 3u);
  EXPECT_NE((ReadDispstat(mem) & 0x0004u), 0u);

  const uint16_t ifReg = mem.Read16(IORegs::REG_IF);
  EXPECT_NE((ifReg & InterruptFlags::VCOUNT), 0u);
}

TEST(DisplayInterruptTest, Dispstat_VCountIrqDoesNotSetIFWhenDisabled) {
  GBAMemory mem;
  mem.Reset();
  PPU ppu(mem);

  // VCOUNT compare=3, VCOUNT IRQ disabled.
  mem.Write16(IORegs::REG_DISPSTAT, (uint16_t)(3u << 8));

  ppu.Update(kCyclesPerScanline * 3);

  const uint16_t ifReg = mem.Read16(IORegs::REG_IF);
  EXPECT_EQ((ifReg & InterruptFlags::VCOUNT), 0u);
}
