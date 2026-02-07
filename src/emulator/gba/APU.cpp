#include <algorithm>
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

  // Prefill the ring buffer with silence to prevent initial underruns.
  // This provides a buffer against timing jitter between the emulator
  // (which may have variable frame timing) and the audio callback
  // (which runs on a precise real-time schedule).
  // Prefill with ~125ms of silence (about 4096 stereo samples at 32768 Hz).
  // This gives significant headroom for timing variations.
  constexpr int PREFILL_SAMPLES = RING_BUFFER_SIZE / 2; // ~4096 samples
  writePos = PREFILL_SAMPLES * 2; // Each sample is 2 int16s (stereo)
  readPos = 0;

  soundcntH = 0;
  soundcntX = 0;

  // Reset PSG channels
  for (auto &ch : psgChannels)
    ch.Reset();
  // Reset Wave channel
  waveChannel.Reset();
  // Reset Noise channel
  noiseChannel.Reset();

  // Reset sample accumulator
  sampleAccumulator = 0.0f;
  smoothedFillError = 0.0f;
  prevLeft = 0;
  prevRight = 0;
}

void APU::PushSample(int16_t left, int16_t right) {
  // Linearly interpolate between previous and current sample to fill
  // the output ring buffer. This avoids the "whirly" aliasing artifacts
  // that nearest-neighbor duplication causes when output rate != input rate.
  sampleAccumulator += currentUpsampleRatio;

  while (sampleAccumulator >= 1.0f) {
    int wp = writePos.load(std::memory_order_relaxed);
    int rp = readPos.load(std::memory_order_acquire);

    int nextWp = (wp + 2) % (RING_BUFFER_SIZE * 2);

    if (nextWp == rp) {
      stats.ringOverrunDrops.fetch_add(1, std::memory_order_relaxed);
      sampleAccumulator = 0.0f;
      prevLeft = left;
      prevRight = right;
      return;
    }

    // Linear interpolation: t=0 is previous sample, t=1 is current sample.
    // sampleAccumulator tells us how far through the current input sample
    // we are — higher values mean we're closer to the previous sample.
    float t = 1.0f -
              (sampleAccumulator - 1.0f) / std::max(currentUpsampleRatio, 1.0f);
    t = std::clamp(t, 0.0f, 1.0f);
    int16_t interpL = (int16_t)((1.0f - t) * prevLeft + t * left);
    int16_t interpR = (int16_t)((1.0f - t) * prevRight + t * right);

    ringBuffer[wp] = interpL;
    ringBuffer[wp + 1] = interpR;
    writePos.store(nextWp, std::memory_order_release);

    sampleAccumulator -= 1.0f;
  }

  prevLeft = left;
  prevRight = right;
}

void APU::Update(int cycles) {
  // Read current sound control registers
  soundcntX = memory.Read16(IORegs::REG_SOUNDCNT_X);
  soundcntH = memory.Read16(IORegs::REG_SOUNDCNT_H);

  // Nothing else to do here - samples are pushed on timer overflow
}

