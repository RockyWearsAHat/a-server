#pragma once

#include "PS1Constants.h"
#include "emulator/common/Loggable.h"
#include <array>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace AIO::Emulator::PS1 {

class PS1Memory;

// ADSR envelope phase
enum class ADSRPhase { Off, Attack, Decay, Sustain, Release };

struct SPUVoice {
  int16_t volumeL = 0;
  int16_t volumeR = 0;
  uint16_t sampleRate = 0;
  uint16_t startAddr = 0; // ÷8 for actual SPU RAM address
  uint16_t adsrLo = 0;
  uint16_t adsrHi = 0;
  int16_t adsrVolume = 0;
  uint16_t repeatAddr = 0;

  // Internal state
  ADSRPhase adsrPhase = ADSRPhase::Off;
  int32_t adsrLevel = 0;
  uint32_t currentAddr = 0;
  uint32_t sampleCounter = 0;      // Fixed-point sample position
  int16_t prevSamples[2] = {0, 0}; // ADPCM decode history
  bool keyOn = false;
  bool keyOff = false;
  bool loopFlag = false;
};

class PS1SPU : public Common::Loggable {
public:
  explicit PS1SPU(PS1Memory &memory);
  ~PS1SPU() = default;

  void Reset();

  // ─── Register Interface ─────────────────────────────────────────────
  uint16_t ReadRegister(uint32_t addr) const;
  void WriteRegister(uint32_t addr, uint16_t value);

  // ─── Timing ─────────────────────────────────────────────────────────
  void Tick(uint32_t cpuCycles);

  // ─── Audio Output ───────────────────────────────────────────────────
  // Fill stereo interleaved buffer, returns number of samples written
  uint32_t GetSamples(int16_t *buffer, uint32_t maxSamples);

  // ─── SPU RAM Access (for DMA) ───────────────────────────────────────
  uint16_t ReadSPURAM(uint32_t addr) const;
  void WriteSPURAM(uint32_t addr, uint16_t value);
  void DMAWrite(uint16_t value);
  uint16_t DMARead();

  // ─── Debug ──────────────────────────────────────────────────────────
  void DumpState(std::ostream &os) const;
  std::string GetDebugSummary() const;
  const SPUVoice &GetVoice(uint32_t index) const { return voices[index]; }

private:
  PS1Memory &memory;

  // ─── SPU RAM ────────────────────────────────────────────────────────
  std::vector<uint16_t> spuRAM;

  // ─── Voices ─────────────────────────────────────────────────────────
  std::array<SPUVoice, SPU::NUM_VOICES> voices{};

  // ─── Global Registers ───────────────────────────────────────────────
  int16_t mainVolumeL = 0;
  int16_t mainVolumeR = 0;
  int16_t reverbVolumeL = 0;
  int16_t reverbVolumeR = 0;
  uint32_t keyOnShadow = 0;
  uint32_t keyOffShadow = 0;
  uint32_t fmMode = 0;
  uint32_t noiseMode = 0;
  uint32_t reverbOn = 0;
  uint32_t voiceStatus = 0;
  uint16_t reverbBase = 0;
  uint16_t irqAddr = 0;
  uint16_t dataAddr = 0;
  uint16_t spuCtrl = 0;
  uint16_t spuStat = 0;
  uint16_t transferCtrl = 0;
  int16_t cdVolumeL = 0;
  int16_t cdVolumeR = 0;
  int16_t extVolumeL = 0;
  int16_t extVolumeR = 0;

  // Transfer address counter
  uint32_t transferAddr = 0;

  // ─── Timing ─────────────────────────────────────────────────────────
  uint32_t cycleAccumulator = 0;

  // ─── Voice Processing ───────────────────────────────────────────────
  int16_t DecodeSample(SPUVoice &voice);
  void UpdateADSR(SPUVoice &voice);
  void ProcessKeyOn();
  void ProcessKeyOff();

  // ─── ADPCM Decoding ─────────────────────────────────────────────────
  static void DecodeADPCMBlock(const uint8_t *block, int16_t *samples,
                               int16_t &prev1, int16_t &prev2);
};

} // namespace AIO::Emulator::PS1
