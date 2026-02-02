#include <cmath>
#include <emulator/gba/APU.h>
#include <emulator/gba/GBAMemory.h>
#include <emulator/gba/IORegs.h>
#include <gtest/gtest.h>

using namespace AIO::Emulator::GBA;

// Tests for audio corruption bugs found in OG-DK, MZM, and MMBN

TEST(AudioCorruptionTest, UpsamplingRatioCalculatedCorrectly) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  apu.SetOutputSampleRate(48000.0f);

  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080);

  // Configure Timer 0 for ~18KHz
  // Timer reload = 65536 - 933 = 64603 (0xFC4B)
  // At 1x prescaler: 16777216 / 933 ≈ 17982 Hz
  mem.Write16(IORegs::TM0CNT_L, 0xFC4B);
  mem.Write16(IORegs::TM0CNT_H, 0x0080);

  // FIFO A uses Timer 0, enabled to both channels
  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0B04);

  // Fill FIFO A with test samples
  for (int i = 0; i < 8; ++i) {
    apu.WriteFIFO_A(0x10203040u);
  }

  EXPECT_GT(apu.GetFifoACount(), 0);

  // Trigger timer overflow
  apu.OnTimerOverflow(0);

  // After one overflow, FIFO should have consumed samples
  EXPECT_LT(apu.GetFifoACount(), 32);
}

TEST(AudioCorruptionTest, MultipleTimerOverflowsFillRingBuffer) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  apu.SetOutputSampleRate(48000.0f);

  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080);
  mem.Write16(IORegs::TM0CNT_L, 0xFC4B);
  mem.Write16(IORegs::TM0CNT_H, 0x0080);
  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0B04);

  // Fill FIFO A
  for (int i = 0; i < 8; ++i) {
    apu.WriteFIFO_A(0x10203040u);
  }

  // Simulate multiple timer overflows
  const int numOverflows = 100;
  for (int i = 0; i < numOverflows; ++i) {
    apu.OnTimerOverflow(0);
  }

  // GetSamples should return non-zero samples
  int16_t buffer[512];
  int samplesReturned = apu.GetSamples(buffer, 256);
  EXPECT_GT(samplesReturned, 0) << "Ring buffer should have samples after "
                                   "multiple timer overflows";
}

TEST(AudioCorruptionTest, UpsamplingPreventsFifoUnderflow) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  apu.SetOutputSampleRate(48000.0f);

  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080);

  // Configure Timer 0 for low sample rate (8KHz)
  // Timer reload = 65536 - 2097 = 63439 (0xF7EF)
  // At 1x prescaler: 16777216 / 2097 ≈ 8000 Hz
  mem.Write16(IORegs::TM0CNT_L, 0xF7EF);
  mem.Write16(IORegs::TM0CNT_H, 0x0080);

  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0B04);

  // Fill FIFO A continuously
  for (int i = 0; i < 8; ++i) {
    apu.WriteFIFO_A(0x20304050u);
  }

  int initialFifoCount = apu.GetFifoACount();

  // Simulate ~100 timer overflows at 8KHz
  // Expected upsample ratio: 48000/8000 = 6.0
  // Each overflow should push ~6 samples to ring buffer
  for (int i = 0; i < 100; ++i) {
    if (apu.GetFifoACount() < 16) {
      apu.WriteFIFO_A(0x20304050u);
    }
    apu.OnTimerOverflow(0);
  }

  // Check that ring buffer has accumulated samples
  int16_t buffer[1024];
  int samplesReturned = apu.GetSamples(buffer, 512);

  EXPECT_GT(samplesReturned, 400)
      << "Upsampling should produce ~6x samples (100 overflows * 6 ≈ 600)";
}

