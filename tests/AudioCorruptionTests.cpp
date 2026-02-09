#include <cmath>
#include <cstdint>
#include <emulator/gba/APU.h>
#include <emulator/gba/GBAMemory.h>
#include <emulator/gba/IORegs.h>
#include <gtest/gtest.h>
#include <vector>

using namespace AIO::Emulator::GBA;

// Helper: set up a standard M4A-style audio configuration
static void SetupM4AAudio(GBAMemory &mem, APU &apu,
                          float outputRate = 32768.0f) {
  mem.SetAPU(&apu);
  mem.Reset();
  apu.Reset();
  apu.ClearRingBuffer();
  apu.SetOutputSampleRate(outputRate);

  // Master sound enable
  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080);

  // M4A standard timer: reload=0xFBE8, prescaler F/1
  mem.Write16(IORegs::REG_TM0CNT_L, 0xFBE8);
  mem.Write16(IORegs::REG_TM0CNT_H, 0x0080);

  // FIFO A on Timer 0, 100% volume, both channels
  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0304);
}

// M4A timer interval: reload=0xFBE8, prescaler 1 → 0x10000 - 0xFBE8 = 1048
static constexpr int M4A_TIMER_CYCLES = 1048;

// Simulate a timer overflow followed by the CPU cycles that would elapse.
// This mirrors real hardware: timer fires → APU consumes FIFO → CPU runs
// for one timer period → Update() generates output at the fixed sample rate.
static void SimulateTimerOverflow(APU &apu, int timer = 0,
                                  int cpuCycles = M4A_TIMER_CYCLES) {
  apu.OnTimerOverflow(timer);
  apu.Update(cpuCycles);
}

// Helper: drain ring buffer and return all samples as vector of stereo pairs
static std::vector<int16_t> DrainRingBuffer(APU &apu, int maxSamples = 4096) {
  std::vector<int16_t> result;
  result.resize(maxSamples * 2);
  int got = apu.GetSamples(result.data(), maxSamples);
  result.resize(got * 2);
  return result;
}

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
  mem.Write16(IORegs::REG_TM0CNT_L, 0xFC4B);
  mem.Write16(IORegs::REG_TM0CNT_H, 0x0080);

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
  mem.Write16(IORegs::REG_TM0CNT_L, 0xFC4B);
  mem.Write16(IORegs::REG_TM0CNT_H, 0x0080);
  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0B04);

  // Fill FIFO A
  for (int i = 0; i < 8; ++i) {
    apu.WriteFIFO_A(0x10203040u);
  }

  // Timer reload 0xFC4B = 64587, interval = 949 cycles
  const int timerCycles = 949;
  const int numOverflows = 100;
  for (int i = 0; i < numOverflows; ++i) {
    SimulateTimerOverflow(apu, 0, timerCycles);
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
  mem.Write16(IORegs::REG_TM0CNT_L, 0xF7EF);
  mem.Write16(IORegs::REG_TM0CNT_H, 0x0080);

  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0B04);

  // Fill FIFO A continuously
  for (int i = 0; i < 8; ++i) {
    apu.WriteFIFO_A(0x20304050u);
  }

  int initialFifoCount = apu.GetFifoACount();

  // Timer reload 0xF7EF = 63471, interval = 2065 cycles
  const int timerCycles = 2065;

  // Simulate ~100 timer overflows at 8KHz
  // Expected upsample ratio: 48000/8000 = 6.0
  // Each overflow should push ~6 samples to ring buffer
  for (int i = 0; i < 100; ++i) {
    if (apu.GetFifoACount() < 16) {
      apu.WriteFIFO_A(0x20304050u);
    }
    SimulateTimerOverflow(apu, 0, timerCycles);
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

  mem.Write16(IORegs::REG_TM0CNT_L, 0xFC4B);
  mem.Write16(IORegs::REG_TM0CNT_H, 0x0080);
  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0B04);

  for (int i = 0; i < 8; ++i) {
    apu.WriteFIFO_A(0x7F7F7F7Fu);
  }

  // Timer reload 0xFC4B, interval = 949 cycles
  for (int i = 0; i < 50; ++i) {
    SimulateTimerOverflow(apu, 0, 949);
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
  mem.Write16(IORegs::BASE + IORegs::TM0CNT_L, 0xFC4B);
  mem.Write16(IORegs::BASE + IORegs::TM0CNT_H, 0x0080);
  mem.Write16(IORegs::BASE + IORegs::TM1CNT_L, 0xF830);
  mem.Write16(IORegs::BASE + IORegs::TM1CNT_H, 0x0080);

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
  apu.ClearRingBuffer(); // Clear prefill for this test

  apu.SetOutputSampleRate(48000.0f);

  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080);
  mem.Write16(IORegs::REG_TM0CNT_L, 0xFC4B);
  mem.Write16(IORegs::REG_TM0CNT_H, 0x0080);

  // FIFO A to LEFT only (bit 9=1, bit 8=0)
  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0204);

  for (int i = 0; i < 8; ++i) {
    apu.WriteFIFO_A(0x7F7F7F7Fu);
  }

  // Timer reload 0xFC4B, interval = 949 cycles
  for (int i = 0; i < 10; ++i) {
    SimulateTimerOverflow(apu, 0, 949);
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
// ==================================================================
// BUG FIX TESTS: Sound disable/re-enable, FIFO underflow, overflow
// ==================================================================

// When master sound is disabled and then re-enabled, the output
// should resume without a large pop/discontinuity.
TEST(AudioCorruptionTest, HpfRunsDuringSoundDisable) {
  GBAMemory mem;
  APU apu(mem);
  SetupM4AAudio(mem, apu);

  // Push some non-zero FIFO samples to build up HPF cap state
  for (int i = 0; i < 8; ++i)
    apu.WriteFIFO_A(0x7F7F7F7Fu);
  for (int i = 0; i < 20; ++i)
    SimulateTimerOverflow(apu);

  auto beforeDisable = DrainRingBuffer(apu);
  ASSERT_GT(beforeDisable.size(), 0u);

  // Disable master sound
  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0000);

  // Generate more samples with sound disabled — HPF should still run
  for (int i = 0; i < 20; ++i)
    SimulateTimerOverflow(apu);
  auto duringSilence = DrainRingBuffer(apu);

  // Re-enable master sound with fresh FIFO data
  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080);
  for (int i = 0; i < 8; ++i)
    apu.WriteFIFO_A(0x01010101u);
  for (int i = 0; i < 5; ++i)
    SimulateTimerOverflow(apu);
  auto afterReenable = DrainRingBuffer(apu);

  // The first sample after re-enable should NOT have a huge discontinuity.
  // If HPF cap was stale, the first output would be a large pop.
  bool hasLargePop = false;
  for (size_t i = 2; i < afterReenable.size(); i += 2) {
    int32_t delta = std::abs(afterReenable[i] - afterReenable[i - 2]);
    if (delta > 2000) {
      hasLargePop = true;
      break;
    }
  }
  EXPECT_FALSE(hasLargePop)
      << "Re-enabling sound should not produce a large pop";
}

