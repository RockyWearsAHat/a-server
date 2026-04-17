#include <gtest/gtest.h>

#include "emulator/gba/APU.h"
#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/IORegs.h"

using namespace AIO::Emulator::GBA;

namespace {
inline void WriteIo32(GBAMemory &mem, uint32_t ioOffset, uint32_t value) {
  mem.Write32(IORegs::BASE + ioOffset, value);
}

inline void WriteIo16(GBAMemory &mem, uint32_t ioOffset, uint16_t value) {
  mem.Write16(IORegs::BASE + ioOffset, value);
}
} // namespace

TEST(DMATest, AlignsAddressesFor32BitTransfer) {
  GBAMemory mem;
  mem.Reset();

  // Source pattern at EWRAM base.
  mem.Write8(0x02000000, 0x11);
  mem.Write8(0x02000001, 0x22);
  mem.Write8(0x02000002, 0x33);
  mem.Write8(0x02000003, 0x44);

  // Clear destination region in VRAM.
  mem.Write8(0x06000000, 0x00);
  mem.Write8(0x06000001, 0x00);
  mem.Write8(0x06000002, 0x00);
  mem.Write8(0x06000003, 0x00);

  // Program DMA3 with intentionally unaligned addresses.
  // Hardware should align both to 32-bit boundaries.
  WriteIo32(mem, IORegs::DMA3SAD, 0x02000001); // unaligned
  WriteIo32(mem, IORegs::DMA3DAD, 0x06000002); // unaligned
  WriteIo16(mem, IORegs::DMA3CNT_L, 1);        // transfer 1 unit

  const uint16_t control = DMAControl::ENABLE | DMAControl::TRANSFER_32BIT |
                           DMAControl::START_IMMEDIATE;
  WriteIo16(mem, IORegs::DMA3CNT_H, control);

  EXPECT_EQ(mem.Read8(0x06000000), 0x11);
  EXPECT_EQ(mem.Read8(0x06000001), 0x22);
  EXPECT_EQ(mem.Read8(0x06000002), 0x33);
  EXPECT_EQ(mem.Read8(0x06000003), 0x44);

  // Destination should have been aligned down to 0x06000000.
  EXPECT_EQ(mem.Read32(0x06000000), 0x44332211u);
}

TEST(DMATest, AlignsAddressesFor16BitTransfer) {
  GBAMemory mem;
  mem.Reset();

  mem.Write8(0x02000000, 0xAA);
  mem.Write8(0x02000001, 0xBB);

  mem.Write8(0x06000000, 0x00);
  mem.Write8(0x06000001, 0x00);

  // Program DMA3 with unaligned 16-bit addresses.
  // Hardware should align both to halfword boundaries.
  WriteIo32(mem, IORegs::DMA3SAD, 0x02000001); // unaligned
  WriteIo32(mem, IORegs::DMA3DAD, 0x06000001); // unaligned
  WriteIo16(mem, IORegs::DMA3CNT_L, 1);

  const uint16_t control = DMAControl::ENABLE | DMAControl::START_IMMEDIATE;
  WriteIo16(mem, IORegs::DMA3CNT_H, control);

  // Halfword at 0x02000000 is 0xBBAA -> bytes AA, BB.
  EXPECT_EQ(mem.Read8(0x06000000), 0xAA);
  EXPECT_EQ(mem.Read8(0x06000001), 0xBB);
  EXPECT_EQ(mem.Read16(0x06000000), 0xBBAAu);
}

TEST(MemoryMapTest, VramUpperWindowMirrorsObjRegion) {
  GBAMemory mem;
  mem.Reset();

  // Seed different values so we can detect incorrect aliasing.
  mem.Write16(0x06000000, 0x1111);
  mem.Write16(0x06010000, 0x2222);

  // Write through the upper 32KB window.
  mem.Write16(0x06018000, 0xABCD);

  // On real hardware 0x06018000 mirrors to 0x06010000.
  EXPECT_EQ(mem.Read16(0x06010000), 0xABCDu);
  EXPECT_EQ(mem.Read16(0x06018000), 0xABCDu);

  // Ensure we did NOT clobber the BG base region.
  EXPECT_EQ(mem.Read16(0x06000000), 0x1111u);
}

TEST(MemoryMapTest, VramByteWritesAlsoAffectObjVram) {
  GBAMemory mem;

  // GBATEK: OBJ VRAM byte writes are ignored (VRAM is 16-bit, but OBJ region
  // does not accept 8-bit writes).
  const uint32_t objVramAddr = 0x06010001; // unaligned byte write in OBJ VRAM
  mem.Write8(objVramAddr, 0x7A);
  EXPECT_EQ(mem.Read16(0x06010000), 0x0000u);

  // Upper VRAM window mirrors into OBJ region; mirroring must not change the
  // byte-write ignore behavior.
  const uint32_t mirroredObjVramAddr = 0x06018001; // mirrors to 0x06010001
  mem.Write8(mirroredObjVramAddr, 0x3C);
  EXPECT_EQ(mem.Read16(0x06010000), 0x0000u);
}

