#include <cstdlib>
#include <cstring>
#include <emulator/gba/APU.h>
#include <emulator/gba/GBAMemory.h>
#include <emulator/gba/IORegs.h>
#include <iostream>

namespace AIO::Emulator::GBA {

namespace {
bool TraceGbaSpam() {
  static const bool enabled = (std::getenv("AIO_TRACE_GBA_SPAM") != nullptr);
  return enabled;
}
} // namespace

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
}

void APU::PushSample(int16_t left, int16_t right) {
  // Push the sample multiple times based on the upsample ratio
  // This converts from game's timer-based sample rate to our output rate
  sampleAccumulator += currentUpsampleRatio;

  static int pushCount = 0;
  pushCount++;

  while (sampleAccumulator >= 1.0f) {
    int wp = writePos.load(std::memory_order_relaxed);
    int rp = readPos.load(std::memory_order_acquire);

    // Calculate next write position
    int nextWp = (wp + 2) % (RING_BUFFER_SIZE * 2);

    // Check if buffer is full (leave one slot empty)
    if (nextWp == rp) {
      // Buffer full, drop sample
      stats.ringOverrunDrops.fetch_add(1, std::memory_order_relaxed);
      // Avoid spinning if we're far behind; drop accumulated output for this
      // input sample.
      sampleAccumulator = 0.0f;
      return;
    }

    ringBuffer[wp] = left;
    ringBuffer[wp + 1] = right;
    writePos.store(nextWp, std::memory_order_release);

    // Trace non-zero samples being pushed
    if (TraceGbaSpam() && (left != 0 || right != 0) && pushCount <= 50) {
      std::cout << "[PUSH_NZ] #" << pushCount << " L=" << left << " R=" << right
                << " upsample=" << currentUpsampleRatio << std::endl;
    }

    sampleAccumulator -= 1.0f;
  }
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
  static int overflowCount = 0;
  overflowCount++;

  // Read SOUNDCNT_H to check which timer each FIFO uses
  uint16_t scntH = memory.Read16(IORegs::REG_SOUNDCNT_H);

  // FIFO A uses timer specified in bit 10 (0=Timer0, 1=Timer1)
  int fifoATimer = (scntH >> 10) & 1;
  // FIFO B uses timer specified in bit 14 (0=Timer0, 1=Timer1)
  int fifoBTimer = (scntH >> 14) & 1;

  // Early exit if this timer isn't used for audio
  if (timer != fifoATimer && timer != fifoBTimer) {
    if (TraceGbaSpam() && overflowCount <= 10) {
      std::cout << "[APU] Timer " << timer << " overflow #" << overflowCount
                << " SKIPPED (not audio timer) scntH=0x" << std::hex << scntH
                << std::dec << " fifoATimer=" << fifoATimer
                << " fifoBTimer=" << fifoBTimer << std::endl;
    }
    return;
  }

  // Read Timer 0 registers (or Timer 1 depending on which is used for audio)
  int audioTimer = timer;
  // IMPORTANT: TMxCNT_L reads return the *current counter* on real hardware
  // (and in our IO emulation). For sample-rate calculation we must use the
  // programmed reload value.
  const uint16_t tmReload = memory.GetTimerReload(audioTimer);
  const uint16_t tmControl = memory.GetTimerControl(audioTimer);

  // Calculate the actual sample rate from timer configuration
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
  currentUpsampleRatio =
      (inputSampleRate > 0.0f) ? (outputSampleRate / inputSampleRate) : 0.0f;

  if (TraceGbaSpam() && (overflowCount <= 3 || overflowCount % 100000 == 0)) {
    std::cout << "[APU] Timer " << timer << " overflow #" << overflowCount
              << " FIFO_A count=" << fifoA_Count
              << " FIFO_B count=" << fifoB_Count
              << " inputRate=" << (int)inputSampleRate
              << " upsample=" << currentUpsampleRatio << std::endl;
  }

  bool consumedA = false;
  bool consumedB = false;

  // When the associated timer overflows, consume a sample from FIFO
  if (timer == fifoATimer && fifoA_Count > 0) {
    currentSampleA = fifoA[fifoA_ReadPos];
    if (TraceGbaSpam() && currentSampleA != 0) {
      std::cout << "[FIFO_A_CONSUME] sample=" << (int)currentSampleA
                << " readPos=" << fifoA_ReadPos << " count=" << fifoA_Count
                << std::endl;
    }
    fifoA_ReadPos = (fifoA_ReadPos + 1) % 32;
    fifoA_Count--;
    consumedA = true;
  } else if (timer == fifoATimer) {
    // FIFO underflow: output silence.
    currentSampleA = 0;
    stats.fifoAUnderflows.fetch_add(1, std::memory_order_relaxed);
  }

  if (timer == fifoBTimer && fifoB_Count > 0) {
    currentSampleB = fifoB[fifoB_ReadPos];
    if (TraceGbaSpam() && currentSampleB != 0) {
      std::cout << "[FIFO_B_CONSUME] sample=" << (int)currentSampleB
                << " readPos=" << fifoB_ReadPos << " count=" << fifoB_Count
                << std::endl;
    }
    fifoB_ReadPos = (fifoB_ReadPos + 1) % 32;
    fifoB_Count--;
    consumedB = true;
  } else if (timer == fifoBTimer) {
    // FIFO underflow: output silence.
    currentSampleB = 0;
    stats.fifoBUnderflows.fetch_add(1, std::memory_order_relaxed);
  }

  // Check if master sound is enabled
  uint16_t scntX = memory.Read16(IORegs::REG_SOUNDCNT_X);
  if (!(scntX & 0x80)) {
    PushSample(0, 0);
    return;
  }

  int32_t left = 0;
  int32_t right = 0;

  // FIFO A volume (bit 2: 0=50%, 1=100%)
  const int volA = (scntH & 0x04) ? 2 : 1;
  // FIFO B volume (bit 3: 0=50%, 1=100%)
  const int volB = (scntH & 0x08) ? 2 : 1;

  // FIFO A enable left/right (bits 9, 8)
  if (scntH & 0x200)
    left += (int32_t)currentSampleA * volA * 64;
  if (scntH & 0x100)
    right += (int32_t)currentSampleA * volA * 64;

  // FIFO B enable left/right (bits 13, 12)
  if (scntH & 0x2000)
    left += (int32_t)currentSampleB * volB * 64;
  if (scntH & 0x1000)
    right += (int32_t)currentSampleB * volB * 64;

  // Clamp to signed 16-bit to avoid wraparound distortion.
  if (left < -32768)
    left = -32768;
  if (left > 32767)
    left = 32767;
  if (right < -32768)
    right = -32768;
  if (right > 32767)
    right = 32767;

  // DEBUG: Trace output samples - log non-zero samples
  if (TraceGbaSpam()) {
    static int sampleLogCount = 0;
    static int nonZeroLogCount = 0;
    sampleLogCount++;

    // Log first 50 samples always, then any non-zero samples up to 100 more
    bool shouldLog = (sampleLogCount <= 50) ||
                     ((left != 0 || right != 0) && nonZeroLogCount < 100);

    if (shouldLog && (left != 0 || right != 0)) {
      nonZeroLogCount++;
      std::cout << "[APU_OUT_NZ] #" << sampleLogCount
                << " sampleA=" << (int)currentSampleA
                << " sampleB=" << (int)currentSampleB << " left=" << left
                << " right=" << right << " volA=" << volA << " volB=" << volB
                << " scntH=0x" << std::hex << scntH << std::dec << std::endl;
    }
  }

  PushSample((int16_t)left, (int16_t)right);
}