// FIFO underflow should hold the last sample value, not snap to zero.
// Snapping to zero causes a DC jump that produces an audible pop.
TEST(AudioCorruptionTest, FifoUnderflowHoldsLastSample) {
  GBAMemory mem;
  APU apu(mem);
  SetupM4AAudio(mem, apu);

  // Fill FIFO with known non-zero value
  apu.WriteFIFO_A(0x40404040u);
  ASSERT_EQ(apu.GetFifoACount(), 4);

  // Consume all 4 samples
  for (int i = 0; i < 4; ++i)
    SimulateTimerOverflow(apu);
  EXPECT_EQ(apu.GetFifoACount(), 0);

  // Drain output so far
  DrainRingBuffer(apu);

  // Trigger more overflows with empty FIFO — should hold last sample
  for (int i = 0; i < 10; ++i)
    SimulateTimerOverflow(apu);

  auto samples = DrainRingBuffer(apu);
  ASSERT_GT(samples.size(), 0u);

  // With held sample (0x40 = 64 signed) and FIFO A 100% vol,
  // the mix should continue at a steady level — no pop.
  // Check max sample-to-sample delta is bounded
  int maxDelta = 0;
  for (size_t i = 2; i < samples.size(); i += 2) {
    int d = std::abs((int)samples[i] - (int)samples[i - 2]);
    maxDelta = std::max(maxDelta, d);
  }
  EXPECT_LT(maxDelta, 1000)
      << "FIFO underflow should not cause a large discontinuity";
}

// With large FIFO values, the mix can swing widely. Verify that
// abrupt polarity changes don't produce int16 wrap-around.
TEST(AudioCorruptionTest, HpfNoInt16Overflow) {
  GBAMemory mem;
  APU apu(mem);
  SetupM4AAudio(mem, apu);

  // Push maximum positive FIFO samples
  for (int i = 0; i < 8; ++i)
    apu.WriteFIFO_A(0x7F7F7F7Fu);
  for (int i = 0; i < 30; ++i)
    SimulateTimerOverflow(apu);
  DrainRingBuffer(apu);

  // Abrupt switch to maximum negative
  for (int i = 0; i < 8; ++i)
    apu.WriteFIFO_A(0x80808080u);
  for (int i = 0; i < 30; ++i)
    SimulateTimerOverflow(apu);

  auto samples = DrainRingBuffer(apu);
  ASSERT_GT(samples.size(), 4u);

  // With SOUNDBIAS-based gain (~48×), legitimate FIFO output can
  // reach ±24k. Check that successive samples don't show sign-flip
  // discontinuities characteristic of int16 wrap-around.
  bool hasWrap = false;
  for (size_t i = 2; i < samples.size(); i += 2) {
    int16_t prev = samples[i - 2];
    int16_t curr = samples[i];
    // A sudden polarity flip with both values near the rails
    // indicates arithmetic overflow (e.g. +32000 → -32000).
    if (std::abs(prev) > 30000 && std::abs(curr) > 30000 &&
        ((prev > 0) != (curr > 0))) {
      hasWrap = true;
      break;
    }
  }
  EXPECT_FALSE(hasWrap)
      << "Audio pipeline should not produce int16 overflow/wrap";
}

