#include <gtest/gtest.h>

#include <cstdint>

#include "emulator/gba/APU.h"
#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/IORegs.h"

using namespace AIO::Emulator::GBA;

namespace {
void ConfigureTimer0_32768Hz(GBAMemory &mem) {
  // GBA CPU = 16777216 Hz. For 32768 Hz sample clock, need 512 cycles/overflow.
  // With prescaler=1: ticksToOverflow = 512 => reload = 0x10000 - 512 = 0xFE00.
  mem.Write16(IORegs::REG_TM0CNT_L, 0xFE00u);
  mem.Write16(IORegs::REG_TM0CNT_H,
              (uint16_t)(TimerControl::ENABLE | TimerControl::PRESCALER_1));
}

// SOUNDCNT_H bits used by our APU implementation:
// - bit2: FIFO A volume (0=50%,1=100%)
// - bit3: FIFO B volume (0=50%,1=100%)
// - bit8/9: FIFO A R/L enable
// - bit12/13: FIFO B R/L enable
// - bit10: FIFO A timer select (0=Timer0,1=Timer1)
// - bit14: FIFO B timer select (0=Timer0,1=Timer1)
uint16_t MakeSoundCntH_ForFifoA(bool vol100, bool left, bool right, int timer) {
  uint16_t v = 0;
  if (vol100) {
    v |= 0x0004u;
  }
  if (right) {
    v |= 0x0100u;
  }
  if (left) {
    v |= 0x0200u;
  }
  if (timer != 0) {
    v |= 0x0400u;
  }
  return v;
}
} // namespace

TEST(APUTest, MasterDisable_ProducesSilence) {
  GBAMemory mem;
  mem.Reset();

  APU apu(mem);
  apu.Reset();

  ConfigureTimer0_32768Hz(mem);

  // Route FIFO A to both channels, volume 100%, timer0. Master sound disabled.
  mem.Write16(IORegs::REG_SOUNDCNT_H,
              MakeSoundCntH_ForFifoA(true, true, true, 0));
  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0000u);

  apu.WriteFIFO_A(0x0000007Fu);
  apu.OnTimerOverflow(0);

  int16_t out[2] = {1, 1};
  EXPECT_EQ(apu.GetSamples(out, 1), 1);
  EXPECT_EQ(out[0], 0);
  EXPECT_EQ(out[1], 0);
}

TEST(APUTest, FifoA_RoutesAndScalesToLeftOnly) {
  GBAMemory mem;
  mem.Reset();

  APU apu(mem);
  apu.Reset();

  ConfigureTimer0_32768Hz(mem);

  // Master enable.
  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080u);

  // FIFO A: volume 100%, left enabled, right disabled, timer0.
  mem.Write16(IORegs::REG_SOUNDCNT_H,
              MakeSoundCntH_ForFifoA(true, true, false, 0));

  // One sample at +127.
  apu.WriteFIFO_A(0x0000007Fu);
  apu.OnTimerOverflow(0);

  int16_t out[2] = {0, 0};
  EXPECT_EQ(apu.GetSamples(out, 1), 1);

  // Our APU scales int8 by 64 when routing into mixer.
  // 127 * 64 = 8128.
  EXPECT_EQ(out[0], 8128);
  EXPECT_EQ(out[1], 0);
}

TEST(APUTest, TimerSelection_GatesAudioTick) {
  GBAMemory mem;
  mem.Reset();

  APU apu(mem);
  apu.Reset();

  ConfigureTimer0_32768Hz(mem);

  // Master enable, select Timer1 for FIFO A and FIFO B. We'll call Timer0
  // overflow.
  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080u);
  // Set FIFO A timer select (bit10) via helper, and FIFO B timer select
  // (bit14).
  mem.Write16(
      IORegs::REG_SOUNDCNT_H,
      (uint16_t)(MakeSoundCntH_ForFifoA(true, true, true, 1) | 0x4000u));

  apu.WriteFIFO_A(0x0000007Fu);
  apu.OnTimerOverflow(0);

  int16_t out[2] = {123, 456};
  // Ring buffer should be empty (no PushSample executed), so GetSamples reports
  // 0.
  EXPECT_EQ(apu.GetSamples(out, 1), 0);
  EXPECT_EQ(out[0], 0);
  EXPECT_EQ(out[1], 0);
}

// -----------------------------------------------------------------------------
// Phase 3b: GB PSG (Channels 1-4)
//
// PSG is not currently implemented in the APU. These tests document expected
// GBATEK behavior and will be enabled once PSG synthesis exists.
// -----------------------------------------------------------------------------

#if 0

