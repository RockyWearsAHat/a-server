#include <gtest/gtest.h>

#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/APU.h"
#include "emulator/gba/IORegs.h"

using namespace AIO::Emulator::GBA;

TEST(APUTest, FifoWriteIncrementsCount) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  EXPECT_EQ(apu.GetFifoACount(), 0);
  apu.WriteFIFO_A(0x11223344u);
  EXPECT_EQ(apu.GetFifoACount(), 4);

  apu.WriteFIFO_B(0x55667788u);
  EXPECT_EQ(apu.GetFifoBCount(), 4);
}

TEST(APUTest, FifoWriteViaMemoryWrite32) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  EXPECT_EQ(apu.GetFifoACount(), 0);
  mem.Write32(0x040000A0, 0x0A0B0C0Du);
  EXPECT_EQ(apu.GetFifoACount(), 4);
}

TEST(APUTest, FifoResetViaSOUNDCNT_H) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  apu.WriteFIFO_A(0x11223344u);
  apu.WriteFIFO_A(0x55667788u);
  EXPECT_GT(apu.GetFifoACount(), 0);

  // Reset FIFO A via SOUNDCNT_H write
  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0800);
  EXPECT_EQ(apu.GetFifoACount(), 0);

  // Reset FIFO B via SOUNDCNT_H write
  apu.WriteFIFO_B(0x11223344u);
  EXPECT_GT(apu.GetFifoBCount(), 0);
  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x8000);
  EXPECT_EQ(apu.GetFifoBCount(), 0);
}

TEST(APUTest, MasterSoundEnableReflectsSOUNDCNT_X) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0000);
  apu.Update(0);
  EXPECT_FALSE(apu.IsSoundEnabled());

  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080);
  apu.Update(0);
  EXPECT_TRUE(apu.IsSoundEnabled());
}

