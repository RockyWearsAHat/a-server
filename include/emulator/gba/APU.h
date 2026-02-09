#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace AIO::Emulator::GBA {

class GBAMemory;

class APU {
public:
  APU(GBAMemory &memory);
  ~APU();

  void Reset();
  void Update(int cycles);

  // Called when timer overflows (for DMA sound)
  void OnTimerOverflow(int timer);

  // Configure the host output sample rate (e.g. SDL device freq).
  void SetOutputSampleRate(float hz);

  // FIFO operations
  void WriteFIFO_A(uint32_t value);
  void WriteFIFO_B(uint32_t value);
  void ResetFIFO_A();
  void ResetFIFO_B();

  // Get samples for audio output - fills buffer with samples
  // Returns number of samples written
  int GetSamples(int16_t *buffer, int numSamples);

  // Check if sound is enabled
  bool IsSoundEnabled() const;

  // Returns bits 0-3 reflecting which PSG channels are currently active
  uint8_t GetChannelStatus() const {
    uint8_t status = 0;
    if (hwCh1.enabled)
      status |= 1;
    if (hwCh2.enabled)
      status |= 2;
    if (hwCh3.enabled)
      status |= 4;
    if (hwCh4.enabled)
      status |= 8;
    return status;
  }

  // Called by GBAMemory when a sound IO register is written (0x060-0x084)
  void OnSoundRegisterWrite(uint32_t offset, uint16_t value);

  // PSG helpers
  // Configure square-wave PSG channel parameters (channel 0 or 1)
  void SetPSGChannelParams(int channel, int periodSamples, int duty,
                           int volume);
  // Generate raw PSG samples for testing/verification (mono)
  std::vector<int16_t> GeneratePSGSamples(int channel, int numSamples);
  // Wave channel helpers
  void SetPSGWaveRAM(const std::array<uint8_t, 32> &data);
  void SetPSGWaveParams(int periodSamples, int volume);
  // Noise channel helpers
  void SetPSGNoiseParams(int periodSamples, bool shortMode, int volume);

  // FIFO fill levels (for sound DMA request logic)
  int GetFifoACount() const { return fifoA_Count; }
  int GetFifoBCount() const { return fifoB_Count; }

  // Ring buffer fill level (for audio timing synchronization)
  // Returns a value from 0.0 (empty) to 1.0 (full)
  float GetRingBufferFillRatio() const {
    int wp = writePos.load(std::memory_order_relaxed);
    int rp = readPos.load(std::memory_order_relaxed);
    int fill = wp - rp;
    if (fill < 0)
      fill += RING_BUFFER_SIZE * 2;
    return static_cast<float>(fill / 2) / static_cast<float>(RING_BUFFER_SIZE);
  }

  // Clear the ring buffer (for testing - removes prefill)
  void ClearRingBuffer() {
    ringBuffer.fill(0);
    writePos = 0;
    readPos = 0;
  }

  // Debug/telemetry (optional logging controlled by env vars)
  struct AudioStats {
    std::atomic<uint64_t> ringUnderrunSamples{0};
    std::atomic<uint64_t> ringOverrunDrops{0};
    std::atomic<uint64_t> fifoAUnderflows{0};
    std::atomic<uint64_t> fifoBUnderflows{0};
    std::atomic<uint64_t> pushCalls{0};
    std::atomic<uint64_t> pushNonZero{0};
  } stats;

private:
  GBAMemory &memory;

  // DMA Sound FIFOs (32 bytes each)
  std::array<int8_t, 32> fifoA;
  std::array<int8_t, 32> fifoB;
  int fifoA_ReadPos = 0;
  int fifoA_WritePos = 0;
  int fifoA_Count = 0;
  int fifoB_ReadPos = 0;
  int fifoB_WritePos = 0;
  int fifoB_Count = 0;

  // Current FIFO samples being output
  int8_t currentSampleA = 0;
  int8_t currentSampleB = 0;

  // Output ring buffer for audio thread (lock-free)
  static constexpr int RING_BUFFER_SIZE = 8192;
  std::array<int16_t, RING_BUFFER_SIZE * 2> ringBuffer; // Stereo pairs
  std::atomic<int> writePos{0};
  std::atomic<int> readPos{0};

  // Sound control registers
  uint16_t soundcntH = 0;
  uint16_t soundcntX = 0;

  // PSG channel state (channels 1 & 2) — test-only interface
  struct PSGChannel {
    int periodSamples = 0;
    int pos = 0;
    int duty = 0;
    int volume = 0;
    bool enabled = false;

    void Reset() {
      periodSamples = 0;
      pos = 0;
      duty = 0;
      volume = 0;
      enabled = false;
    }

