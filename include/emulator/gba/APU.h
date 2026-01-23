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

  // PSG helpers
  // Configure square-wave PSG channel parameters (channel 0 or 1)
  void SetPSGChannelParams(int channel, int periodSamples, int duty, int volume);
  // Generate raw PSG samples for testing/verification (mono)
  std::vector<int16_t> GeneratePSGSamples(int channel, int numSamples);
  // Wave channel helpers
  void SetPSGWaveRAM(const std::array<uint8_t, 32> &data);
  void SetPSGWaveParams(int periodSamples, int volume);

  // FIFO fill levels (for sound DMA request logic)
  int GetFifoACount() const { return fifoA_Count; }
  int GetFifoBCount() const { return fifoB_Count; }

  // Debug/telemetry (optional logging controlled by env vars)
  struct AudioStats {
    std::atomic<uint64_t> ringUnderrunSamples{0};
    std::atomic<uint64_t> ringOverrunDrops{0};
    std::atomic<uint64_t> fifoAUnderflows{0};
    std::atomic<uint64_t> fifoBUnderflows{0};
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

  // PSG channel state (channels 1 & 2)
  struct PSGChannel {
    int periodSamples = 0; // number of output samples per full period
    int pos = 0;           // current sample position within period
    int duty = 0;          // 0..3 corresponding to duty ratio
    int volume = 0;        // 0..15
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
      // duty mapping: 0=1/8, 1=1/4, 2=1/2, 3=3/4 high
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
      // scale to int16 using volume (0..15)
      int16_t amp = int16_t((high ? 1.0f : -1.0f) * (volume / 15.0f) * 30000);
      return amp;
    }
  };

  std::array<PSGChannel, 2> psgChannels;

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

  // Sample rate conversion
  float sampleAccumulator = 0.0f;
  float currentUpsampleRatio = 2.45f; // Default, updated based on timer
  static constexpr float OUTPUT_SAMPLE_RATE = 32768.0f;
  float outputSampleRate = OUTPUT_SAMPLE_RATE;
  static constexpr float GBA_CPU_FREQ = 16777216.0f;

  // Add a sample to the ring buffer
  void PushSample(int16_t left, int16_t right);
};

} // namespace AIO::Emulator::GBA
