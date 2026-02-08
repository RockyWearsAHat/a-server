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
  writePos = 0;
  readPos = 0;

  soundcntH = 0;
  soundcntX = 0;

  for (auto &ch : psgChannels)
    ch.Reset();
  waveChannel.Reset();
  noiseChannel.Reset();

  sampleAccumulator = 0.0f;
  prevLeft = 0;
  prevRight = 0;
}

void APU::PushSample(int16_t left, int16_t right) {
  // Linear interpolation resampler: converts from GBA sample rate to
  // host output rate. Avoids the aliasing artifacts of nearest-neighbor.
  sampleAccumulator += currentUpsampleRatio;

  while (sampleAccumulator >= 1.0f) {
    sampleAccumulator -= 1.0f;

    int wp = writePos.load(std::memory_order_relaxed);
    int rp = readPos.load(std::memory_order_acquire);
    int nextWp = (wp + 2) % (RING_BUFFER_SIZE * 2);

    if (nextWp == rp) {
      stats.ringOverrunDrops.fetch_add(1, std::memory_order_relaxed);
      sampleAccumulator = 0.0f;
      break;
    }

    float t = 1.0f - sampleAccumulator / std::max(currentUpsampleRatio, 1.0f);
    t = std::clamp(t, 0.0f, 1.0f);
    int16_t interpL = static_cast<int16_t>((1.0f - t) * prevLeft + t * left);
    int16_t interpR = static_cast<int16_t>((1.0f - t) * prevRight + t * right);

    ringBuffer[wp] = interpL;
    ringBuffer[wp + 1] = interpR;
    writePos.store(nextWp, std::memory_order_release);
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
  // Master sound gate (SOUNDCNT_X bit 7)
  if (!(memory.Read16(IORegs::REG_SOUNDCNT_X) & 0x80))
    return;

  uint16_t scntH = memory.Read16(IORegs::REG_SOUNDCNT_H);

  int fifoATimer = (scntH >> 10) & 1;
  int fifoBTimer = (scntH >> 14) & 1;

  bool isATimer = (timer == fifoATimer);
  bool isBTimer = (timer == fifoBTimer);

  if (!isATimer && !isBTimer)
    return;

  // Consume FIFO samples for whichever channel(s) use this timer
  if (isATimer && fifoA_Count > 0) {
    currentSampleA = fifoA[fifoA_ReadPos];
    fifoA_ReadPos = (fifoA_ReadPos + 1) % 32;
    fifoA_Count--;
  } else if (isATimer) {
    stats.fifoAUnderflows.fetch_add(1, std::memory_order_relaxed);
  }

  if (isBTimer && fifoB_Count > 0) {
    currentSampleB = fifoB[fifoB_ReadPos];
    fifoB_ReadPos = (fifoB_ReadPos + 1) % 32;
    fifoB_Count--;
  } else if (isBTimer) {
    stats.fifoBUnderflows.fetch_add(1, std::memory_order_relaxed);
  }

  // Push audio output using this timer's frequency as the clock source.
  // When both FIFOs share a timer, every overflow produces one output.
  // When they use different timers, each timer overflow pushes separately
  // using its own frequency — avoids starving a FIFO whose timer fires less
  // often.
  const uint16_t tmReload = memory.GetTimerReload(timer);
  const uint16_t tmControl = memory.GetTimerControl(timer);
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
  if (cyclesPerSample <= 0)
    return;

  const float inputSampleRate =
      GBA_CPU_FREQ / static_cast<float>(cyclesPerSample);
  currentUpsampleRatio = outputSampleRate / inputSampleRate;

  // GBA DAC: int8 FIFO samples, volume-shifted, mixed to L/R, scaled to int16.
  // Max per-FIFO: int8 (-128..127) << 2 = -512..508
  // Two FIFOs summed: -1024..1016
  constexpr int kDacToInt16 = 32;
  const int volShiftA = (scntH & 0x04) ? 2 : 1;
  const int volShiftB = (scntH & 0x08) ? 2 : 1;

  int32_t left = 0;
  int32_t right = 0;

  int32_t dacA = static_cast<int32_t>(currentSampleA) << volShiftA;
  int32_t dacB = static_cast<int32_t>(currentSampleB) << volShiftB;

  if (scntH & 0x200)
    left += dacA;
  if (scntH & 0x100)
    right += dacA;
  if (scntH & 0x2000)
    left += dacB;
  if (scntH & 0x1000)
    right += dacB;

  left = std::clamp(left, -1024, 1023) * kDacToInt16;
  right = std::clamp(right, -1024, 1023) * kDacToInt16;

  PushSample(static_cast<int16_t>(left), static_cast<int16_t>(right));
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
  int rp = readPos.load(std::memory_order_relaxed);
  int wp = writePos.load(std::memory_order_acquire);

  int samplesWritten = 0;
  for (int i = 0; i < numSamples; i++) {
    if (rp == wp) {
      // Underrun: hold last sample to avoid pops from zero insertion
      if (samplesWritten > 0) {
        buffer[i * 2] = buffer[(i - 1) * 2];
        buffer[i * 2 + 1] = buffer[(i - 1) * 2 + 1];
      } else {
        buffer[i * 2] = 0;
        buffer[i * 2 + 1] = 0;
      }
    } else {
      buffer[i * 2] = ringBuffer[rp];
      buffer[i * 2 + 1] = ringBuffer[rp + 1];
      rp = (rp + 2) % (RING_BUFFER_SIZE * 2);
      samplesWritten++;
    }
  }

  readPos.store(rp, std::memory_order_release);
  return samplesWritten;
}

bool APU::IsSoundEnabled() const { return (soundcntX & 0x80) != 0; }

} // namespace AIO::Emulator::GBA