    int16_t Sample() const {
      if (!enabled || periodSamples <= 0)
        return 0;
      float highRatio = 0.125f;
      switch (duty) {
      case 0:
        highRatio = 0.125f;
        break;
      case 1:
        highRatio = 0.25f;
        break;
      case 2:
        highRatio = 0.5f;
        break;
      case 3:
        highRatio = 0.75f;
        break;
      }
      int highLen = std::max(1, int(highRatio * periodSamples));
      bool high = (pos < highLen);
      int16_t amp = int16_t((high ? 1.0f : -1.0f) * (volume / 15.0f) * 30000);
      return amp;
    }
  };

  std::array<PSGChannel, 2> psgChannels;

  // Hardware-driven PSG state (driven from IO register reads)
  struct HwSquareChannel {
    int frequencyTimer = 0;  // counts down at 4.194 MHz / prescaler
    int frequency = 0;       // 11-bit frequency value from register
    int duty = 0;            // 0-3 duty cycle selection
    int dutyPos = 0;         // 0-7 position in duty waveform
    int volume = 0;          // 0-15 current envelope volume
    int envelopeInitVol = 0; // initial envelope volume
    int envelopePeriod = 0;  // envelope step period (0 = disabled)
    int envelopeTimer = 0;   // counts down each frame-seq tick
    bool envelopeIncrease = false;
    int lengthCounter = 0; // 0 = expired
    bool lengthEnable = false;
    bool enabled = false;
    bool dacEnabled = false; // DAC on when volume bits or direction set

    // Sweep (channel 1 only)
    int sweepPeriod = 0;
    int sweepTimer = 0;
    int sweepShift = 0;
    bool sweepNegate = false;
    bool sweepEnabled = false;
    int sweepShadow = 0;

    void Reset() { *this = HwSquareChannel{}; }

    int16_t DacOutput() const {
      if (!enabled || !dacEnabled)
        return 0;
      static const uint8_t dutyTable[4][8] = {
          {0, 0, 0, 0, 0, 0, 0, 1}, // 12.5%
          {1, 0, 0, 0, 0, 0, 0, 1}, // 25%
          {1, 0, 0, 0, 0, 1, 1, 1}, // 50%
          {0, 1, 1, 1, 1, 1, 1, 0}, // 75%
      };
      int sample = dutyTable[duty & 3][dutyPos & 7] ? volume : 0;
      return (int16_t)((sample * 2 - 15) * 256);
    }
  };

  struct HwWaveChannel {
    int frequencyTimer = 0;
    int frequency = 0;
    int pos = 0;         // 0-31 sample position
    int volumeShift = 0; // 0=100%, 1=50%, 2=25%, 3=mute (right-shift)
    int lengthCounter = 0;
    bool lengthEnable = false;
    bool enabled = false;
    bool dacEnabled = false;
    bool bankMode = false; // false=single bank, true=two banks
    int bankSelect = 0;    // which bank to play

    void Reset() { *this = HwWaveChannel{}; }

    int16_t DacOutput(const uint8_t *waveRam) const {
      if (!enabled || !dacEnabled || volumeShift == 3)
        return 0;
      int byteIdx = pos / 2;
      int nibble = (pos & 1) ? (waveRam[byteIdx] & 0xF)
                             : ((waveRam[byteIdx] >> 4) & 0xF);
      int shifted = (volumeShift == 0) ? nibble : (nibble >> volumeShift);
      return (int16_t)((shifted * 2 - 15) * 256);
    }
  };

  struct HwNoiseChannel {
    int frequencyTimer = 0;
    int divisor = 0;
    int shiftAmount = 0;
    uint16_t lfsr = 0x7FFF;
    bool shortMode = false;
    int volume = 0;
    int envelopeInitVol = 0;
    int envelopePeriod = 0;
    int envelopeTimer = 0;
    bool envelopeIncrease = false;
    int lengthCounter = 0;
    bool lengthEnable = false;
    bool enabled = false;
    bool dacEnabled = false;

    void Reset() { *this = HwNoiseChannel{}; }

    int16_t DacOutput() const {
      if (!enabled || !dacEnabled)
        return 0;
      int sample = (~lfsr & 1) ? volume : 0;
      return (int16_t)((sample * 2 - 15) * 256);
    }
  };

  HwSquareChannel hwCh1, hwCh2;
  HwWaveChannel hwCh3;
  HwNoiseChannel hwCh4;

  // Frame sequencer: clocks at 512 Hz (every 32768 CPU cycles)
  int frameSequencerCycles = 0;
  int frameSequencerStep = 0;
  static constexpr int FRAME_SEQ_PERIOD = 32768; // CPU cycles per step