TEST(AudioCorruptionTest, NoAudioWhenMasterSoundDisabled) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  apu.SetOutputSampleRate(48000.0f);

  // Master sound DISABLED (bit 7 = 0)
  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0000);

  mem.Write16(IORegs::TM0CNT_L, 0xFC4B);
  mem.Write16(IORegs::TM0CNT_H, 0x0080);
  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0B04);

  for (int i = 0; i < 8; ++i) {
    apu.WriteFIFO_A(0x7F7F7F7Fu);
  }

  for (int i = 0; i < 50; ++i) {
    apu.OnTimerOverflow(0);
  }

  int16_t buffer[256];
  int samplesReturned = apu.GetSamples(buffer, 128);

  // All samples should be zero (silence)
  bool allZero = true;
  for (int i = 0; i < samplesReturned * 2; ++i) {
    if (buffer[i] != 0) {
      allZero = false;
      break;
    }
  }

  EXPECT_TRUE(allZero) << "Audio should be silent when master sound disabled";
}

TEST(AudioCorruptionTest, FifoAAndFifoBIndependent) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  apu.SetOutputSampleRate(48000.0f);

  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080);

  // FIFO A on Timer 0, FIFO B on Timer 1
  mem.Write16(IORegs::TM0CNT_L, 0xFC4B);
  mem.Write16(IORegs::TM0CNT_H, 0x0080);
  mem.Write16(IORegs::TM1CNT_L, 0xF830);
  mem.Write16(IORegs::TM1CNT_H, 0x0080);

  // Bits: 2=A vol, 3=B vol, 8-9=A R/L, 10=A timer, 12-13=B R/L, 14=B timer
  uint16_t scntH = 0;
  scntH |= 0x0004;
  scntH |= 0x0008;
  scntH |= 0x0100;
  scntH |= 0x0200;
  scntH |= 0x0000;
  scntH |= 0x1000;
  scntH |= 0x2000;
  scntH |= 0x4000;
  mem.Write16(IORegs::REG_SOUNDCNT_H, scntH);

  // Fill both FIFOs with different patterns
  for (int i = 0; i < 8; ++i) {
    apu.WriteFIFO_A(0x20202020u);
    apu.WriteFIFO_B(0x40404040u);
  }

  int initialA = apu.GetFifoACount();
  int initialB = apu.GetFifoBCount();

  // Trigger Timer 0 (FIFO A only)
  apu.OnTimerOverflow(0);

  int afterA = apu.GetFifoACount();
  int afterB = apu.GetFifoBCount();

  EXPECT_LT(afterA, initialA) << "FIFO A should consume on Timer 0";
  EXPECT_EQ(afterB, initialB) << "FIFO B should NOT consume on Timer 0";

  // Trigger Timer 1 (FIFO B only)
  apu.OnTimerOverflow(1);

  int finalA = apu.GetFifoACount();
  int finalB = apu.GetFifoBCount();

  EXPECT_EQ(finalA, afterA) << "FIFO A should NOT consume on Timer 1";
  EXPECT_LT(finalB, afterB) << "FIFO B should consume on Timer 1";
}

TEST(AudioCorruptionTest, StereoPanningWorks) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  apu.SetOutputSampleRate(48000.0f);

  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080);
  mem.Write16(IORegs::TM0CNT_L, 0xFC4B);
  mem.Write16(IORegs::TM0CNT_H, 0x0080);

  // FIFO A to LEFT only (bit 9=1, bit 8=0)
  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0204);

  for (int i = 0; i < 8; ++i) {
    apu.WriteFIFO_A(0x7F7F7F7Fu);
  }

  for (int i = 0; i < 10; ++i) {
    apu.OnTimerOverflow(0);
  }

  int16_t buffer[64];
  int samplesReturned = apu.GetSamples(buffer, 32);
  EXPECT_GT(samplesReturned, 0);

  // Check that left channel has signal, right channel is zero
  bool leftHasSignal = false;
  bool rightIsZero = true;

  for (int i = 0; i < samplesReturned; ++i) {
    int16_t left = buffer[i * 2];
    int16_t right = buffer[i * 2 + 1];

    if (left != 0)
      leftHasSignal = true;
    if (right != 0)
      rightIsZero = false;
  }

  EXPECT_TRUE(leftHasSignal) << "Left channel should have audio";
  EXPECT_TRUE(rightIsZero) << "Right channel should be silent";
}