// GBATEK: Sound DMA (DMA1/DMA2 with START_SPECIAL) fires only when the FIFO
// count drops to <= 16 samples (half-full). When the FIFO is well-stocked,
// timer overflows consume one sample each but do NOT trigger a DMA refill.
// This test verifies the FIFO-level gating using the APU FIFO count API.
TEST(AudioDmaTest, SoundFifoDmaNotTriggeredEveryTimerOverflow) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Enable master sound.
  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080);

  // Route FIFO A to both channels, full volume, Timer0.
  // Bit 2 = FIFO A vol 100%, bits 8/9 = R/L enable, bit 10 = Timer0.
  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0304);

  // Fill source memory with a recognisable pattern (64 bytes = 16 samples × 4).
  const uint32_t srcBase = 0x02000000u;
  for (int i = 0; i < 64; ++i)
    mem.Write8(srcBase + (uint32_t)i, (uint8_t)(0x10 + i));

  // Set up DMA1 as FIFO A sound DMA: dest-fixed, repeat, 32-bit, START_SPECIAL.
  WriteIo32(mem, IORegs::DMA1SAD, srcBase);
  WriteIo32(mem, IORegs::DMA1DAD, 0x040000A0u);
  WriteIo16(mem, IORegs::DMA1CNT_L, 4);
  const uint16_t dmaCtrl = DMAControl::ENABLE | DMAControl::REPEAT |
                           DMAControl::DEST_FIXED | DMAControl::TRANSFER_32BIT |
                           DMAControl::START_SPECIAL;
  WriteIo16(mem, IORegs::DMA1CNT_H, dmaCtrl);

  // Timer0: overflow every UpdateTimers(1) call (reload=0xFFFF, prescaler 1).
  mem.Write16(IORegs::BASE + IORegs::TM0CNT_L, 0xFFFF);
  mem.Write16(IORegs::BASE + IORegs::TM0CNT_H,
              TimerControl::ENABLE | TimerControl::PRESCALER_1);

  // Pre-fill FIFO A with 32 samples (max capacity) via WriteFIFO_A (4 bytes
  // each).  This places the FIFO above the 16-sample refill threshold.
  for (int i = 0; i < 8; ++i)
    apu.WriteFIFO_A(0x01020304u);
  ASSERT_EQ(apu.GetFifoACount(), 32);

  // Run 15 timer overflows.  Each overflow pops one sample.  After 15 pops the
  // FIFO holds 17 samples, which is still above the threshold (> 16).  DMA
  // must NOT fire during these 15 overflows.
  for (int n = 0; n < 15; ++n)
    mem.UpdateTimers(1);
  EXPECT_EQ(apu.GetFifoACount(), 17);

  // 16th overflow: FIFO drops from 17 to 16 (== threshold, <= 16 is true).
  // DMA fires and writes 4 words × 4 bytes = 16 samples into FIFO A.
  // Post-DMA count = 16 + 16 = 32.
  mem.UpdateTimers(1);
  EXPECT_EQ(apu.GetFifoACount(), 32);
}
// ============================================================================
// DMA Source Address Control Tests (per GBATEK)
// ============================================================================
// GBATEK: DMA_SAD/DAD control bits:
//   0 = Increment after each transfer
//   1 = Decrement after each transfer
//   2 = Fixed (source address doesn't change)
//   3 = Increment/Reload (DAD only)

TEST(DMATest, SourceControlFixed_ReadsFromSameAddressRepeatedly) {
  GBAMemory mem;
  mem.Reset();

  // Source pattern at EWRAM base - different values at each offset
  mem.Write32(0x02000000u, 0xAABBCCDDu);
  mem.Write32(0x02000004u, 0x11223344u);
  mem.Write32(0x02000008u, 0x55667788u);

  // Clear destination
  mem.Write32(0x06000000u, 0x00000000u);
  mem.Write32(0x06000004u, 0x00000000u);
  mem.Write32(0x06000008u, 0x00000000u);

  // DMA3: src=0x02000000, dst=0x06000000, count=3, srcCtrl=FIXED (2)
  WriteIo32(mem, IORegs::DMA3SAD, 0x02000000u);
  WriteIo32(mem, IORegs::DMA3DAD, 0x06000000u);
  WriteIo16(mem, IORegs::DMA3CNT_L, 3);

  // Control: Enable + 32-bit + Immediate + SrcFixed (bits 7-5 = 2)
  const uint16_t ctrl = DMAControl::ENABLE | DMAControl::TRANSFER_32BIT |
                        DMAControl::START_IMMEDIATE | (2u << 7);
  WriteIo16(mem, IORegs::DMA3CNT_H, ctrl);

  // With srcCtrl=Fixed, all 3 transfers read from 0x02000000
  EXPECT_EQ(mem.Read32(0x06000000u), 0xAABBCCDDu);
  EXPECT_EQ(mem.Read32(0x06000004u), 0xAABBCCDDu);
  EXPECT_EQ(mem.Read32(0x06000008u), 0xAABBCCDDu);
}
