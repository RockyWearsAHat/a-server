#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <emulator/gba/APU.h>
#include <emulator/gba/GBAMemory.h>
#include <emulator/gba/IORegs.h>

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

  // Reset PSG channels
  for (auto &ch : psgChannels)
    ch.Reset();
  waveChannel.Reset();
  noiseChannel.Reset();

  // Reset frame sequencer
  frameSequencerCycles = 0;
  frameSequencerStep = 0;
  psgCycleAccum = 0;
  soundcntL = 0;
  dacCycleAccumulator = 0;
  cachedBiasMode = 0;
  cachedBiasLevel = 0x200;

  // Reset resampler state
  sampleAccumulator = 0.0;
  prevLeft = 0;
  prevRight = 0;
  lpfLeft.Reset();
  lpfRight.Reset();
  lastConfiguredLpfRate = 0.0f;
}

// Configure biquad low-pass filter coefficients (Butterworth, second-order).
// Cutoff is set to outputRate/2 so that frequencies above the output
// Nyquist are attenuated before downsampling, preventing aliasing.
void APU::ConfigureLPF(float dacRate, float outRate) {
  // Cutoff at 90% of output Nyquist to leave margin
  double cutoff = outRate * 0.45;
  if (cutoff >= dacRate * 0.5)
    cutoff = dacRate * 0.499; // can't exceed input Nyquist

  double w0 = 2.0 * M_PI * cutoff / dacRate;
  double cosw0 = std::cos(w0);
  double sinw0 = std::sin(w0);
  // Q = 0.7071 for Butterworth (maximally flat passband)
  double alpha = sinw0 / (2.0 * 0.7071067811865476);

  double a0 = 1.0 + alpha;
  lpfLeft.b0 = lpfRight.b0 = ((1.0 - cosw0) / 2.0) / a0;
  lpfLeft.b1 = lpfRight.b1 = (1.0 - cosw0) / a0;
  lpfLeft.b2 = lpfRight.b2 = ((1.0 - cosw0) / 2.0) / a0;
  lpfLeft.a1 = lpfRight.a1 = (-2.0 * cosw0) / a0;
  lpfLeft.a2 = lpfRight.a2 = (1.0 - alpha) / a0;

  lastConfiguredLpfRate = dacRate;
}

void APU::PushSample(int16_t left, int16_t right) {
  // Apply biquad low-pass before resampling (anti-alias for downsampling)
  double filteredL = lpfLeft.Process(static_cast<double>(left));
  double filteredR = lpfRight.Process(static_cast<double>(right));

  int16_t fL = static_cast<int16_t>(
      std::clamp(filteredL, -32768.0, 32767.0));
  int16_t fR = static_cast<int16_t>(
      std::clamp(filteredR, -32768.0, 32767.0));

  sampleAccumulator += currentResampleRatio;

  while (sampleAccumulator >= 1.0) {
    sampleAccumulator -= 1.0;

    int wp = writePos.load(std::memory_order_relaxed);
    int rp = readPos.load(std::memory_order_acquire);
    int nextWp = (wp + 2) % (RING_BUFFER_SIZE * 2);

    if (nextWp == rp) {
      stats.ringOverrunDrops.fetch_add(1, std::memory_order_relaxed);
      break;
    }

    // Linear interpolation between previous and current filtered sample
    float t = static_cast<float>(sampleAccumulator);
    int16_t outL =
        static_cast<int16_t>((1.0f - t) * fL + t * prevLeft);
    int16_t outR =
        static_cast<int16_t>((1.0f - t) * fR + t * prevRight);

    ringBuffer[wp] = outL;
    ringBuffer[wp + 1] = outR;
    writePos.store(nextWp, std::memory_order_release);
  }

  prevLeft = fL;
  prevRight = fR;
}

