#include <gtest/gtest.h>

#include "emulator/gba/APU.h"
#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/IORegs.h"

using namespace AIO::Emulator::GBA;

namespace {
inline void WriteIo32(GBAMemory& mem, uint32_t ioOffset, uint32_t value) {
    mem.Write32(IORegs::BASE + ioOffset, value);
}

inline void WriteIo16(GBAMemory& mem, uint32_t ioOffset, uint16_t value) {
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
    WriteIo16(mem, IORegs::DMA3CNT_L, 1); // transfer 1 unit

    const uint16_t control = DMAControl::ENABLE | DMAControl::TRANSFER_32BIT | DMAControl::START_IMMEDIATE;
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

    // Byte writes to VRAM are performed on a 16-bit bus; hardware duplicates the byte
    // into both halves of the aligned halfword. This should apply to OBJ VRAM too.
    const uint32_t objVramAddr = 0x06010001; // unaligned byte write in OBJ VRAM region
    mem.Write8(objVramAddr, 0x7A);

    // Should write 0x7A7A into the aligned halfword at 0x06010000.
    EXPECT_EQ(mem.Read16(0x06010000), 0x7A7A);

    // Upper VRAM window mirrors into OBJ region; ensure it behaves the same.
    const uint32_t mirroredObjVramAddr = 0x06018001; // mirrors to 0x06010001
    mem.Write8(mirroredObjVramAddr, 0x3C);
    EXPECT_EQ(mem.Read16(0x06010000), 0x3C3C);
}

TEST(AudioDmaTest, SoundFifoDmaNotTriggeredEveryTimerOverflow) {
    GBAMemory mem;
    APU apu(mem);
    mem.SetAPU(&apu);
    mem.Reset();
    apu.Reset();

    // Enable master sound.
    mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080);

    // Route FIFO A to both channels, full volume, and use Timer0.
    // Bits: 2=FIFO A full vol, 8/9 enable R/L, 10 timer select.
    uint16_t scntH = 0;
    scntH |= 0x0004;  // FIFO A volume 100%
    scntH |= 0x0100;  // FIFO A -> Right
    scntH |= 0x0200;  // FIFO A -> Left
    scntH |= 0x0000;  // FIFO A timer select = Timer0
    mem.Write16(IORegs::REG_SOUNDCNT_H, scntH);

    // Set up DMA1 as a typical sound DMA: src inc, dst fixed (FIFO A), repeat, 32-bit, start special.
    // Transfer count = 4 words (16 bytes) per request.
    const uint32_t srcBase = 0x02000000;
    for (int i = 0; i < 512; ++i) {
        mem.Write8(srcBase + (uint32_t)i, (uint8_t)(i & 0xFF));
    }

    WriteIo32(mem, IORegs::DMA1SAD, srcBase);
    WriteIo32(mem, IORegs::DMA1DAD, 0x040000A0);
    WriteIo16(mem, IORegs::DMA1CNT_L, 4);
    const uint16_t dmaCtrl = DMAControl::ENABLE | DMAControl::REPEAT | DMAControl::DEST_FIXED |
                             DMAControl::TRANSFER_32BIT | DMAControl::START_SPECIAL;
    WriteIo16(mem, IORegs::DMA1CNT_H, dmaCtrl);

    // Configure Timer0 to overflow every cycle (reload=0xFFFF) so we can stress the trigger logic.
    mem.Write16(IORegs::BASE + IORegs::TM0CNT_L, 0xFFFF);
    mem.Write16(IORegs::BASE + IORegs::TM0CNT_H, TimerControl::ENABLE | TimerControl::PRESCALER_1);

    // Run 20 overflows.
    for (int n = 0; n < 20; ++n) {
        mem.UpdateTimers(1);
    }

    // If DMA were triggered every overflow, the DMA1 source address would advance by 20 * 16 bytes = 320.
    // With FIFO-level gating, it should advance far less.
    const uint32_t sadAfter = mem.Read32(IORegs::BASE + IORegs::DMA1SAD);
    EXPECT_LT(sadAfter - srcBase, 160u);
}
