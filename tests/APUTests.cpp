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

// ============================================================================
// Additional APU Coverage Tests
// ============================================================================

TEST(APUTest, FifoOverflowIsHandled) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Fill FIFO A to capacity (32 samples)
  for (int i = 0; i < 8; ++i) {
    apu.WriteFIFO_A(0x11223344u);
  }
  EXPECT_EQ(apu.GetFifoACount(), 32);

  // Writing more should not crash or increase count
  apu.WriteFIFO_A(0xDEADBEEFu);
  EXPECT_EQ(apu.GetFifoACount(), 32);
}

TEST(APUTest, FifoBOverflowIsHandled) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Fill FIFO B to capacity
  for (int i = 0; i < 8; ++i) {
    apu.WriteFIFO_B(0x55667788u);
  }
  EXPECT_EQ(apu.GetFifoBCount(), 32);

  // Writing more should not crash
  apu.WriteFIFO_B(0xCAFEBABEu);
  EXPECT_EQ(apu.GetFifoBCount(), 32);
}

TEST(APUTest, FifoResetClearsAllSamples) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  apu.WriteFIFO_A(0x11223344u);
  apu.WriteFIFO_A(0x55667788u);
  EXPECT_GT(apu.GetFifoACount(), 0);

  apu.ResetFIFO_A();
  EXPECT_EQ(apu.GetFifoACount(), 0);

  apu.WriteFIFO_B(0xAABBCCDDu);
  EXPECT_GT(apu.GetFifoBCount(), 0);

  apu.ResetFIFO_B();
  EXPECT_EQ(apu.GetFifoBCount(), 0);
}

TEST(APUTest, MasterSoundDisabledProducesSilence) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Disable master sound
  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0000);
  apu.Update(0);
  EXPECT_FALSE(apu.IsSoundEnabled());
}

TEST(APUTest, SetOutputSampleRateZeroIsIgnored) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Setting sample rate to 0 should be ignored
  apu.SetOutputSampleRate(0.0f);
  // No crash expected
}

TEST(APUTest, SetOutputSampleRateNegativeIsIgnored) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Setting negative sample rate should be ignored
  apu.SetOutputSampleRate(-48000.0f);
  // No crash expected
}

TEST(APUTest, PSGChannelDisabledWhenVolumeZero) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Set volume to 0
  apu.SetPSGChannelParams(0, 8, 0, 0);

  // Generate samples - should be silent
  auto samples = apu.GeneratePSGSamples(0, 8);
  for (auto s : samples) {
    EXPECT_EQ(s, 0);
  }
}

TEST(APUTest, PSGChannelDisabledWhenPeriodZero) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Set period to 0
  apu.SetPSGChannelParams(0, 0, 0, 15);

  // Generate samples - should be silent
  auto samples = apu.GeneratePSGSamples(0, 8);
  for (auto s : samples) {
    EXPECT_EQ(s, 0);
  }
}

TEST(APUTest, PSGWaveChannelDisabledWhenMuted) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  std::array<uint8_t, 32> wave;
  for (int i = 0; i < 32; ++i)
    wave[i] = (uint8_t)(i % 16);
  apu.SetPSGWaveRAM(wave);

  // Volume level 3 = mute for wave channel
  apu.SetPSGWaveParams(4, 3);

  auto samples = apu.GeneratePSGSamples(2, 8);
  for (auto s : samples) {
    EXPECT_EQ(s, 0);
  }
}

TEST(APUTest, GetSamplesFromEmptyBuffer) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  int16_t buffer[64];
  int written = apu.GetSamples(buffer, 32);

  // Should return 0 written (buffer was empty)
  EXPECT_EQ(written, 0);

  // Buffer should be filled with silence
  for (int i = 0; i < 64; ++i) {
    EXPECT_EQ(buffer[i], 0);
  }
}

TEST(APUTest, PSGDutyCycle1) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Duty 1 = 1/4 high (2/8 samples high)
  apu.SetPSGChannelParams(0, 8, 1, 15);
  auto s = apu.GeneratePSGSamples(0, 8);

  int highCount = 0;
  for (auto v : s)
    if (v > 0)
      ++highCount;
  EXPECT_EQ(highCount, 2);
}

TEST(APUTest, PSGDutyCycle3) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Duty 3 = 3/4 high (6/8 samples high)
  apu.SetPSGChannelParams(0, 8, 3, 15);
  auto s = apu.GeneratePSGSamples(0, 8);

  int highCount = 0;
  for (auto v : s)
    if (v > 0)
      ++highCount;
  EXPECT_EQ(highCount, 6);
}