// Fixed-rate output precision: over many timer intervals, the total
// output sample count should match totalCpuCycles / cyclesPerSample.
TEST(AudioCorruptionTest, UpsampleAccumulatorPrecision) {
  GBAMemory mem;
  APU apu(mem);
  SetupM4AAudio(mem, apu);

  for (int i = 0; i < 8; ++i)
    apu.WriteFIFO_A(0x10101010u);

  const int numOverflows = 1000;
  for (int i = 0; i < numOverflows; ++i) {
    if (apu.GetFifoACount() < 8)
      apu.WriteFIFO_A(0x10101010u);
    SimulateTimerOverflow(apu);
  }

  auto samples = DrainRingBuffer(apu, 8192);
  int sampleCount = static_cast<int>(samples.size()) / 2;

  // Total CPU cycles = 1000 * 1048 = 1,048,000
  // cyclesPerSample = 16777216 / 32768 = 512
  // Expected: 1,048,000 / 512 ≈ 2047 samples
  EXPECT_NEAR(sampleCount, 2047, 10)
      << "Fixed-rate output should produce precise sample count";
}

// After FIFO underflow + refill, audio should resume smoothly
// without a pop. This tests the MMBN ~200ms periodic pop scenario.
TEST(AudioCorruptionTest, FifoRefillAfterUnderflowIsSmooth) {
  GBAMemory mem;
  APU apu(mem);
  SetupM4AAudio(mem, apu);

  // Initial fill and playback
  for (int i = 0; i < 8; ++i)
    apu.WriteFIFO_A(0x30303030u);
  for (int i = 0; i < 40; ++i)
    SimulateTimerOverflow(apu);
  DrainRingBuffer(apu);

  // Let FIFO drain to empty
  while (apu.GetFifoACount() > 0)
    SimulateTimerOverflow(apu);
  // A few more overflows with empty FIFO
  for (int i = 0; i < 5; ++i)
    SimulateTimerOverflow(apu);

  // Refill with similar value (simulating M4A DMA refill)
  for (int i = 0; i < 8; ++i)
    apu.WriteFIFO_A(0x32323232u);

  // Continue playback
  for (int i = 0; i < 10; ++i)
    SimulateTimerOverflow(apu);

  auto samples = DrainRingBuffer(apu);
  ASSERT_GT(samples.size(), 4u);

  // Max sample-to-sample delta should be bounded
  int maxDelta = 0;
  for (size_t i = 2; i < samples.size(); i += 2) {
    int d = std::abs((int)samples[i] - (int)samples[i - 2]);
    maxDelta = std::max(maxDelta, d);
  }
  EXPECT_LT(maxDelta, 1500)
      << "Refill after underflow should not produce a pop";
}

// Output should converge to zero when fed constant zero FIFO input.
TEST(AudioCorruptionTest, HpfConvergesToZeroOnSilence) {
  GBAMemory mem;
  APU apu(mem);
  SetupM4AAudio(mem, apu);

  // Push non-zero audio then drain
  for (int i = 0; i < 8; ++i)
    apu.WriteFIFO_A(0x60606060u);
  for (int i = 0; i < 20; ++i)
    SimulateTimerOverflow(apu);
  DrainRingBuffer(apu);

  // Now push silence
  for (int i = 0; i < 8; ++i)
    apu.WriteFIFO_A(0x00000000u);
  for (int i = 0; i < 2000; ++i) {
    if (apu.GetFifoACount() < 8)
      apu.WriteFIFO_A(0x00000000u);
    SimulateTimerOverflow(apu);
  }

  auto samples = DrainRingBuffer(apu, 16384);
  ASSERT_GT(samples.size(), 100u);

  // The last 100 samples should all be near zero.
  // Tolerance of 100 accounts for SOUNDBIAS gain (~48×).
  bool allNearZero = true;
  size_t start = samples.size() - 100;
  for (size_t i = start; i < samples.size(); i += 2) {
    if (std::abs(samples[i]) > 100) {
      allNearZero = false;
      break;
    }
  }
  EXPECT_TRUE(allNearZero)
      << "HPF should converge to zero after sustained silence input";
}