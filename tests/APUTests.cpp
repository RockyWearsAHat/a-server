#include <gtest/gtest.h>

#include "emulator/gba/APU.h"
#include "emulator/gba/GBAMemory.h"
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

TEST(APUTest, PSGSquareDutyAndFrequency) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Create a 8-sample period, duty 0 (1/8 high), volume max
  apu.SetPSGChannelParams(0, 8, 0, 15);
  auto s = apu.GeneratePSGSamples(0, 8);
  ASSERT_EQ(s.size(), 8);
  // only first sample high
  int highCount = 0;
  for (auto v : s)
    if (v > 0)
      ++highCount;
  EXPECT_EQ(highCount, 1);

  // Duty 2 should be half high
  apu.SetPSGChannelParams(0, 8, 2, 15);
  s = apu.GeneratePSGSamples(0, 8);
  highCount = 0;
  for (auto v : s)
    if (v > 0)
      ++highCount;
  EXPECT_EQ(highCount, 4);
}

TEST(APUTest, PSGVolumeScaling) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  apu.SetPSGChannelParams(1, 4, 2, 15);
  auto sMax = apu.GeneratePSGSamples(1, 4);

  apu.SetPSGChannelParams(1, 4, 2, 7);
  auto sHalf = apu.GeneratePSGSamples(1, 4);

  // Expect magnitudes roughly proportional (allowing integer rounding)
  int maxMag = 0;
  for (auto v : sMax)
    maxMag = std::max(maxMag, std::abs(v));
  int halfMag = 0;
  for (auto v : sHalf)
    halfMag = std::max(halfMag, std::abs(v));
  EXPECT_GT(maxMag, 0);
  EXPECT_GT(halfMag, 0);
  EXPECT_LT(halfMag, maxMag);
}
TEST(APUTest, PSGWavePlayback) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  std::array<uint8_t, 32> w;
  for (int i = 0; i < 32; ++i)
    w[i] = uint8_t(i % 16);
  apu.SetPSGWaveRAM(w);
  apu.SetPSGWaveParams(4, 0); // periodSamples=4, volume=0 (100%)
  auto s = apu.GeneratePSGSamples(2, 8);
  ASSERT_EQ(s.size(), 8);
  // first 4 samples equal, next 4 samples equal and different from first
  int firstEqual = 0;
  for (int i = 0; i < 4; ++i)
    if (s[i] == s[0])
      ++firstEqual;
  int secondEqual = 0;
  for (int i = 4; i < 8; ++i)
    if (s[i] == s[4])
      ++secondEqual;
  EXPECT_EQ(firstEqual, 4);
  EXPECT_EQ(secondEqual, 4);
  EXPECT_NE(s[0], s[4]);

  // volume scaling: half volume should have smaller magnitude
  apu.SetPSGWaveParams(4, 1); // 50%
  auto sHalf = apu.GeneratePSGSamples(2, 4);
  int magFull = 0, magHalf = 0;
  for (auto v : s)
    magFull = std::max(magFull, std::abs(v));
  for (auto v : sHalf)
    magHalf = std::max(magHalf, std::abs(v));
  EXPECT_GT(magFull, 0);
  EXPECT_GT(magHalf, 0);
  EXPECT_LT(magHalf, magFull);
}

TEST(APUTest, PSGNoiseModesDiffer) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();
  // Very fast toggling to exercise LFSR steps
  apu.SetPSGNoiseParams(1, false, 15);
  auto seqNormal = apu.GeneratePSGSamples(3, 32);
  apu.SetPSGNoiseParams(1, true, 15);
  auto seqShort = apu.GeneratePSGSamples(3, 32);
  // Sequences should not be identical
  bool equal = true;
  for (int i = 0; i < 32; ++i) {
    if (seqNormal[i] != seqShort[i]) {
      equal = false;
      break;
    }
  }
  EXPECT_FALSE(equal);
}

TEST(APUTest, PSGNoiseVolumeScaling) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();
  apu.SetPSGNoiseParams(1, false, 15);
  auto sFull = apu.GeneratePSGSamples(3, 16);
  apu.SetPSGNoiseParams(1, false, 7);
  auto sHalf = apu.GeneratePSGSamples(3, 16);
  int maxFull = 0;
  int maxHalf = 0;
  for (auto v : sFull)
    maxFull = std::max(maxFull, std::abs(v));
  for (auto v : sHalf)
    maxHalf = std::max(maxHalf, std::abs(v));
  EXPECT_GT(maxFull, 0);
  EXPECT_GT(maxHalf, 0);
  EXPECT_LT(maxHalf, maxFull);
}

TEST(APUTest, PSGNoiseProducesBothPolarities) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();
  apu.SetPSGNoiseParams(1, false, 15);
  auto s = apu.GeneratePSGSamples(3, 64);
  int pos = 0, neg = 0;
  for (auto v : s) {
    if (v > 0)
      ++pos;
    else if (v < 0)
      ++neg;
  }
  EXPECT_GT(pos, 0);
  EXPECT_GT(neg, 0);
}
