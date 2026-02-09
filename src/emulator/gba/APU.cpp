#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <emulator/gba/APU.h>
#include <emulator/gba/GBAMemory.h>
#include <emulator/gba/IORegs.h>
#include <iostream>

namespace AIO::Emulator::GBA {

APU::APU(GBAMemory &mem) : memory(mem) { Reset(); }

APU::~APU() = default;

void APU::Reset() {
  fifoA.fill(0);
  fifoB.fill(0);
  fifoA_ReadPos = 0;
  fifoA_WritePos = 0;
  fifoA_Count = 0;
  fifoB_ReadPos = 0;
  fifoB_WritePos = 0;
  fifoB_Count = 0;

  currentSampleA = 0;
  currentSampleB = 0;

  ringBuffer.fill(0);
  writePos = 0;
  readPos = 0;

  soundcntH = 0;
  soundcntX = 0;

  for (auto &ch : psgChannels)
    ch.Reset();
  waveChannel.Reset();
  noiseChannel.Reset();

  hwCh1.Reset();
  hwCh2.Reset();
  hwCh3.Reset();
  hwCh4.Reset();
  frameSequencerCycles = 0;
  frameSequencerStep = 0;

  hpfCapL = 0;
  hpfCapR = 0;

  currentUpsampleRatio = 2.048f;
  psgOutputAccumulator = 0.0f;

  // Prefill ring buffer with silence so the SDL callback has a safety
  // margin (~31 ms at 32 kHz) to absorb frame-boundary timing jitter.
  prefilled = false;
  int wp = 0;
  for (int i = 0; i < RING_PREFILL_SAMPLES; ++i) {
    ringBuffer[wp] = 0;
    ringBuffer[wp + 1] = 0;
    wp = (wp + 2) % (RING_BUFFER_SIZE * 2);
  }
  writePos.store(wp, std::memory_order_relaxed);
  readPos.store(0, std::memory_order_relaxed);
  prefilled = true;
}

void APU::PushSample(int16_t left, int16_t right) {
  int wp = writePos.load(std::memory_order_relaxed);
  int rp = readPos.load(std::memory_order_acquire);

  int nextWp = (wp + 2) % (RING_BUFFER_SIZE * 2);

  if (nextWp == rp) {
    stats.ringOverrunDrops.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  ringBuffer[wp] = left;
  ringBuffer[wp + 1] = right;
  writePos.store(nextWp, std::memory_order_release);
}

void APU::GenerateOutputSample() {
  uint16_t scntX = memory.Read16(IORegs::REG_SOUNDCNT_X);

  int32_t left = 0, right = 0;

  if (scntX & 0x80) {
    uint16_t scntH = memory.Read16(IORegs::REG_SOUNDCNT_H);
    uint16_t scntL = memory.Read16(0x04000000 + IORegs::SOUNDCNT_L);

    // --- PSG channels ---
    int psgVolRight = scntL & 0x7;
    int psgVolLeft = (scntL >> 4) & 0x7;
    uint8_t psgEnableRight = (scntL >> 8) & 0xF;
    uint8_t psgEnableLeft = (scntL >> 12) & 0xF;

    int psgRatio = scntH & 0x3;
    int psgShift = 4 - psgRatio;
    if (psgShift < 1)
      psgShift = 1;

    int ch1 = hwCh1.DacOutput();
    int ch2 = hwCh2.DacOutput();
    uint8_t waveRam[16];
    ReadWaveRam(waveRam);
    int ch3 = hwCh3.DacOutput(waveRam);
    int ch4 = hwCh4.DacOutput();

    int32_t psgLeft = 0, psgRight = 0;
    if (psgEnableLeft & 1)
      psgLeft += ch1;
    if (psgEnableLeft & 2)
      psgLeft += ch2;
    if (psgEnableLeft & 4)
      psgLeft += ch3;
    if (psgEnableLeft & 8)
      psgLeft += ch4;
    if (psgEnableRight & 1)
      psgRight += ch1;
    if (psgEnableRight & 2)
      psgRight += ch2;
    if (psgEnableRight & 4)
      psgRight += ch3;
    if (psgEnableRight & 8)
      psgRight += ch4;

    // mGBA PSG gain chain: accumulate unsigned → <<3 → *(1+masterVol)
    psgLeft = (psgLeft << 3) * (psgVolLeft + 1);
    psgRight = (psgRight << 3) * (psgVolRight + 1);
    psgLeft >>= psgShift;
    psgRight >>= psgShift;

    // --- FIFO channels ---
    const int volA = (scntH & 0x04) ? 4 : 2;
    const int volB = (scntH & 0x08) ? 4 : 2;

    if (scntH & 0x200)
      left += (int32_t)currentSampleA * volA;
    if (scntH & 0x100)
      right += (int32_t)currentSampleA * volA;
    if (scntH & 0x2000)
      left += (int32_t)currentSampleB * volB;
    if (scntH & 0x1000)
      right += (int32_t)currentSampleB * volB;

    left += psgLeft;
    right += psgRight;

    // SOUNDBIAS gain — mirrors mGBA's _applyBias().
    // Bias is bits 1-9 of SOUNDBIAS register (default 0x200 → bias=0x200).
    // Add bias, clamp to 10-bit, subtract bias, scale by masterVolume*3/16.
    uint16_t soundBiasReg = memory.Read16(0x04000000 + IORegs::SOUNDBIAS);
    int bias = soundBiasReg & 0x3FF;
    constexpr int MASTER_VOL = 0x100;
    auto applyBias = [bias](int sample) -> int {
      sample += bias;
      if (sample >= 0x400)
        sample = 0x3FF;
      else if (sample < 0)
        sample = 0;
      return ((sample - bias) * MASTER_VOL * 3) >> 4;
    };
    left = applyBias(left);
    right = applyBias(right);
  }

  // Clamp to 16-bit
  int16_t outL = static_cast<int16_t>(std::clamp(left, -32768, 32767));
  int16_t outR = static_cast<int16_t>(std::clamp(right, -32768, 32767));

  // DC-blocking high-pass filter — ALWAYS runs (even during silence)
  // so the capacitor state decays smoothly and doesn't produce pops
  // when sound re-enables after being disabled.
  static constexpr int64_t HPF_FILTER = 65368;
  int32_t capL = static_cast<int32_t>(hpfCapL >> 16);
  int32_t capR = static_cast<int32_t>(hpfCapR >> 16);
  int32_t degradedL32 = static_cast<int32_t>(outL) - capL;
  int32_t degradedR32 = static_cast<int32_t>(outR) - capR;
  int16_t degradedL =
      static_cast<int16_t>(std::clamp(degradedL32, -32768, 32767));
  int16_t degradedR =
      static_cast<int16_t>(std::clamp(degradedR32, -32768, 32767));
  hpfCapL = (static_cast<int64_t>(outL) << 16) -
            static_cast<int64_t>(degradedL) * HPF_FILTER;
  hpfCapR = (static_cast<int64_t>(outR) << 16) -
            static_cast<int64_t>(degradedR) * HPF_FILTER;

  PushSample(degradedL, degradedR);
  stats.pushCalls.fetch_add(1, std::memory_order_relaxed);
  if (degradedL != 0 || degradedR != 0)
    stats.pushNonZero.fetch_add(1, std::memory_order_relaxed);
}

void APU::Update(int cycles) {
  soundcntX = memory.Read16(IORegs::REG_SOUNDCNT_X);
  soundcntH = memory.Read16(IORegs::REG_SOUNDCNT_H);

  if (!(soundcntX & 0x80))
    return;

  TickPSGTimers(cycles);

  frameSequencerCycles += cycles;
  while (frameSequencerCycles >= FRAME_SEQ_PERIOD) {
    frameSequencerCycles -= FRAME_SEQ_PERIOD;
    StepFrameSequencer();
  }

  // Fixed-rate output: generate output samples at the configured sample rate
  // regardless of FIFO activity. GenerateOutputSample() mixes both PSG and
  // FIFO channels — FIFO contributes via the held currentSampleA/B values
  // which are updated by OnTimerOverflow(). This ensures PSG audio is never
  // silenced when FIFO timers are active (e.g. MZM text screens use PSG
  // while the music engine uses FIFO).
  psgOutputAccumulator += static_cast<float>(cycles);
  const float cyclesPerSample = GBA_CPU_FREQ / outputSampleRate;
  while (psgOutputAccumulator >= cyclesPerSample) {
    psgOutputAccumulator -= cyclesPerSample;
    GenerateOutputSample();
  }
}

void APU::SetOutputSampleRate(float hz) {
  if (hz <= 0.0f)
    return;
  outputSampleRate = hz;
  // Recalculate default upsample ratio assuming M4A standard timer
  // (reload=0xFBE8, prescaler F/1 → ~16009 Hz FIFO rate)
  currentUpsampleRatio = hz / 16009.0f;
}

void APU::SetPSGChannelParams(int channel, int periodSamples, int duty,
                              int volume) {
  if (channel < 0 || channel >= int(psgChannels.size()))
    return;
  auto &ch = psgChannels[channel];
  ch.periodSamples = std::max(0, periodSamples);
  ch.duty = duty & 3;
  ch.volume = std::clamp(volume, 0, 15);
  ch.pos = 0;
  ch.enabled = (ch.periodSamples > 0 && ch.volume > 0);
}

std::vector<int16_t> APU::GeneratePSGSamples(int channel, int numSamples) {
  std::vector<int16_t> out;
  out.reserve(numSamples);
  if (channel < 0)
    return out;

  // Square channels (0,1)
  if (channel < int(psgChannels.size())) {
    auto &ch = psgChannels[channel];
    for (int i = 0; i < numSamples; ++i) {
      out.push_back(ch.Sample());
      if (ch.periodSamples > 0) {
        ch.pos = (ch.pos + 1) % ch.periodSamples;
      }
    }
    return out;
  }

  // Wave channel (2)
  if (channel == 2) {
    for (int i = 0; i < numSamples; ++i) {
      out.push_back(waveChannel.Sample());
      waveChannel.Advance();
    }
    return out;
  }

  // Noise channel (3)
  if (channel == 3) {
    for (int i = 0; i < numSamples; ++i) {
      out.push_back(noiseChannel.Sample());
      noiseChannel.Advance();
    }
    return out;
  }

  return out;
}

void APU::SetPSGWaveRAM(const std::array<uint8_t, 32> &data) {
  waveChannel.wave = data;
}

void APU::SetPSGWaveParams(int periodSamples, int volume) {
  waveChannel.periodSamples = std::max(0, periodSamples);
  waveChannel.volume = std::clamp(volume, 0, 3);
  waveChannel.stepCounter = 0;
  waveChannel.pos = 0;
  waveChannel.enabled =
      (waveChannel.periodSamples > 0 && waveChannel.volume != 3);
}

void APU::SetPSGNoiseParams(int periodSamples, bool shortMode, int volume) {
  noiseChannel.periodSamples = std::max(0, periodSamples);
  noiseChannel.shortMode = shortMode;
  noiseChannel.volume = std::clamp(volume, 0, 15);
  noiseChannel.stepCounter = 0;
  noiseChannel.lfsr = 0x7FFF;
  noiseChannel.enabled =
      (noiseChannel.periodSamples > 0 && noiseChannel.volume > 0);
}

void APU::OnTimerOverflow(int timer) {
  uint16_t scntH = memory.Read16(IORegs::REG_SOUNDCNT_H);

  int fifoATimer = (scntH >> 10) & 1;
  int fifoBTimer = (scntH >> 14) & 1;

  if (timer != fifoATimer && timer != fifoBTimer)
    return;

  // During sound DMA recovery (recursive UpdateTimers inside PerformDMA),
  // skip FIFO consumption. Output continues using the held sample values,
  // which avoids both audio gaps and transient spikes from freshly-refilled
  // FIFO data. On real hardware, DMA and audio are effectively atomic.
  if (!suppressFifoConsumption) {
    if (timer == fifoATimer) {
      if (fifoA_Count > 0) {
        currentSampleA = fifoA[fifoA_ReadPos];
        fifoA_ReadPos = (fifoA_ReadPos + 1) % 32;
        fifoA_Count--;
      } else {
        stats.fifoAUnderflows.fetch_add(1, std::memory_order_relaxed);
      }
    }

    if (timer == fifoBTimer) {
      if (fifoB_Count > 0) {
        currentSampleB = fifoB[fifoB_ReadPos];
        fifoB_ReadPos = (fifoB_ReadPos + 1) % 32;
        fifoB_Count--;
      } else {
        stats.fifoBUnderflows.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }

  // Recalculate upsample ratio from the timer's reload value.
  // Use ReadIORegister16Internal to avoid re-entrant
  // FlushPendingPeripheralCycles → UpdateTimers → OnTimerOverflow recursion.
  uint32_t timerOffset =
      IORegs::TM0CNT_L + (timer * IORegs::TIMER_CHANNEL_SIZE);
  uint16_t reload = memory.ReadIORegister16Internal(timerOffset);
  uint16_t control = memory.ReadIORegister16Internal(timerOffset + 2);

  int prescalerDiv = 1;
  switch (control & 0x3) {
  case 1:
    prescalerDiv = 64;
    break;
  case 2:
    prescalerDiv = 256;
    break;
  case 3:
    prescalerDiv = 1024;
    break;
  }

  float timerInterval = static_cast<float>((0x10000 - reload) * prescalerDiv);
  if (timerInterval > 0.0f) {
    float fifoRate = GBA_CPU_FREQ / timerInterval;
    if (fifoRate > 0.0f)
      currentUpsampleRatio = outputSampleRate / fifoRate;
  }
}

void APU::WriteFIFO_A(uint32_t value) {
  for (int i = 0; i < 4; i++) {
    if (fifoA_Count < 32) {
      int8_t sample = static_cast<int8_t>((value >> (i * 8)) & 0xFF);
      fifoA[fifoA_WritePos] = sample;
      fifoA_WritePos = (fifoA_WritePos + 1) % 32;
      fifoA_Count++;
    }
  }
}

void APU::WriteFIFO_B(uint32_t value) {
  for (int i = 0; i < 4; i++) {
    if (fifoB_Count < 32) {
      int8_t sample = static_cast<int8_t>((value >> (i * 8)) & 0xFF);
      fifoB[fifoB_WritePos] = sample;
      fifoB_WritePos = (fifoB_WritePos + 1) % 32;
      fifoB_Count++;
    }
  }
}

void APU::ResetFIFO_A() {
  fifoA.fill(0);
  fifoA_ReadPos = 0;
  fifoA_WritePos = 0;
  fifoA_Count = 0;
  currentSampleA = 0;
}

void APU::ResetFIFO_B() {
  fifoB.fill(0);
  fifoB_ReadPos = 0;
  fifoB_WritePos = 0;
  fifoB_Count = 0;
  currentSampleB = 0;
}

int APU::GetSamples(int16_t *buffer, int numSamples) {
  int samplesWritten = 0;
  uint32_t localUnderruns = 0;

  for (int i = 0; i < numSamples; i++) {
    int rp = readPos.load(std::memory_order_relaxed);
    int wp = writePos.load(std::memory_order_acquire);

    if (rp == wp) {
      // Buffer empty - fill rest with silence
      buffer[i * 2] = 0;
      buffer[i * 2 + 1] = 0;
      localUnderruns++;
    } else {
      buffer[i * 2] = ringBuffer[rp];
      buffer[i * 2 + 1] = ringBuffer[rp + 1];

      int nextRp = (rp + 2) % (RING_BUFFER_SIZE * 2);
      readPos.store(nextRp, std::memory_order_release);
      samplesWritten++;
    }
  }

  if (localUnderruns != 0) {
    stats.ringUnderrunSamples.fetch_add(localUnderruns,
                                        std::memory_order_relaxed);
  }

  return samplesWritten;
}

bool APU::IsSoundEnabled() const { return (soundcntX & 0x80) != 0; }

void APU::ReadWaveRam(uint8_t *out16bytes) {
  for (int i = 0; i < 16; ++i) {
    out16bytes[i] = memory.Read8(0x04000000 + IORegs::WAVE_RAM + i);
  }
}

void APU::TickPSGTimers(int cycles) {
  // Channel 1 (Square with sweep)
  // GBA CPU = 16.78MHz = 4× GB. Period in GBA cycles = (2048-freq) * 16.
  if (hwCh1.enabled && hwCh1.dacEnabled) {
    hwCh1.frequencyTimer -= cycles;
    while (hwCh1.frequencyTimer <= 0) {
      hwCh1.frequencyTimer += (2048 - hwCh1.frequency) * 16;
      hwCh1.dutyPos = (hwCh1.dutyPos + 1) & 7;
    }
  }

  // Channel 2 (Square)
  if (hwCh2.enabled && hwCh2.dacEnabled) {
    hwCh2.frequencyTimer -= cycles;
    while (hwCh2.frequencyTimer <= 0) {
      hwCh2.frequencyTimer += (2048 - hwCh2.frequency) * 16;
      hwCh2.dutyPos = (hwCh2.dutyPos + 1) & 7;
    }
  }

  // Channel 3 (Wave)
  // Wave period divider runs at 2× square rate. GBA cycles = (2048-freq) * 8.
  if (hwCh3.enabled && hwCh3.dacEnabled) {
    hwCh3.frequencyTimer -= cycles;
    while (hwCh3.frequencyTimer <= 0) {
      hwCh3.frequencyTimer += (2048 - hwCh3.frequency) * 8;
      hwCh3.pos = (hwCh3.pos + 1) & 31;
    }
  }

  // Channel 4 (Noise)
  // Divisor table scaled to GBA CPU cycles (4× GB values).
  if (hwCh4.enabled && hwCh4.dacEnabled) {
    hwCh4.frequencyTimer -= cycles;
    while (hwCh4.frequencyTimer <= 0) {
      static const int divisors[8] = {32, 64, 128, 192, 256, 320, 384, 448};
      hwCh4.frequencyTimer += divisors[hwCh4.divisor & 7] << hwCh4.shiftAmount;
      // Clock LFSR
      uint16_t bit0 = hwCh4.lfsr & 1;
      uint16_t bit1 = (hwCh4.lfsr >> 1) & 1;
      uint16_t newbit = bit0 ^ bit1;
      hwCh4.lfsr = (hwCh4.lfsr >> 1) | (newbit << 14);
      if (hwCh4.shortMode) {
        hwCh4.lfsr = (hwCh4.lfsr & ~(1 << 6)) | (newbit << 6);
      }
    }
  }
}

void APU::StepFrameSequencer() {
  // GBA frame sequencer: 512 Hz, 8 steps — only clocks length/envelope/sweep
  switch (frameSequencerStep) {
  case 0:
  case 4:
    ClockLength();
    break;
  case 2:
  case 6:
    ClockLength();
    ClockSweep();
    break;
  case 7:
    ClockEnvelope();
    break;
  default:
    break;
  }

  frameSequencerStep = (frameSequencerStep + 1) & 7;
}

void APU::OnSoundRegisterWrite(uint32_t offset, uint16_t value) {
  switch (offset) {
  case IORegs::SOUND1CNT_L: {
    hwCh1.sweepPeriod = (value >> 4) & 0x7;
    hwCh1.sweepNegate = (value >> 3) & 1;
    hwCh1.sweepShift = value & 0x7;
    break;
  }
  case IORegs::SOUND1CNT_H: {
    hwCh1.duty = (value >> 6) & 3;
    hwCh1.envelopeInitVol = (value >> 12) & 0xF;
    hwCh1.envelopeIncrease = (value >> 11) & 1;
    hwCh1.envelopePeriod = (value >> 8) & 0x7;
    hwCh1.dacEnabled = (value & 0xF800) != 0;
    if (!hwCh1.dacEnabled)
      hwCh1.enabled = false;
    break;
  }
  case IORegs::SOUND1CNT_X: {
    hwCh1.frequency = value & 0x7FF;
    hwCh1.lengthEnable = (value >> 14) & 1;

    if (value & 0x8000) {
      // Read back envelope params from stored state
      uint16_t s1h = memory.Read16(0x04000000 + IORegs::SOUND1CNT_H);
      hwCh1.enabled = hwCh1.dacEnabled;
      hwCh1.volume = hwCh1.envelopeInitVol;
      hwCh1.envelopeTimer = hwCh1.envelopePeriod;
      hwCh1.lengthCounter = hwCh1.lengthCounter > 0 ? hwCh1.lengthCounter : 64;
      int rawLen = s1h & 0x3F;
      if (rawLen)
        hwCh1.lengthCounter = 64 - rawLen;
      hwCh1.frequencyTimer = (2048 - hwCh1.frequency) * 16;
      hwCh1.dutyPos = 0;

      hwCh1.sweepShadow = hwCh1.frequency;
      hwCh1.sweepTimer = hwCh1.sweepPeriod ? hwCh1.sweepPeriod : 8;
      hwCh1.sweepEnabled = (hwCh1.sweepPeriod > 0 || hwCh1.sweepShift > 0);

      // Overflow check on trigger
      if (hwCh1.sweepShift > 0) {
        int delta = hwCh1.sweepShadow >> hwCh1.sweepShift;
        int newFreq = hwCh1.sweepNegate ? (hwCh1.sweepShadow - delta)
                                        : (hwCh1.sweepShadow + delta);
        if (newFreq > 2047)
          hwCh1.enabled = false;
      }
    }
    break;
  }
  case IORegs::SOUND2CNT_L: {
    hwCh2.duty = (value >> 6) & 3;
    hwCh2.envelopeInitVol = (value >> 12) & 0xF;
    hwCh2.envelopeIncrease = (value >> 11) & 1;
    hwCh2.envelopePeriod = (value >> 8) & 0x7;
    hwCh2.dacEnabled = (value & 0xF800) != 0;
    if (!hwCh2.dacEnabled)
      hwCh2.enabled = false;
    break;
  }
  case IORegs::SOUND2CNT_H: {
    hwCh2.frequency = value & 0x7FF;
    hwCh2.lengthEnable = (value >> 14) & 1;

    if (value & 0x8000) {
      uint16_t s2l = memory.Read16(0x04000000 + IORegs::SOUND2CNT_L);
      hwCh2.enabled = hwCh2.dacEnabled;
      hwCh2.volume = hwCh2.envelopeInitVol;
      hwCh2.envelopeTimer = hwCh2.envelopePeriod;
      hwCh2.lengthCounter = hwCh2.lengthCounter > 0 ? hwCh2.lengthCounter : 64;
      int rawLen = s2l & 0x3F;
      if (rawLen)
        hwCh2.lengthCounter = 64 - rawLen;
      hwCh2.frequencyTimer = (2048 - hwCh2.frequency) * 16;
      hwCh2.dutyPos = 0;
    }
    break;
  }
  case IORegs::SOUND3CNT_L: {
    hwCh3.dacEnabled = (value >> 7) & 1;
    hwCh3.bankMode = (value >> 5) & 1;
    hwCh3.bankSelect = (value >> 6) & 1;
    if (!hwCh3.dacEnabled)
      hwCh3.enabled = false;
    break;
  }
  case IORegs::SOUND3CNT_H: {
    int volCode = (value >> 13) & 0x7;
    // GBA-specific: volCode >= 4 means "Force 75%" (sample * 3/16)
    if (volCode >= 4) {
      hwCh3.volumeShift = 0;
      hwCh3.forceThreeQuarters = true;
    } else {
      static const int volShifts[4] = {4, 0, 1, 2}; // mute, 100%, 50%, 25%
      hwCh3.volumeShift = volShifts[volCode];
      hwCh3.forceThreeQuarters = false;
    }
    break;
  }
  case IORegs::SOUND3CNT_X: {
    hwCh3.frequency = value & 0x7FF;
    hwCh3.lengthEnable = (value >> 14) & 1;

    if (value & 0x8000) {
      uint16_t s3h = memory.Read16(0x04000000 + IORegs::SOUND3CNT_H);
      hwCh3.enabled = hwCh3.dacEnabled;
      hwCh3.lengthCounter = hwCh3.lengthCounter > 0 ? hwCh3.lengthCounter : 256;
      int rawLen = s3h & 0xFF;
      if (rawLen)
        hwCh3.lengthCounter = 256 - rawLen;
      hwCh3.frequencyTimer = (2048 - hwCh3.frequency) * 8;
      hwCh3.pos = 0;
    }
    break;
  }
  case IORegs::SOUND4CNT_L: {
    hwCh4.envelopeInitVol = (value >> 12) & 0xF;
    hwCh4.envelopeIncrease = (value >> 11) & 1;
    hwCh4.envelopePeriod = (value >> 8) & 0x7;
    hwCh4.dacEnabled = (value & 0xF800) != 0;
    if (!hwCh4.dacEnabled)
      hwCh4.enabled = false;
    break;
  }
  case IORegs::SOUND4CNT_H: {
    hwCh4.divisor = value & 0x7;
    hwCh4.shiftAmount = (value >> 4) & 0xF;
    hwCh4.shortMode = (value >> 3) & 1;
    hwCh4.lengthEnable = (value >> 14) & 1;

    if (value & 0x8000) {
      uint16_t s4l = memory.Read16(0x04000000 + IORegs::SOUND4CNT_L);
      hwCh4.enabled = hwCh4.dacEnabled;
      hwCh4.volume = hwCh4.envelopeInitVol;
      hwCh4.envelopeTimer = hwCh4.envelopePeriod;
      hwCh4.lengthCounter = hwCh4.lengthCounter > 0 ? hwCh4.lengthCounter : 64;
      int rawLen = s4l & 0x3F;
      if (rawLen)
        hwCh4.lengthCounter = 64 - rawLen;
      hwCh4.lfsr = 0x7FFF;
      static const int divisors[8] = {32, 64, 128, 192, 256, 320, 384, 448};
      hwCh4.frequencyTimer = divisors[hwCh4.divisor & 7] << hwCh4.shiftAmount;
    }
    break;
  }
  default:
    break;
  }
}

void APU::ClockLength() {
  if (hwCh1.lengthEnable && hwCh1.lengthCounter > 0) {
    if (--hwCh1.lengthCounter == 0)
      hwCh1.enabled = false;
  }
  if (hwCh2.lengthEnable && hwCh2.lengthCounter > 0) {
    if (--hwCh2.lengthCounter == 0)
      hwCh2.enabled = false;
  }
  if (hwCh3.lengthEnable && hwCh3.lengthCounter > 0) {
    if (--hwCh3.lengthCounter == 0)
      hwCh3.enabled = false;
  }
  if (hwCh4.lengthEnable && hwCh4.lengthCounter > 0) {
    if (--hwCh4.lengthCounter == 0)
      hwCh4.enabled = false;
  }
}

void APU::ClockEnvelope() {
  // Channel 1 envelope
  if (hwCh1.envelopePeriod > 0) {
    if (--hwCh1.envelopeTimer <= 0) {
      hwCh1.envelopeTimer = hwCh1.envelopePeriod;
      if (hwCh1.envelopeIncrease && hwCh1.volume < 15)
        hwCh1.volume++;
      else if (!hwCh1.envelopeIncrease && hwCh1.volume > 0)
        hwCh1.volume--;
    }
  }

  // Channel 2 envelope
  if (hwCh2.envelopePeriod > 0) {
    if (--hwCh2.envelopeTimer <= 0) {
      hwCh2.envelopeTimer = hwCh2.envelopePeriod;
      if (hwCh2.envelopeIncrease && hwCh2.volume < 15)
        hwCh2.volume++;
      else if (!hwCh2.envelopeIncrease && hwCh2.volume > 0)
        hwCh2.volume--;
    }
  }

  // Channel 4 envelope
  if (hwCh4.envelopePeriod > 0) {
    if (--hwCh4.envelopeTimer <= 0) {
      hwCh4.envelopeTimer = hwCh4.envelopePeriod;
      if (hwCh4.envelopeIncrease && hwCh4.volume < 15)
        hwCh4.volume++;
      else if (!hwCh4.envelopeIncrease && hwCh4.volume > 0)
        hwCh4.volume--;
    }
  }
}

void APU::ClockSweep() {
  if (!hwCh1.sweepEnabled || hwCh1.sweepPeriod == 0)
    return;

  if (--hwCh1.sweepTimer <= 0) {
    hwCh1.sweepTimer = hwCh1.sweepPeriod ? hwCh1.sweepPeriod : 8;

    if (hwCh1.sweepShift > 0) {
      int delta = hwCh1.sweepShadow >> hwCh1.sweepShift;
      int newFreq = hwCh1.sweepNegate ? (hwCh1.sweepShadow - delta)
                                      : (hwCh1.sweepShadow + delta);

      if (newFreq > 2047) {
        hwCh1.enabled = false;
      } else if (newFreq >= 0) {
        hwCh1.sweepShadow = newFreq;
        hwCh1.frequency = newFreq;
      }
    }
  }
}

} // namespace AIO::Emulator::GBA