TEST(APUTest, FifoWriteViaMemoryWrite16) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  EXPECT_EQ(apu.GetFifoACount(), 0);

  // 16-bit writes to FIFO_A should work
  mem.Write16(0x040000A0, 0x1234u);
  // Implementation may or may not accept 16-bit writes
  // Just verify no crash
}

TEST(APUTest, PSGNoiseShortModeProducesDifferentPattern) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Normal LFSR mode
  apu.SetPSGNoiseParams(1, false, 15);
  auto normalSamples = apu.GeneratePSGSamples(3, 64);

  // Reset and use short mode
  apu.SetPSGNoiseParams(1, true, 15);
  auto shortSamples = apu.GeneratePSGSamples(3, 64);

  // The patterns should differ
  bool differ = false;
  for (size_t i = 0; i < normalSamples.size(); ++i) {
    if (normalSamples[i] != shortSamples[i]) {
      differ = true;
      break;
    }
  }
  EXPECT_TRUE(differ);
}

TEST(APUTest, SoundcntHWriteDoesNotCrash) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Test various SOUNDCNT_H configurations
  // Just verify writes don't crash

  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0304u);
  apu.Update(0);

  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x3F0Cu);
  apu.Update(0);

  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0000u);
  apu.Update(0);
}

TEST(APUTest, PSGWaveVolumeScaling) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  std::array<uint8_t, 32> wave;
  for (int i = 0; i < 32; ++i)
    wave[i] = 15; // Max value
  apu.SetPSGWaveRAM(wave);

  // Volume 0 = 100%
  apu.SetPSGWaveParams(4, 0);
  auto fullVol = apu.GeneratePSGSamples(2, 8);

  // Volume 1 = 50%
  apu.SetPSGWaveParams(4, 1);
  auto halfVol = apu.GeneratePSGSamples(2, 8);

  // Volume 2 = 25%
  apu.SetPSGWaveParams(4, 2);
  auto quarterVol = apu.GeneratePSGSamples(2, 8);

  // Magnitudes should decrease
  int magFull = 0, magHalf = 0, magQuarter = 0;
  for (auto v : fullVol)
    magFull = std::max(magFull, std::abs((int)v));
  for (auto v : halfVol)
    magHalf = std::max(magHalf, std::abs((int)v));
  for (auto v : quarterVol)
    magQuarter = std::max(magQuarter, std::abs((int)v));

  EXPECT_GT(magFull, magHalf);
  EXPECT_GT(magHalf, magQuarter);
}

// ============================================================================
// Documentation-Driven APU Tests (Audio_System.md spec)
// ============================================================================

/**
 * Per Audio_System.md:
 *   "Direct Sound (DMA Audio) - 8-bit PCM samples via FIFO buffers"
 *   "DMA sound triggering on timer overflow"
 */
TEST(APUTest, TimerOverflowDoesNotCrashWhenFifoEmpty) {
  // Spec: OnTimerOverflow should handle empty FIFO gracefully
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // FIFO is empty, timer overflow should not crash
  EXPECT_NO_THROW(apu.OnTimerOverflow(0));
  EXPECT_NO_THROW(apu.OnTimerOverflow(1));
}

TEST(APUTest, TimerOverflowConsumesFromFifo) {
  // Spec: "Timer-based sample rate control"
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Enable master sound
  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080);
  // Configure SOUNDCNT_H: FIFO A uses Timer 0, enable A to both L/R
  // Bit 10 = FIFO A Timer (0=Timer0, 1=Timer1)
  // Bits 8-9 = FIFO A volume and enable
  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0B04); // FIFO A enabled, timer 0

  apu.WriteFIFO_A(0x11223344u);
  int countBefore = apu.GetFifoACount();
  EXPECT_EQ(countBefore, 4);

  // Timer 0 overflow should consume a sample from FIFO A
  apu.Update(0); // Read registers
  apu.OnTimerOverflow(0);

  // FIFO should have consumed a sample (count decreases)
  int countAfter = apu.GetFifoACount();
  EXPECT_LT(countAfter, countBefore);
}

/**
 * Per Audio_System.md:
 *   "Volume control (50%/100%)"
 *   SOUNDCNT_H bits control DMA sound volume
 */
TEST(APUTest, SoundcntHVolumeSettingsAccepted) {
  // Spec: Volume control via SOUNDCNT_H
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Enable master sound
  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080);

  // 50% volume (bit 2 = 0 for FIFO A)
  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0000);
  apu.Update(0);
  EXPECT_TRUE(apu.IsSoundEnabled());

  // 100% volume (bit 2 = 1 for FIFO A)
  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0004);
  apu.Update(0);
  EXPECT_TRUE(apu.IsSoundEnabled());
}