  void StepFrameSequencer();
  void ClockLength();
  void ClockEnvelope();
  void ClockSweep();
  int16_t MixPSGChannels();
  void TickPSGTimers(int cycles);

  // Read Wave RAM from IO registers (0x04000090-0x0400009F)
  void ReadWaveRam(uint8_t *out16bytes);

  // Wave channel (channel 3)
  struct WaveChannel {
    std::array<uint8_t, 32> wave; // 4-bit samples (0..15)
    int periodSamples = 0;        // number of output samples per wave nibble
    int pos = 0;                  // current wave index 0..31
    int stepCounter = 0;          // counts up to periodSamples
    int volume = 0;               // 0=100%,1=50%,2=25%,3=mute
    bool enabled = false;

    void Reset() {
      wave.fill(0);
      periodSamples = 0;
      pos = 0;
      stepCounter = 0;
      volume = 0;
      enabled = false;
    }

    int16_t Sample() const {
      if (!enabled || periodSamples <= 0)
        return 0;
      int nib = wave[pos] & 0x0F;
      float s = (nib / 15.0f) * 2.0f - 1.0f;
      float volScale = 0.0f;
      switch (volume) {
      case 0:
        volScale = 1.0f;
        break;
      case 1:
        volScale = 0.5f;
        break;
      case 2:
        volScale = 0.25f;
        break;
      default:
        volScale = 0.0f;
        break;
      }
      return int16_t(s * volScale * 30000);
    }

    void Advance() {
      if (periodSamples <= 0)
        return;
      stepCounter++;
      if (stepCounter >= periodSamples) {
        stepCounter = 0;
        pos = (pos + 1) % int(wave.size());
      }
    }
  };

  WaveChannel waveChannel;

  // Noise channel (channel 4)
  struct NoiseChannel {
    uint16_t lfsr = 0x7FFF; // 15-bit LFSR initial state
    int periodSamples = 0;  // number of output samples per LFSR step
    int stepCounter = 0;
    bool shortMode = false; // 7-bit mode when true
    int volume = 0;         // 0..15
    bool enabled = false;

    void Reset() {
      lfsr = 0x7FFF;
      periodSamples = 0;
      stepCounter = 0;
      shortMode = false;
      volume = 0;
      enabled = false;
    }

    int16_t Sample() const {
      if (!enabled || periodSamples <= 0)
        return 0;
      // Use bit0 as output: 0 -> +, 1 -> -
      int sign = (lfsr & 1) ? -1 : 1;
      return int16_t(sign * (volume / 15.0f) * 30000);
    }

    void Advance() {
      if (periodSamples <= 0)
        return;
      stepCounter++;
      if (stepCounter >= periodSamples) {
        stepCounter = 0;
        // LFSR feedback: newbit = bit0 XOR bit1
        uint16_t bit0 = lfsr & 1;
        uint16_t bit1 = (lfsr >> 1) & 1;
        uint16_t newbit = bit0 ^ bit1;
        lfsr = (lfsr >> 1) | (newbit << 14);
        if (shortMode) {
          // In short mode also set bit 6 to newbit
          lfsr = (lfsr & ~(1 << 6)) | (newbit << 6);
        }
      }
    }
  };

  NoiseChannel noiseChannel;

  // Upsample from FIFO rate (~16 kHz) to output rate (32768 Hz).
  // Each FIFO timer overflow produces ~2.05 output samples using
  // sample-and-hold, keeping FIFO consumption and output synchronized.
  static constexpr float GBA_CPU_FREQ = 16777216.0f;
  static constexpr float OUTPUT_SAMPLE_RATE = 32768.0f;
  float outputSampleRate = OUTPUT_SAMPLE_RATE;

  // Fractional accumulator for upsample: advances by 1.0 per timer
  // overflow, generates output samples until it's < 1.0.
  // Ratio = outputSampleRate / fifoTimerRate ≈ 2.048 for M4A games.
  float sampleAccumulator = 0.0f;
  float currentUpsampleRatio = 2.048f; // default, recalculated per timer

  // Ring buffer prefill: absorbs frame-boundary jitter between the
  // emulator thread (producing samples in bursts per frame) and the
  // SDL callback (consuming samples at a steady real-time rate).
  static constexpr int RING_PREFILL_SAMPLES = 1024;
  bool prefilled = false;

  // DC-blocking high-pass filter (simulates GBA coupling capacitor).
  // Matches mGBA's approach: cap = prev_sample - degraded * FILTER
  // FILTER = 65368 ≈ 0.9975 in Q16, giving ~13 Hz cutoff at 32 kHz.
  int32_t hpfCapL = 0;
  int32_t hpfCapR = 0;

  void GenerateOutputSample();
  void PushSample(int16_t left, int16_t right);
};

} // namespace AIO::Emulator::GBA