void APU::WriteFIFO_A(uint32_t value) {
  // Write 4 bytes (samples) to FIFO A
  // GBA FIFO samples are signed 8-bit PCM (-128 to +127)
  static int writeCountA = 0;
  static int nonZeroWriteCountA = 0;
  writeCountA++;
  bool hasNonZero = (value != 0);
  if (hasNonZero)
    nonZeroWriteCountA++;

  for (int i = 0; i < 4; i++) {
    if (fifoA_Count < 32) {
      int8_t sample = static_cast<int8_t>((value >> (i * 8)) & 0xFF);
      fifoA[fifoA_WritePos] = sample;
      fifoA_WritePos = (fifoA_WritePos + 1) % 32;
      fifoA_Count++;
    }
  }

  // Log all non-zero writes (first 100), and first 20 of any write
  if (TraceGbaSpam() &&
      ((hasNonZero && nonZeroWriteCountA <= 100) || writeCountA <= 20)) {
    std::cout << "[FIFO_A_WRITE #" << writeCountA << "] value=0x" << std::hex
              << value << std::dec << " count=" << fifoA_Count
              << " writePos=" << fifoA_WritePos << std::endl;
  }
}

void APU::WriteFIFO_B(uint32_t value) {
  // Write 4 bytes (samples) to FIFO B
  // GBA FIFO samples are signed 8-bit PCM (-128 to +127)
  static int writeCountB = 0;
  writeCountB++;
  bool hasNonZero = (value != 0);

  for (int i = 0; i < 4; i++) {
    if (fifoB_Count < 32) {
      int8_t sample = static_cast<int8_t>((value >> (i * 8)) & 0xFF);
      fifoB[fifoB_WritePos] = sample;
      fifoB_WritePos = (fifoB_WritePos + 1) % 32;
      fifoB_Count++;
    }
  }

  if (TraceGbaSpam() && hasNonZero && writeCountB <= 20) {
    std::cout << "[FIFO_B_WRITE] value=0x" << std::hex << value << std::dec
              << " count=" << fifoB_Count << " writePos=" << fifoB_WritePos
              << std::endl;
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

  // Optional: periodic diagnostics from the audio callback thread.
  const bool traceAudioStats =
      (std::getenv("AIO_TRACE_AUDIO_STATS") != nullptr);
  static uint32_t samplesSinceLastLog = 0;
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

  if (traceAudioStats) {
    samplesSinceLastLog += (uint32_t)numSamples;
    if (samplesSinceLastLog >= (uint32_t)OUTPUT_SAMPLE_RATE) {
      samplesSinceLastLog = 0;
      const uint64_t underruns =
          stats.ringUnderrunSamples.exchange(0, std::memory_order_relaxed);
      const uint64_t drops =
          stats.ringOverrunDrops.exchange(0, std::memory_order_relaxed);
      const uint64_t ufa =
          stats.fifoAUnderflows.exchange(0, std::memory_order_relaxed);
      const uint64_t ufb =
          stats.fifoBUnderflows.exchange(0, std::memory_order_relaxed);

      const int rp = readPos.load(std::memory_order_relaxed);
      const int wp = writePos.load(std::memory_order_relaxed);
      int fillSamples = wp - rp;
      if (fillSamples < 0)
        fillSamples += (RING_BUFFER_SIZE * 2);
      fillSamples /= 2;

      std::cout << "[AUDIO] underrunSamples/s=" << underruns
                << " ringDrops/s=" << drops << " fifoAUnderflow/s=" << ufa
                << " fifoBUnderflow/s=" << ufb << " ringFill=" << fillSamples
                << "/" << RING_BUFFER_SIZE
                << " upsample=" << currentUpsampleRatio
                << " fifoA=" << fifoA_Count << " fifoB=" << fifoB_Count
                << std::endl;
    }
  }

  return samplesWritten;
}

bool APU::IsSoundEnabled() const { return (soundcntX & 0x80) != 0; }

} // namespace AIO::Emulator::GBA