void APU::Update(int cycles) {
  // Check master enable first to avoid expensive register reads
  soundcntX = memory.ReadIODirect16(IORegs::SOUNDCNT_X);
  if (!(soundcntX & 0x80))
    return;

  // Only read other registers if sound is enabled
  soundcntH = memory.ReadIODirect16(IORegs::SOUNDCNT_H);
  soundcntL = memory.ReadIODirect16(IORegs::SOUNDCNT_L);

  // Advance PSG frequency timers
  StepPSGFrequencyTimers(cycles);

  // Tick frame sequencer (512 Hz = every 32768 CPU cycles)
  frameSequencerCycles += cycles;
  while (frameSequencerCycles >= FRAME_SEQ_PERIOD) {
    frameSequencerCycles -= FRAME_SEQ_PERIOD;
    TickFrameSequencer();
  }

  // Cache SOUNDBIAS for GenerateDacSample()
  uint16_t soundbias = memory.ReadIODirect16(IORegs::SOUNDBIAS);
  cachedBiasMode = (soundbias >> 14) & 3;
  cachedBiasLevel = soundbias & 0x3FF;
  int cyclesPerDacSample = 512 >> cachedBiasMode;

  dacCycleAccumulator += cycles;
  while (dacCycleAccumulator >= cyclesPerDacSample) {
    dacCycleAccumulator -= cyclesPerDacSample;
    GenerateDacSample();
  }
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

  // Also set hardware fields so Sample() works in test mode
  ch.currentVolume = ch.volume;
  ch.dacEnabled = (ch.volume > 0);
  ch.channelEnabled = ch.enabled;
  ch.wavePos = 0;
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
      // Advance wavePos based on periodSamples for test mode
      if (ch.periodSamples > 0) {
        ch.pos = (ch.pos + 1) % ch.periodSamples;
        // Map pos back to 8-step wavePos
        ch.wavePos = (ch.pos * 8) / ch.periodSamples;
      }
    }
    return out;
  }

  // Wave channel (2)
  if (channel == 2) {
    for (int i = 0; i < numSamples; ++i) {
      out.push_back(waveChannel.Sample());
      waveChannel.stepCounter++;
      if (waveChannel.periodSamples > 0 &&
          waveChannel.stepCounter >= waveChannel.periodSamples) {
        waveChannel.stepCounter = 0;
        waveChannel.Advance();
      }
    }
    return out;
  }

  // Noise channel (3)
  if (channel == 3) {
    for (int i = 0; i < numSamples; ++i) {
      out.push_back(noiseChannel.Sample());
      noiseChannel.stepCounter++;
      if (noiseChannel.periodSamples > 0 &&
          noiseChannel.stepCounter >= noiseChannel.periodSamples) {
        noiseChannel.stepCounter = 0;
        noiseChannel.Advance();
      }
    }
    return out;
  }

  return out;
}

void APU::SetPSGWaveRAM(const std::array<uint8_t, 32> &data) {
  waveChannel.wave = data;
  // Copy nibbles into hardware waveRAM (packed byte format)
  for (int i = 0; i < 16; i++) {
    waveChannel.waveRAM[i] = (data[i * 2] << 4) | (data[i * 2 + 1] & 0xF);
  }
}

void APU::SetPSGWaveParams(int periodSamples, int volume) {
  waveChannel.periodSamples = std::max(0, periodSamples);
  waveChannel.volume = std::clamp(volume, 0, 3);
  waveChannel.stepCounter = 0;
  waveChannel.pos = 0;
  waveChannel.enabled =
      (waveChannel.periodSamples > 0 && waveChannel.volume != 3);

  // Set hardware fields for Sample() — volumeCode mapping: 0=mute,1=100%,2=50%,3=25%
  // Test volume: 0=100%, 1=50%, 2=25%, 3=mute → map to hardware volumeCode
  static constexpr int testVolToHwVol[] = {1, 2, 3, 0};
  waveChannel.volumeCode = testVolToHwVol[volume & 3];
  waveChannel.forceVolume = false;
  waveChannel.dacEnabled = waveChannel.enabled;
  waveChannel.channelEnabled = waveChannel.enabled;
  waveChannel.wavePos = 0;
  waveChannel.currentSample = (waveChannel.waveRAM[0] >> 4) & 0xF;
}