TEST(APUTest, PSG_Channel2_Duty12_5Percent_WaveformPattern) {
  GBAMemory mem;
  mem.Reset();

  APU apu(mem);
  apu.Reset();
  mem.SetAPU(&apu);

  ConfigureTimer0_32768Hz(mem);

  // Master enable.
  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080u);

  // Ensure OnTimerOverflow(0) runs by selecting timer0 for FIFO A/B.
  // Also keep DirectSound routing off so it doesn't affect the waveform.
  mem.Write16(IORegs::REG_SOUNDCNT_H, 0x0000u);

  // Route PSG channel 2 to both L/R; set L/R volumes to max.
  // NR50 (low byte): left vol=7 (bits4-6), right vol=7 (bits0-2) => 0x77.
  // NR51 (high byte): right ch2 (bit1), left ch2 (bit5) => 0x22.
  mem.Write16(IORegs::BASE + IORegs::SOUNDCNT_L, 0x2277u);

  // SOUND2CNT_L: duty=12.5% (0), initial envelope volume=15.
  // Bits 6-7 duty, bits 12-15 initial volume.
  mem.Write16(IORegs::BASE + IORegs::SOUND2CNT_L, (uint16_t)((0u << 6) | (15u << 12)));

  // Choose freq such that square wave is 4096 Hz.
  // f = 131072/(2048-freq) => 4096 => (2048-freq)=32 => freq=2016.
  const uint16_t freq = 2016u;

  // Trigger channel (bit15 write-only) and set frequency.
  mem.Write16(IORegs::BASE + IORegs::SOUND2CNT_H, (uint16_t)(0x8000u | freq));

  // For 12.5% duty, the 8-step pattern is 00000001.
  // With freq chosen so that we advance one duty step per sample at 32768 Hz,
  // we should see exactly 1 high in any 8-sample window (phase may vary
  // depending on trigger/clock alignment).
  int16_t buf[2] = {0, 0};
  int highs = 0;
  for (int i = 0; i < 8; ++i) {
    apu.OnTimerOverflow(0);
    EXPECT_EQ(apu.GetSamples(buf, 1), 1);
    const bool high = (buf[0] != 0) || (buf[1] != 0);
    highs += high ? 1 : 0;
  }
  EXPECT_EQ(highs, 1);
}

TEST(APUTest, PSG_Channel2_Envelope_DecrementsAtFrameSequencerRate) {
  GBAMemory mem;
  mem.Reset();

  APU apu(mem);
  apu.Reset();
  mem.SetAPU(&apu);

  // Master enable (not strictly required for envelope, but matches real usage).
  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080u);

  // SOUND2CNT_L envelope fields (NR22):
  // - initial volume (bits 12-15)
  // - direction (bit 11): 0=decrease
  // - period (bits 8-10): 1 => clock every envelope tick
  // Duty doesn't matter for this test.
  const uint8_t initialVol = 5;
  const uint16_t sound2cntL = (uint16_t)((initialVol << 12) | (0u << 11) | (1u << 8));
  mem.Write16(IORegs::BASE + IORegs::SOUND2CNT_L, sound2cntL);

  // Trigger channel 2.
  const uint16_t freq = 1024u;
  mem.Write16(IORegs::BASE + IORegs::SOUND2CNT_H, (uint16_t)(0x8000u | freq));

  EXPECT_EQ(apu.DebugGetPsgSquare2Volume(), initialVol);

  // Frame sequencer clocks at 512Hz (every 32768 CPU cycles).
  // Envelope clocks at 64Hz on step 7. With step starting at 0 on Reset(),
  // the first envelope clock happens after 7 frame sequencer ticks.
  apu.Update(7 * 32768);
  EXPECT_EQ(apu.DebugGetPsgSquare2Volume(), (uint8_t)(initialVol - 1));

  // Next envelope clock is 8 sequencer ticks later.
  apu.Update(8 * 32768);
  EXPECT_EQ(apu.DebugGetPsgSquare2Volume(), (uint8_t)(initialVol - 2));
}

TEST(APUTest, PSG_Channel1_Sweep_AffectsFrequencyOverTime) {
  GBAMemory mem;
  mem.Reset();

  APU apu(mem);
  apu.Reset();
  mem.SetAPU(&apu);

  // Master enable.
  mem.Write16(IORegs::REG_SOUNDCNT_X, 0x0080u);

  // SOUND1CNT_L (NR10): sweep
  // period=1 (bits4-6), negate=0 (bit3), shift=1 (bits0-2)
  const uint16_t sound1cntL = (uint16_t)((1u << 4) | 1u);
  mem.Write16(IORegs::BASE + IORegs::SOUND1CNT_L, sound1cntL);

  // SOUND1CNT_H (NR11/NR12): set envelope so DAC is enabled (init vol=15)
  const uint16_t sound1cntH = (uint16_t)(15u << 12);
  mem.Write16(IORegs::BASE + IORegs::SOUND1CNT_H, sound1cntH);

  // Trigger with a known starting frequency.
  const uint16_t startFreq = 1024u;
  mem.Write16(IORegs::BASE + IORegs::SOUND1CNT_X, (uint16_t)(0x8000u | startFreq));

  EXPECT_TRUE(apu.DebugGetPsgSquare1Enabled());
  EXPECT_EQ(apu.DebugGetPsgSquare1Freq11(), startFreq);

  // Sweep clocks at 128Hz on sequencer steps 2 and 6.
  // With step starting at 0, the first sweep clock is at step 2 (after 2 ticks).
  apu.Update(2 * 32768);

  const uint16_t expected = (uint16_t)(startFreq + (startFreq >> 1));
  EXPECT_EQ(apu.DebugGetPsgSquare1Freq11(), expected);
  EXPECT_TRUE(apu.DebugGetPsgSquare1Enabled());
}

#endif
