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
class InterruptController;

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
  uint32_t sampleCounter = 0;      // Fixed-point sample position (4.12)
  int16_t prevSamples[2] = {0, 0}; // ADPCM decode history
  bool keyOn = false;
  bool keyOff = false;
  bool loopFlag = false;

  // Decoded ADPCM block buffer
  int16_t decodedSamples[28] = {};
  uint32_t sampleIndex = 28; // Start past end to force first decode
};

class PS1SPU : public Common::Loggable {
public:
  PS1SPU(PS1Memory &memory, InterruptController &interrupts);
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
  // ─── XA-ADPCM Input ──────────────────────────────────────────────────
  // Called by CDROM with decoded stereo XA samples. sampleRate is 37800 or
  // 18900.
  void FeedXASamples(const int16_t *left, const int16_t *right, uint32_t count,
                     uint32_t sampleRate);
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
  InterruptController &interrupts;

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

  // ─── XA-ADPCM Input Buffer ──────────────────────────────────────────
  // Circular buffer fed by CDROM::DecodeXASector (must be power of 2)
  static constexpr uint32_t XA_BUFFER_CAPACITY = 8192;
  std::array<int16_t, XA_BUFFER_CAPACITY> xaBufL{};
  std::array<int16_t, XA_BUFFER_CAPACITY> xaBufR{};
  uint32_t xaWritePos = 0;
  uint32_t xaReadPos = 0;
  uint32_t xaCount = 0;          // Samples currently in buffer
  uint32_t xaSampleRate = 37800; // 37800 or 18900 Hz
  uint32_t xaFracAccum = 0;      // Fixed-point rate-conversion accumulator
  int16_t xaHeldL = 0;           // Most recently consumed XA sample (L)
  int16_t xaHeldR = 0;           // Most recently consumed XA sample (R)

  // ─── Reverb Configuration Registers (rev00..rev1F) ──────────────────
  // 32 signed 16-bit registers at 0x1F801DC0..0x1F801DFE
  // Indices match NOCASH PSX §SPU Reverb Registers:
  //  [0]=dAPF1  [1]=dAPF2  [2]=vIIR   [3]=vCOMB1  [4]=vCOMB2  [5]=vCOMB3
  //  [6]=vCOMB4 [7]=vWALL  [8]=vAPF1  [9]=vAPF2
  //  [10]=mLSAME [11]=mRSAME [12]=mLCOMB1 [13]=mRCOMB1
  //  [14]=mLCOMB2 [15]=mRCOMB2 [16]=dLSAME [17]=dRSAME
  //  [18]=mLDIFF [19]=mRDIFF [20]=mLCOMB3 [21]=mRCOMB3
  //  [22]=mLCOMB4 [23]=mRCOMB4 [24]=dLDIFF [25]=dRDIFF
  //  [26]=mLAPF1 [27]=mRAPF1 [28]=mLAPF2 [29]=mRAPF2
  //  [30]=vLIN   [31]=vRIN
  std::array<int16_t, 32> reverbRegs{};
  uint32_t reverbBufferAddr = 0; // Current reverb buffer byte address
  uint32_t reverbCycleAccum = 0; // Accumulates to trigger reverb at 22050Hz

  // ─── Noise Generator State ──────────────────────────────────────────
  // Per NOCASH PSX §SPU Noise Generator
  int16_t noiseLevel = 0;
  int32_t noiseTimer = 0; // Counts down; reloaded from (0x20000 >> noiseShift)

  // ─── FM (Pitch Modulation) State ────────────────────────────────────
  // Per NOCASH PSX §SPU ADPCM Pitch — VxOUTX(x-1) used by voice x
  std::array<int16_t, SPU::NUM_VOICES> lastVoiceOutput{};

  // Transfer address counter
  uint32_t transferAddr = 0;

  // IRQ latch — prevents re-firing until re-armed by irqAddr write or SPUCTRL
  bool spuIRQFired = false;

  // ─── Timing ─────────────────────────────────────────────────────────
  uint32_t cycleAccumulator = 0;

  // ─── Voice Processing ───────────────────────────────────────────────
  // voiceIndex argument carries FM source voice index for pitch modulation
  int16_t DecodeSample(SPUVoice &voice, uint32_t voiceIndex);
  void UpdateADSR(SPUVoice &voice);
  void ProcessKeyOn();
  void ProcessKeyOff();

  // ─── ADPCM Decoding ─────────────────────────────────────────────────
  static void DecodeADPCMBlock(const uint8_t *block, int16_t *samples,
                               int16_t &prev1, int16_t &prev2);

  // ─── Reverb Processing ──────────────────────────────────────────────
  // Per NOCASH PSX §SPU Reverb Formula. Operates at 22050Hz.
  void ProcessReverb(int32_t inL, int32_t inR, int32_t &outL, int32_t &outR);
  // Address access helpers (reg value in 8-byte units, relative to
  // reverbBufferAddr)
  int16_t ReadReverbBuf(int32_t s16Reg, int32_t extraBytes = 0) const;
  void WriteReverbBuf(int32_t s16Reg, int16_t value);
  uint32_t ReverbWrap(int32_t rawByteAddr) const;

  // ─── Noise Generator ────────────────────────────────────────────────
  void TickNoise(); // Called once per 44100Hz sample
};

} // namespace AIO::Emulator::PS1