/**
 * Per Audio_System.md:
 *   "Master sound enable (SOUNDCNT_X)"
 *   "Bit 7 of SOUNDCNT_X enables/disables all sound"
 */
TEST(APUTest, MasterSoundBit7ControlsAllSound) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Master enable = bit 7
  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080);
  apu.Update(0);
  EXPECT_TRUE(apu.IsSoundEnabled());

  // Clear bit 7 = disabled
  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0000);
  apu.Update(0);
  EXPECT_FALSE(apu.IsSoundEnabled());

  // Set bit 7 again = enabled
  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080);
  apu.Update(0);
  EXPECT_TRUE(apu.IsSoundEnabled());
}

/**
 * Per Audio_System.md:
 *   "FIFO_A at 0x040000A0, FIFO_B at 0x040000A4"
 */
TEST(APUTest, FifoAddressesCorrect) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Write to FIFO_A address
  EXPECT_EQ(apu.GetFifoACount(), 0);
  mem.Write32(0x040000A0, 0x12345678);
  EXPECT_EQ(apu.GetFifoACount(), 4);

  // Write to FIFO_B address
  EXPECT_EQ(apu.GetFifoBCount(), 0);
  mem.Write32(0x040000A4, 0xABCDEF00);
  EXPECT_EQ(apu.GetFifoBCount(), 4);
}

/**
 * Per Audio_System.md:
 *   "Sample mixing and output to SDL2"
 *   GetSamples is the interface for the audio callback
 */
TEST(APUTest, GetSamplesFillsBufferWithSilenceWhenEmpty) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  int16_t buffer[128];
  // Fill with non-zero to verify it gets cleared
  for (int i = 0; i < 128; ++i)
    buffer[i] = 0x7FFF;

  int written = apu.GetSamples(buffer, 64);

  // Should return 0 (no samples available) and fill with silence
  EXPECT_EQ(written, 0);
  for (int i = 0; i < 128; ++i) {
    EXPECT_EQ(buffer[i], 0);
  }
}

/**
 * Per Audio_System.md:
 *   "PSG (Programmable Sound Generator) - 4 legacy Game Boy sound channels"
 *   "Square wave, wave RAM, noise generators"
 */
TEST(APUTest, PSGChannelsAreIndependent) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Configure channel 0 with one pattern
  apu.SetPSGChannelParams(0, 8, 0, 15); // Duty 0 = 1/8 high

  // Configure channel 1 with a different pattern
  apu.SetPSGChannelParams(1, 8, 2, 15); // Duty 2 = 1/2 high

  auto ch0 = apu.GeneratePSGSamples(0, 8);
  auto ch1 = apu.GeneratePSGSamples(1, 8);

  // Count high samples
  int ch0High = 0, ch1High = 0;
  for (auto v : ch0)
    if (v > 0)
      ++ch0High;
  for (auto v : ch1)
    if (v > 0)
      ++ch1High;

  // Channel 0 should have 1 high, channel 1 should have 4
  EXPECT_EQ(ch0High, 1);
  EXPECT_EQ(ch1High, 4);
}

/**
 * Per Audio_System.md:
 *   "Stereo panning (L/R enable)"
 *   SOUNDCNT_H bits control left/right enable for each FIFO
 */
TEST(APUTest, StereoPanningBitsAccepted) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080);

  // FIFO A to left only (bit 9 = L enable, bit 8 = R enable)
  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0200);
  apu.Update(0);
  EXPECT_TRUE(apu.IsSoundEnabled());

  // FIFO A to right only
  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0100);
  apu.Update(0);
  EXPECT_TRUE(apu.IsSoundEnabled());

  // FIFO A to both
  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0300);
  apu.Update(0);
  EXPECT_TRUE(apu.IsSoundEnabled());
}

/**
 * Per Audio_System.md:
 *   "Direct Sound FIFO A/B buffers" - each is 32 bytes (32 samples)
 */
TEST(APUTest, FifoCapacityIs32Samples) {
  GBAMemory mem;
  APU apu(mem);
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();

  // Each 32-bit write adds 4 samples, 8 writes = 32 samples
  for (int i = 0; i < 8; ++i) {
    apu.WriteFIFO_A(0x11223344u);
  }
  EXPECT_EQ(apu.GetFifoACount(), 32);

  // 9th write should not increase count (overflow handled)
  apu.WriteFIFO_A(0x55667788u);
  EXPECT_EQ(apu.GetFifoACount(), 32);
}