void APU::SetPSGNoiseParams(int periodSamples, bool shortMode, int volume) {
  noiseChannel.periodSamples = std::max(0, periodSamples);
  noiseChannel.shortMode = shortMode;
  noiseChannel.volume = std::clamp(volume, 0, 15);
  noiseChannel.stepCounter = 0;
  noiseChannel.lfsr = 0;
  noiseChannel.enabled =
      (noiseChannel.periodSamples > 0 && noiseChannel.volume > 0);

  // Set hardware fields so Sample() works
  noiseChannel.currentVolume = noiseChannel.volume;
  noiseChannel.widthMode = shortMode;
  noiseChannel.dacEnabled = (noiseChannel.volume > 0);
  noiseChannel.channelEnabled = noiseChannel.enabled;
}

void APU::OnTimerOverflow(int timer) {
  uint16_t scntH = memory.ReadIODirect16(IORegs::SOUNDCNT_H);

  int fifoATimer = (scntH >> 10) & 1;
  int fifoBTimer = (scntH >> 14) & 1;

  if (timer != fifoATimer && timer != fifoBTimer) {
    return;
  }

  // Timer overflow latches the next byte from the FIFO into currentSample.
  // The DAC reads currentSample at its own rate (SOUNDBIAS-driven).
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

// ── SOUNDBIAS-rate DAC output ────────────────────────────────────────────
// On real hardware, the final mixer runs at the SOUNDBIAS sample rate
// (32768 Hz by default). It combines the latched FIFO samples (updated by
// timer overflow) with the PSG channels (running at 262144 Hz internally
// but resampled here). The result is a 10-bit unsigned value (0-3FFh)
// which is then output via PWM.

void APU::GenerateDacSample() {
  // FIFO volume: full vol = <<2, half vol = <<1 (mGBA: <<2 >> !volumeCh)
  const int volShiftA = (soundcntH & 0x04) ? 2 : 1;
  const int volShiftB = (soundcntH & 0x08) ? 2 : 1;

  // Read the held FIFO sample (latched on timer overflow)
  int32_t dacA = static_cast<int32_t>(currentSampleA) << volShiftA;
  int32_t dacB = static_cast<int32_t>(currentSampleB) << volShiftB;

  int32_t left = 0;
  int32_t right = 0;

  if (soundcntH & 0x200)
    left += dacA;
  if (soundcntH & 0x100)
    right += dacA;
  if (soundcntH & 0x2000)
    left += dacB;
  if (soundcntH & 0x1000)
    right += dacB;

  // PSG mix (channels output 0-15, scaled by <<3, master vol, psgShift)
  int32_t psgMixed = GeneratePSGMixSample();
  int32_t psgLeft = (int16_t)(psgMixed & 0xFFFF);
  int32_t psgRight = (int16_t)((psgMixed >> 16) & 0xFFFF);

  left += psgLeft;
  right += psgRight;

  // mGBA's _applyBias: 10-bit DAC clamp, then scale by masterVolume
  // ((sample + bias) clamped to [0, 0x3FF] - bias) * masterVolume * 3 >> 4
  // masterVolume = GBA_AUDIO_VOLUME_MAX = 0x100, so * 0x100 * 3 >> 4 = * 48
  int bias = cachedBiasLevel;

  left = std::clamp(left + bias, 0, 0x3FF) - bias;
  right = std::clamp(right + bias, 0, 0x3FF) - bias;

  constexpr int kMasterVolume = 0x100;
  left = (left * kMasterVolume * 3) >> 4;
  right = (right * kMasterVolume * 3) >> 4;

  // Resample ratio: output rate / SOUNDBIAS DAC rate
  float soundbiasRate = 32768.0f * (float)(1 << cachedBiasMode);
  currentResampleRatio =
      (soundbiasRate > 0.0f) ? (outputSampleRate / soundbiasRate) : 1.0;

  // Reconfigure anti-alias filter when DAC rate changes
  if (soundbiasRate != lastConfiguredLpfRate)
    ConfigureLPF(soundbiasRate, outputSampleRate);

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
}

void APU::ResetFIFO_B() {
  fifoB.fill(0);
  fifoB_ReadPos = 0;
  fifoB_WritePos = 0;
  fifoB_Count = 0;
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

// ── PSG Register Write Handler ──────────────────────────────────────────

void APU::WriteRegister(uint32_t offset, uint16_t value) {
  if (!(soundcntX & 0x80) && offset != IORegs::SOUNDCNT_X)
    return;

  switch (offset) {
  // ── Channel 1: Sweep ──
  case IORegs::SOUND1CNT_L: {
    auto &ch = psgChannels[0];
    ch.sweepShift = value & 7;
    ch.sweepDir = (value >> 3) & 1;
    ch.sweepPeriod = (value >> 4) & 7;
    break;
  }
  // ── Channel 1: Duty/Length/Envelope ──
  case IORegs::SOUND1CNT_H: {
    auto &ch = psgChannels[0];
    ch.lengthCounter = 64 - (value & 0x3F);
    ch.duty = (value >> 6) & 3;
    ch.envelopePeriod = (value >> 8) & 7;
    ch.envelopeDir = (value >> 11) & 1;
    ch.envelopeInitVol = (value >> 12) & 0xF;
    ch.dacEnabled = (value & 0xF800) != 0;
    if (!ch.dacEnabled)
      ch.channelEnabled = false;
    break;
  }
  // ── Channel 1: Frequency/Control ──
  case IORegs::SOUND1CNT_X: {
    auto &ch = psgChannels[0];
    ch.frequency = value & 0x7FF;
    ch.lengthEnabled = (value >> 14) & 1;
    if (value & 0x8000) { // trigger
      ch.channelEnabled = ch.dacEnabled;
      if (ch.lengthCounter == 0)
        ch.lengthCounter = 64;
      ch.freqTimer = (2048 - ch.frequency) * 16;
      ch.currentVolume = ch.envelopeInitVol;
      ch.envelopeTimer = ch.envelopePeriod;
      ch.wavePos = 0;
      // Sweep init
      ch.sweepFreqShadow = ch.frequency;
      ch.sweepTimer = ch.sweepPeriod > 0 ? ch.sweepPeriod : 8;
      ch.sweepEnabled = (ch.sweepPeriod > 0 || ch.sweepShift > 0);
      ch.sweepNegate = false;
      if (ch.sweepShift > 0) {
        int newFreq = ch.sweepFreqShadow + (ch.sweepDir ? -1 : 1) *
                                               (ch.sweepFreqShadow >>
                                                ch.sweepShift);
        if (newFreq > 2047)
          ch.channelEnabled = false;
      }
    }
    break;
  }
  // ── Channel 2: Duty/Length/Envelope ──
  case IORegs::SOUND2CNT_L: {
    auto &ch = psgChannels[1];
    ch.lengthCounter = 64 - (value & 0x3F);
    ch.duty = (value >> 6) & 3;
    ch.envelopePeriod = (value >> 8) & 7;
    ch.envelopeDir = (value >> 11) & 1;
    ch.envelopeInitVol = (value >> 12) & 0xF;
    ch.dacEnabled = (value & 0xF800) != 0;
    if (!ch.dacEnabled)
      ch.channelEnabled = false;
    break;
  }
  // ── Channel 2: Frequency/Control ──
  case IORegs::SOUND2CNT_H: {
    auto &ch = psgChannels[1];
    ch.frequency = value & 0x7FF;
    ch.lengthEnabled = (value >> 14) & 1;
    if (value & 0x8000) { // trigger
      ch.channelEnabled = ch.dacEnabled;
      if (ch.lengthCounter == 0)
        ch.lengthCounter = 64;
      ch.freqTimer = (2048 - ch.frequency) * 16;
      ch.currentVolume = ch.envelopeInitVol;
      ch.envelopeTimer = ch.envelopePeriod;
      ch.wavePos = 0;
    }
    break;
  }
  // ── Channel 3: Stop/Wave RAM select ──
  case IORegs::SOUND3CNT_L: {
    waveChannel.dimension = (value >> 5) & 1;
    waveChannel.bankNumber = (value >> 6) & 1;
    waveChannel.dacEnabled = (value >> 7) & 1;
    if (!waveChannel.dacEnabled)
      waveChannel.channelEnabled = false;
    break;
  }
  // ── Channel 3: Length/Volume ──
  case IORegs::SOUND3CNT_H: {
    waveChannel.lengthCounter = 256 - (value & 0xFF);
    waveChannel.volumeCode = (value >> 13) & 3;
    waveChannel.forceVolume = (value >> 15) & 1;
    break;
  }
  // ── Channel 3: Frequency/Control ──
  case IORegs::SOUND3CNT_X: {
    waveChannel.frequency = value & 0x7FF;
    waveChannel.lengthEnabled = (value >> 14) & 1;
    if (value & 0x8000) { // trigger
      waveChannel.channelEnabled = waveChannel.dacEnabled;
      if (waveChannel.lengthCounter == 0)
        waveChannel.lengthCounter = 256;
      waveChannel.freqTimer = (2048 - waveChannel.frequency) * 8;
      waveChannel.wavePos = 0;
      // Copy wave RAM from IO registers
      for (int i = 0; i < 16; i++) {
        waveChannel.waveRAM[i] = memory.ReadIODirect16(
            IORegs::WAVE_RAM + (i & ~1)) >> ((i & 1) ? 8 : 0) & 0xFF;
      }
    }
    break;
  }
  // ── Channel 4: Length/Envelope ──
  case IORegs::SOUND4CNT_L: {
    noiseChannel.lengthCounter = 64 - (value & 0x3F);
    noiseChannel.envelopePeriod = (value >> 8) & 7;
    noiseChannel.envelopeDir = (value >> 11) & 1;
    noiseChannel.envelopeInitVol = (value >> 12) & 0xF;
    noiseChannel.dacEnabled = (value & 0xF800) != 0;
    if (!noiseChannel.dacEnabled)
      noiseChannel.channelEnabled = false;
    break;
  }
  // ── Channel 4: Frequency/Control ──
  case IORegs::SOUND4CNT_H: {
    noiseChannel.divRatio = value & 7;
    noiseChannel.widthMode = (value >> 3) & 1;
    noiseChannel.shiftFreq = (value >> 4) & 0xF;
    noiseChannel.lengthEnabled = (value >> 14) & 1;
    if (value & 0x8000) { // trigger
      noiseChannel.channelEnabled = noiseChannel.dacEnabled;
      if (noiseChannel.lengthCounter == 0)
        noiseChannel.lengthCounter = 64;
      noiseChannel.lfsr = 0;
      noiseChannel.currentVolume = noiseChannel.envelopeInitVol;
      noiseChannel.envelopeTimer = noiseChannel.envelopePeriod;
      // GBATEK: freq = 524288/r/2^(s+1), r=0 → r=0.5
      // Period (CPU cycles) = 32*r*2^(s+1), r=0 → 16*2^(s+1)
      int r = noiseChannel.divRatio == 0 ? 16 : noiseChannel.divRatio * 32;
      noiseChannel.freqTimer = r << (noiseChannel.shiftFreq + 1);
    }
    break;
  }
  // ── SOUNDCNT_L ──
  case IORegs::SOUNDCNT_L:
    soundcntL = value;
    break;

  // ── SOUNDCNT_X: master enable ──
  case IORegs::SOUNDCNT_X:
    if (!(value & 0x80)) {
      // Turning off master sound disables all PSG channels
      for (auto &ch : psgChannels)
        ch.channelEnabled = false;
      waveChannel.channelEnabled = false;
      noiseChannel.channelEnabled = false;
    }
    soundcntX = value;
    break;

  default:
    // Wave RAM writes (0x090..0x09F) are handled by normal IO write-through
    break;
  }
}

// ── Frame Sequencer (512 Hz) ─────────────────────────────────────────────

void APU::TickFrameSequencer() {
  // mGBA increments frame FIRST, then acts (GBAudioUpdateFrame line 657)
  frameSequencerStep = (frameSequencerStep + 1) & 7;

  // Step 0: Length
  // Step 2: Length, Sweep
  // Step 4: Length
  // Step 6: Length, Sweep
  // Step 7: Envelope

  // Length counter (steps 0, 2, 4, 6)
  if ((frameSequencerStep & 1) == 0) {
    for (auto &ch : psgChannels) {
      if (ch.lengthEnabled && ch.lengthCounter > 0) {
        ch.lengthCounter--;
        if (ch.lengthCounter == 0)
          ch.channelEnabled = false;
      }
    }
    if (waveChannel.lengthEnabled && waveChannel.lengthCounter > 0) {
      waveChannel.lengthCounter--;
      if (waveChannel.lengthCounter == 0)
        waveChannel.channelEnabled = false;
    }
    if (noiseChannel.lengthEnabled && noiseChannel.lengthCounter > 0) {
      noiseChannel.lengthCounter--;
      if (noiseChannel.lengthCounter == 0)
        noiseChannel.channelEnabled = false;
    }
  }

  // Sweep (steps 2, 6) — channel 1 only
  if (frameSequencerStep == 2 || frameSequencerStep == 6) {
    auto &ch = psgChannels[0];
    if (ch.sweepEnabled && ch.sweepPeriod > 0) {
      ch.sweepTimer--;
      if (ch.sweepTimer <= 0) {
        ch.sweepTimer = ch.sweepPeriod > 0 ? ch.sweepPeriod : 8;
        if (ch.sweepShift > 0) {
          int delta = ch.sweepFreqShadow >> ch.sweepShift;
          int newFreq = ch.sweepDir ? (ch.sweepFreqShadow - delta)
                                    : (ch.sweepFreqShadow + delta);
          if (newFreq > 2047) {
            ch.channelEnabled = false;
          } else if (newFreq >= 0) {
            ch.sweepFreqShadow = newFreq;
            ch.frequency = newFreq;
            // Overflow check again
            int next = ch.sweepFreqShadow +
                       (ch.sweepDir ? -1 : 1) *
                           (ch.sweepFreqShadow >> ch.sweepShift);
            if (next > 2047)
              ch.channelEnabled = false;
          }
        }
      }
    }
  }

  // Envelope (step 7)
  if (frameSequencerStep == 7) {
    for (auto &ch : psgChannels) {
      if (ch.envelopePeriod > 0) {
        ch.envelopeTimer--;
        if (ch.envelopeTimer <= 0) {
          ch.envelopeTimer = ch.envelopePeriod;
          if (ch.envelopeDir && ch.currentVolume < 15) {
            ch.currentVolume++;
          } else if (!ch.envelopeDir && ch.currentVolume > 0) {
            ch.currentVolume--;
          }
        }
      }
    }
    if (noiseChannel.envelopePeriod > 0) {
      noiseChannel.envelopeTimer--;
      if (noiseChannel.envelopeTimer <= 0) {
        noiseChannel.envelopeTimer = noiseChannel.envelopePeriod;
        if (noiseChannel.envelopeDir && noiseChannel.currentVolume < 15) {
          noiseChannel.currentVolume++;
        } else if (!noiseChannel.envelopeDir &&
                   noiseChannel.currentVolume > 0) {
          noiseChannel.currentVolume--;
        }
      }
    }
  }

}

// ── PSG Frequency Timer Stepping ─────────────────────────────────────────

void APU::StepPSGFrequencyTimers(int cycles) {
  // Square channels: period = (2048 - freq) * 16 CPU cycles
  for (auto &ch : psgChannels) {
    if (!ch.channelEnabled)
      continue;
    ch.freqTimer -= cycles;
    while (ch.freqTimer <= 0) {
      ch.freqTimer += (2048 - ch.frequency) * 16;
      ch.wavePos = (ch.wavePos + 1) & 7;
    }
  }

  // Wave channel: period = (2048 - freq) * 8 CPU cycles
  if (waveChannel.channelEnabled) {
    waveChannel.freqTimer -= cycles;
    while (waveChannel.freqTimer <= 0) {
      waveChannel.freqTimer += (2048 - waveChannel.frequency) * 8;
      waveChannel.Advance();
    }
  }

  // Noise channel: period = 32*r*2^(s+1), r=0 → 16*2^(s+1)
  if (noiseChannel.channelEnabled) {
    noiseChannel.freqTimer -= cycles;
    while (noiseChannel.freqTimer <= 0) {
      int r = noiseChannel.divRatio == 0 ? 16 : noiseChannel.divRatio * 32;
      noiseChannel.freqTimer += r << (noiseChannel.shiftFreq + 1);
      // Clock LFSR (mGBA: lsb = (lfsr ^ (lfsr >> 1) ^ 1) & 1)
      uint16_t lsb = (noiseChannel.lfsr ^ (noiseChannel.lfsr >> 1) ^ 1) & 1;
      noiseChannel.lfsr >>= 1;
      if (lsb) {
        noiseChannel.lfsr |= (1 << 14);
        if (noiseChannel.widthMode)
          noiseChannel.lfsr |= (1 << 6);
      } else if (noiseChannel.widthMode) {
        noiseChannel.lfsr &= ~(1 << 6);
      }
    }
  }
}

// ── PSG Mix ──────────────────────────────────────────────────────────────

int32_t APU::GeneratePSGMixSample() {
  // PSG mixing per mGBA's GBAudioSamplePSG + GBAAudioSample
  // SOUNDCNT_L: bits 0-2=right vol, 4-6=left vol, 8-11=R enable, 12-15=L enable
  int rightVol = soundcntL & 7;
  int leftVol = (soundcntL >> 4) & 7;
  int rightEnable = (soundcntL >> 8) & 0xF;
  int leftEnable = (soundcntL >> 12) & 0xF;

  // Raw PSG samples (0-15 range from Sample())
  int16_t ch1 = psgChannels[0].Sample();
  int16_t ch2 = psgChannels[1].Sample();
  int16_t ch3 = waveChannel.Sample();
  int16_t ch4 = noiseChannel.Sample();

  // Route channels to L/R and sum (mGBA sums then <<3 for ch1-3, ch4 is <<3 separately)
  int32_t psgLeft = 0;
  int32_t psgRight = 0;

  // Channels 1-3: sum raw, then <<3 (mGBA: GBAudioSamplePSG lines 748-780)
  int16_t samples123[3] = {ch1, ch2, ch3};
  for (int i = 0; i < 3; i++) {
    if (leftEnable & (1 << i))
      psgLeft += samples123[i];
    if (rightEnable & (1 << i))
      psgRight += samples123[i];
  }
  psgLeft <<= 3;
  psgRight <<= 3;

  // Channel 4 (noise): <<3 applied to sample before routing (mGBA line 782)
  int16_t noiseSample = ch4 << 3;
  if (leftEnable & 0x8)
    psgLeft += noiseSample;
  if (rightEnable & 0x8)
    psgRight += noiseSample;

  // Apply PSG master volume: multiply by (1 + vol) (mGBA line 791)
  psgLeft *= (1 + leftVol);
  psgRight *= (1 + rightVol);

  // SOUNDCNT_H bits 0-1: PSG volume ratio via psgShift = 4 - volume (mGBA line 361)
  int psgVolume = soundcntH & 3;
  int psgShift = 4 - psgVolume;
  psgLeft >>= psgShift;
  psgRight >>= psgShift;

  // Pack left/right into a single int32: lower 16 = left, upper 16 = right
  uint16_t packedLeft = static_cast<uint16_t>(static_cast<int16_t>(psgLeft));
  uint16_t packedRight = static_cast<uint16_t>(static_cast<int16_t>(psgRight));
  return (int32_t)packedLeft | ((int32_t)packedRight << 16);
}

} // namespace AIO::Emulator::GBA
