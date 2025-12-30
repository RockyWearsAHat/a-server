#include <gtest/gtest.h>

#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/IORegs.h"
#include "emulator/gba/PPU.h"

using namespace AIO::Emulator::GBA;

namespace {
inline void WriteIo32(GBAMemory &mem, uint32_t ioOffset, uint32_t value) {
  mem.Write32(IORegs::BASE + ioOffset, value);
}

inline void WriteIo16(GBAMemory &mem, uint32_t ioOffset, uint16_t value) {
  mem.Write16(IORegs::BASE + ioOffset, value);
}

inline uint16_t ReadIo16(GBAMemory &mem, uint32_t ioOffset) {
  return mem.Read16(IORegs::BASE + ioOffset);
}
} // namespace

TEST(DMAEdgeTest, VBlankRepeat_IncReloadReloadsDestinationEachFrame) {
  GBAMemory mem;
  mem.Reset();

  // Allow VRAM writes.
  mem.Write16(0x04000000u, 0x0080u);

  // Program DMA3: copy 1 halfword from src to VRAM each VBlank.
  const uint32_t srcBase = 0x02000000u;
  const uint32_t dstBase = 0x06000000u;
  mem.Write16(srcBase + 0u, 0x1111u);
  mem.Write16(srcBase + 2u, 0x2222u);
  mem.Write16(dstBase + 0u, 0x0000u);

  WriteIo32(mem, IORegs::DMA3SAD, srcBase);
  WriteIo32(mem, IORegs::DMA3DAD, dstBase);
  WriteIo16(mem, IORegs::DMA3CNT_L, 1);

  const uint16_t ctrl =
      (uint16_t)(DMAControl::ENABLE | DMAControl::REPEAT |
                 DMAControl::DEST_INCREMENT_RELOAD | DMAControl::START_VBLANK);
  WriteIo16(mem, IORegs::DMA3CNT_H, ctrl);

  // Run two frames. VBlank starts once per frame; should trigger DMA twice.
  PPU ppu(mem);
  ppu.Update(1232 * 228);
  ppu.Update(1232 * 228);

  // With dest inc/reload + repeat, destination reloads to DAD each trigger.
  // Source increments between triggers, so second trigger overwrites with
  // 0x2222.
  EXPECT_EQ(mem.Read16(dstBase), 0x2222u);

  // Enable should remain set (repeat DMA stays armed).
  EXPECT_NE((ReadIo16(mem, IORegs::DMA3CNT_H) & DMAControl::ENABLE), 0u);
}

TEST(DMAEdgeTest, HBlankRepeat_TriggersOnEachHBlank) {
  GBAMemory mem;
  mem.Reset();

  // Allow VRAM writes.
  mem.Write16(0x04000000u, 0x0080u);

  const uint32_t srcBase = 0x02000000u;
  const uint32_t dstBase = 0x06000000u;
  mem.Write16(srcBase + 0u, 0xAAAAu);
  mem.Write16(srcBase + 2u, 0xBBBBu);
  mem.Write16(dstBase + 0u, 0x0000u);
  mem.Write16(dstBase + 2u, 0x0000u);

  WriteIo32(mem, IORegs::DMA3SAD, srcBase);
  WriteIo32(mem, IORegs::DMA3DAD, dstBase);
  WriteIo16(mem, IORegs::DMA3CNT_L, 1);

  const uint16_t ctrl = (uint16_t)(DMAControl::ENABLE | DMAControl::REPEAT |
                                   DMAControl::START_HBLANK);
  WriteIo16(mem, IORegs::DMA3CNT_H, ctrl);

  // Run just enough to hit HBlank twice (scanline 0 and 1).
  PPU ppu(mem);
  // Scanline 0: reach HBlank start (960 cycles) => DMA triggers once.
  ppu.Update(960);
  // Finish scanline 0 and reach HBlank start of scanline 1.
  ppu.Update((1232 - 960) + 960);

  EXPECT_EQ(mem.Read16(dstBase + 0u), 0xAAAAu);
  EXPECT_EQ(mem.Read16(dstBase + 2u), 0xBBBBu);
}

TEST(DMAEdgeTest, ImmediateDma_IrqSetsIFAndClearsEnableAndCount) {
  GBAMemory mem;
  mem.Reset();

  // Allow VRAM writes.
  mem.Write16(0x04000000u, 0x0080u);

  const uint32_t srcBase = 0x02000000u;
  const uint32_t dstBase = 0x06000000u;
  mem.Write16(srcBase + 0u, 0x1234u);

  WriteIo32(mem, IORegs::DMA3SAD, srcBase);
  WriteIo32(mem, IORegs::DMA3DAD, dstBase);
  WriteIo16(mem, IORegs::DMA3CNT_L, 1);

  const uint16_t ctrl = (uint16_t)(DMAControl::ENABLE | DMAControl::IRQ_ENABLE |
                                   DMAControl::START_IMMEDIATE);
  WriteIo16(mem, IORegs::DMA3CNT_H, ctrl);

  EXPECT_EQ(mem.Read16(dstBase), 0x1234u);

  const uint16_t ifReg = mem.Read16(IORegs::REG_IF);
  EXPECT_NE((ifReg & InterruptFlags::DMA3), 0u);

  // Immediate DMA clears enable and count.
  EXPECT_EQ((ReadIo16(mem, IORegs::DMA3CNT_H) & DMAControl::ENABLE), 0u);
  EXPECT_EQ(ReadIo16(mem, IORegs::DMA3CNT_L), 0u);
}
