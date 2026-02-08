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

  // Handle writes to PSG sound registers (offsets 0x060..0x084, 0x090..0x09F)
  void WriteRegister(uint32_t offset, uint16_t value);

  // PSG helpers (for unit tests)
  void SetPSGChannelParams(int channel, int periodSamples, int duty,
                           int volume);
  std::vector<int16_t> GeneratePSGSamples(int channel, int numSamples);
  void SetPSGWaveRAM(const std::array<uint8_t, 32> &data);
  void SetPSGWaveParams(int periodSamples, int volume);
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

  // Last latched FIFO byte per channel (held between timer overflows)
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

  // Hardware-accurate PSG channel state (channels 1-4)
  // These structs store the raw register values and runtime state for
  // cycle-accurate sample generation driven from the frame sequencer.

  // Square wave channel (channels 1 & 2)
  struct PSGChannel {
    // Register shadow values
    int duty = 0;           // 0..3
    int lengthCounter = 0;  // 6-bit, counts down
    bool lengthEnabled = false;
    int envelopeInitVol = 0; // 0..15
    int envelopeDir = 0;     // 0=decrease, 1=increase
    int envelopePeriod = 0;  // 0..7 (0=disabled)
    int frequency = 0;      // 11-bit frequency register value

    // Channel 1 sweep (unused for channel 2)
    int sweepShift = 0;
    int sweepDir = 0;        // 0=increase, 1=decrease
    int sweepPeriod = 0;     // 0=disabled
    int sweepTimer = 0;
    int sweepFreqShadow = 0;
    bool sweepEnabled = false;
    bool sweepNegate = false; // set when negate was used

    // Runtime state
    int currentVolume = 0;
    int envelopeTimer = 0;
    int freqTimer = 0;       // counts down in CPU cycles
    int wavePos = 0;         // 0..7 position in 8-step duty table
    bool dacEnabled = false; // bits 3-7 of NRx2 nonzero
    bool channelEnabled = false;

    // Test-only compat fields
    int periodSamples = 0;
    int pos = 0;
    int volume = 0;
    bool enabled = false;

    void Reset() {
      duty = 0;
      lengthCounter = 0;
      lengthEnabled = false;
      envelopeInitVol = 0;
      envelopeDir = 0;
      envelopePeriod = 0;
      frequency = 0;
      sweepShift = 0;
      sweepDir = 0;
      sweepPeriod = 0;
      sweepTimer = 0;
      sweepFreqShadow = 0;
      sweepEnabled = false;
      sweepNegate = false;
      currentVolume = 0;
      envelopeTimer = 0;
      freqTimer = 0;
      wavePos = 0;
      dacEnabled = false;
      channelEnabled = false;
      periodSamples = 0;
      pos = 0;
      volume = 0;
      enabled = false;
    }

    // Raw DAC output: duty * currentVolume (0-15 range, matching mGBA)
    int16_t Sample() const {
      if (!channelEnabled || !dacEnabled)
        return 0;

      static constexpr uint8_t dutyTable[4] = {
          0b00000001, // 12.5%
          0b10000001, // 25%
          0b10000111, // 50%
          0b01111110, // 75%
      };
      int bit = (dutyTable[duty & 3] >> (wavePos & 7)) & 1;
      return static_cast<int16_t>(bit * currentVolume);
    }
  };

  std::array<PSGChannel, 2> psgChannels;

  // Wave channel (channel 3)
  struct WaveChannel {
    std::array<uint8_t, 16> waveRAM{}; // 16 bytes = 32 4-bit samples
    bool dimension = false; // 0=single bank, 1=two banks
    int bankNumber = 0;
    bool dacEnabled = false;
    int lengthCounter = 0;
    bool lengthEnabled = false;
    int volumeCode = 0;   // 0=mute, 1=100%, 2=50%, 3=25%
    bool forceVolume = false; // force 75%
    int frequency = 0;    // 11-bit
    bool channelEnabled = false;

    int freqTimer = 0;
    int wavePos = 0;      // 0..31 (nibble index)
    int currentSample = 0; // current 4-bit sample value

    // Test-only compat
    std::array<uint8_t, 32> wave{};
    int periodSamples = 0;
    int pos = 0;
    int stepCounter = 0;
    int volume = 0;
    bool enabled = false;

    void Reset() {
      waveRAM.fill(0);
      dimension = false;
      bankNumber = 0;
      dacEnabled = false;
      lengthCounter = 0;
      lengthEnabled = false;
      volumeCode = 0;
      forceVolume = false;
      frequency = 0;
      channelEnabled = false;
      freqTimer = 0;
      wavePos = 0;
      currentSample = 0;
      wave.fill(0);
      periodSamples = 0;
      pos = 0;
      stepCounter = 0;
      volume = 0;
      enabled = false;
    }

    // Raw DAC output: 4-bit sample with volume shift (matching mGBA)
    int16_t Sample() const {
      if (!channelEnabled || !dacEnabled)
        return 0;

      int sample = currentSample;

      // Volume shift (mGBA: volume 0=mute(>>4), 1=100%, 2=50%(>>1), 3=25%(>>2))
      if (forceVolume) {
        // 75% volume: multiply by 3, shift applied after in GBA mode
        sample = (sample * 3) / 4;
      } else {
        switch (volumeCode) {
        case 0: return 0; // mute
        case 1: break;    // 100%
        case 2: sample >>= 1; break; // 50%
        case 3: sample >>= 2; break; // 25%
        }
      }
      return static_cast<int16_t>(sample);
    }

    void Advance() {
      wavePos = (wavePos + 1) & 31;
      // Read the nibble from wave RAM
      int byteIdx = wavePos / 2;
      currentSample = (wavePos & 1) ? (waveRAM[byteIdx] & 0x0F)
                                    : ((waveRAM[byteIdx] >> 4) & 0x0F);
    }
  };

  WaveChannel waveChannel;

  // Noise channel (channel 4)
  struct NoiseChannel {
    int lengthCounter = 0;
    bool lengthEnabled = false;
    int envelopeInitVol = 0;
    int envelopeDir = 0;
    int envelopePeriod = 0;
    int divRatio = 0;       // 0..7
    bool widthMode = false; // 0=15-bit, 1=7-bit
    int shiftFreq = 0;      // 0..15
    bool dacEnabled = false;
    bool channelEnabled = false;

    int currentVolume = 0;
    int envelopeTimer = 0;
    uint16_t lfsr = 0;
    int freqTimer = 0;

    // Test-only compat
    int periodSamples = 0;
    int stepCounter = 0;
    bool shortMode = false;
    int volume = 0;
    bool enabled = false;

    void Reset() {
      lengthCounter = 0;
      lengthEnabled = false;
      envelopeInitVol = 0;
      envelopeDir = 0;
      envelopePeriod = 0;
      divRatio = 0;
      widthMode = false;
      shiftFreq = 0;
      dacEnabled = false;
      channelEnabled = false;
      currentVolume = 0;
      envelopeTimer = 0;
      lfsr = 0;
      freqTimer = 0;
      periodSamples = 0;
      stepCounter = 0;
      shortMode = false;
      volume = 0;
      enabled = false;
    }

    // mGBA: sample = lsb * volume, where lsb is the feedback bit.
    // After the shift, the output is determined by bit 0 of the LFSR.
    // With the ^1 inversion in feedback, bit 0 directly gives the sample.
    int16_t Sample() const {
      if (!channelEnabled || !dacEnabled)
        return 0;
      int bit = lfsr & 1;
      return static_cast<int16_t>(bit * currentVolume);
    }

    void Advance() {
      uint16_t lsb = (lfsr ^ (lfsr >> 1) ^ 1) & 1;
      lfsr >>= 1;
      if (lsb) {
        lfsr |= (1 << 14);
        if (widthMode)
          lfsr |= (1 << 6);
      } else if (widthMode) {
        lfsr &= ~(1 << 6);
      }
    }
  };

  NoiseChannel noiseChannel;

  // Frame sequencer: 512 Hz (every 32768 CPU cycles)
  // Steps 0-7, clocks length(all), envelope(ch1-4), sweep(ch1)
  static constexpr int FRAME_SEQ_PERIOD = 32768; // CPU cycles per step
  int frameSequencerCycles = 0;
  int frameSequencerStep = 0;

  // PSG cycle accumulators for frequency timers
  int psgCycleAccum = 0;

  // SOUNDCNT_L register value for PSG L/R routing and volume
  uint16_t soundcntL = 0;

  // Cached SOUNDBIAS fields to avoid redundant IO reads in GenerateDacSample()
  int cachedBiasMode = 0;
  int cachedBiasLevel = 0x200; // bits 0-9, default 0x200

  void TickFrameSequencer();
  void StepPSGFrequencyTimers(int cycles);
  int32_t GeneratePSGMixSample();

  // SOUNDBIAS-rate DAC: generates output at hardware sample rate
  // (32768/65536/131072/262144 Hz depending on SOUNDBIAS bits 14-15)
  int dacCycleAccumulator = 0;
  void GenerateDacSample();

  // Sample rate conversion: biquad anti-alias filter + linear interpolation
  double sampleAccumulator = 0.0;
  double currentResampleRatio = 1.0; // outputRate / dacRate
  static constexpr float OUTPUT_SAMPLE_RATE = 32768.0f;
  float outputSampleRate = OUTPUT_SAMPLE_RATE;
  static constexpr float GBA_CPU_FREQ = 16777216.0f;

  // Biquad low-pass filter state (second-order IIR, applied at DAC rate
  // before downsampling to prevent aliasing)
  struct BiquadState {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a1 = 0.0, a2 = 0.0;
    double x1 = 0.0, x2 = 0.0; // input history
    double y1 = 0.0, y2 = 0.0; // output history

    double Process(double input) {
      double output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
      x2 = x1;
      x1 = input;
      y2 = y1;
      y1 = output;
      return output;
    }

    void Reset() { x1 = x2 = y1 = y2 = 0.0; }
  };

  BiquadState lpfLeft, lpfRight;
  float lastConfiguredLpfRate = 0.0f;
  void ConfigureLPF(float dacRate, float outRate);

  // Previous filtered sample for linear interpolation
  int16_t prevLeft = 0;
  int16_t prevRight = 0;

  void PushSample(int16_t left, int16_t right);
};

} // namespace AIO::Emulator::GBA