void APU::SetOutputSampleRate(float hz) {
  if (hz <= 0.0f) {
    return;
  }
  outputSampleRate = hz;
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

  if (timer != fifoATimer && timer != fifoBTimer) {
    return;
  }

  int audioTimer = timer;
  const uint16_t tmReload = memory.GetTimerReload(audioTimer);
  const uint16_t tmControl = memory.GetTimerControl(audioTimer);

  int prescaler = 1;
  switch (tmControl & 3) {
  case 0:
    prescaler = 1;
    break;
  case 1:
    prescaler = 64;
    break;
  case 2:
    prescaler = 256;
    break;
  case 3:
    prescaler = 1024;
    break;
  }
  const int cyclesPerSample = (0x10000 - tmReload) * prescaler;
  const float inputSampleRate =
      (cyclesPerSample > 0) ? (GBA_CPU_FREQ / (float)cyclesPerSample) : 0.0f;
  float baseRatio =
      (inputSampleRate > 0.0f) ? (outputSampleRate / inputSampleRate) : 0.0f;

  // Adaptive rate control via exponential moving average of the buffer
  // fill error. The EMA smooths out per-sample noise to give a stable
  // correction signal, preventing the pitch jitter that plagued earlier
  // approaches. We target 50% fill (4096 samples) with a very slow
  // response — the large prefill buffer absorbs short-term jitter.
  const float fill = GetRingBufferFillRatio();
  constexpr float kTargetFill = 0.50f;
  constexpr float kMinRatio = 0.5f;
  constexpr float kMaxRatio = 8.0f;

  float error = kTargetFill - fill;

  // Exponential moving average of error (smooths over ~2000 samples)
  constexpr float kEmaAlpha = 0.0005f;
  smoothedFillError =
      smoothedFillError * (1.0f - kEmaAlpha) + error * kEmaAlpha;

  // Ratio = base * (1 + gain * smoothedError)
  // At 50% speed: fill stays near 0, error ≈ 0.5, smoothedError → 0.5,
  // so ratio ≈ 1.0 * (1 + 4.0 * 0.5) = 3.0 — overproduces slightly,
  // which fills buffer, which reduces error, which reduces ratio toward 2.0
  currentUpsampleRatio = baseRatio * (1.0f + 4.0f * smoothedFillError);
  currentUpsampleRatio = std::clamp(currentUpsampleRatio, kMinRatio, kMaxRatio);

  if (timer == fifoATimer && fifoA_Count > 0) {
    currentSampleA = fifoA[fifoA_ReadPos];
    fifoA_ReadPos = (fifoA_ReadPos + 1) % 32;
    fifoA_Count--;
  } else if (timer == fifoATimer) {
    // Hold last sample on underflow (hardware DAC latches last value)
    stats.fifoAUnderflows.fetch_add(1, std::memory_order_relaxed);
  }

  if (timer == fifoBTimer && fifoB_Count > 0) {
    currentSampleB = fifoB[fifoB_ReadPos];
    fifoB_ReadPos = (fifoB_ReadPos + 1) % 32;
    fifoB_Count--;
  } else if (timer == fifoBTimer) {
    // Hold last sample on underflow (hardware DAC latches last value)
    stats.fifoBUnderflows.fetch_add(1, std::memory_order_relaxed);
  }

  // GBA DAC model: int8 samples are shifted left by 1 (50%) or 2 (100%)
  // into a 10-bit signed range (±512). Scale to int16 matches mGBA's
  // _applyBias.
  constexpr int kDacToInt16 =
      48; // mGBA: (masterVolume * 3) >> 4 = (256*3)/16 = 48

  const int volShiftA = (scntH & 0x04) ? 2 : 1;
  const int volShiftB = (scntH & 0x08) ? 2 : 1;

  int32_t left = 0;
  int32_t right = 0;

  int32_t dacA = (int32_t)currentSampleA << volShiftA;
  int32_t dacB = (int32_t)currentSampleB << volShiftB;

  if (scntH & 0x200)
    left += dacA;
  if (scntH & 0x100)
    right += dacA;

  if (scntH & 0x2000)
    left += dacB;
  if (scntH & 0x1000)
    right += dacB;

  // Clamp to 10-bit DAC range, then scale to int16
  left = std::clamp(left, -512, 511) * kDacToInt16;
  right = std::clamp(right, -512, 511) * kDacToInt16;

  PushSample((int16_t)left, (int16_t)right);
  stats.pushCalls.fetch_add(1, std::memory_order_relaxed);
  if (left != 0 || right != 0)
    stats.pushNonZero.fetch_add(1, std::memory_order_relaxed);
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

  for (int i = 0; i < numSamples; i++) {
    int rp = readPos.load(std::memory_order_relaxed);
    int wp = writePos.load(std::memory_order_acquire);

    if (rp == wp) {
      buffer[i * 2] = 0;
      buffer[i * 2 + 1] = 0;
    } else {
      buffer[i * 2] = ringBuffer[rp];
      buffer[i * 2 + 1] = ringBuffer[rp + 1];

      int nextRp = (rp + 2) % (RING_BUFFER_SIZE * 2);
      readPos.store(nextRp, std::memory_order_release);
      samplesWritten++;
    }
  }

  return samplesWritten;
}

bool APU::IsSoundEnabled() const { return (soundcntX & 0x80) != 0; }

} // namespace AIO::Emulator::GBA
