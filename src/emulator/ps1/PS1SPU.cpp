#include "emulator/ps1/PS1SPU.h"
#include "emulator/ps1/InterruptController.h"
#include "emulator/ps1/PS1Memory.h"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace AIO::Emulator::PS1 {

PS1SPU::PS1SPU(PS1Memory &memory, InterruptController &interrupts)
    : Loggable("PS1.SPU"), memory(memory), interrupts(interrupts),
      spuRAM(MemSize::SPU_RAM / 2, 0) {} // SPU RAM in 16-bit words

void PS1SPU::Reset() {
  std::fill(spuRAM.begin(), spuRAM.end(), 0);

  for (auto &v : voices) {
    v = SPUVoice{};
  }

  mainVolumeL = 0;
  mainVolumeR = 0;
  reverbVolumeL = 0;
  reverbVolumeR = 0;
  keyOnShadow = 0;
  keyOffShadow = 0;
  fmMode = 0;
  noiseMode = 0;
  reverbOn = 0;
  voiceStatus = 0;
  reverbBase = 0;
  irqAddr = 0;
  dataAddr = 0;
  spuCtrl = 0;
  spuStat = 0;
  transferCtrl = 0;
  cdVolumeL = 0;
  cdVolumeR = 0;
  extVolumeL = 0;
  extVolumeR = 0;
  // Reset XA-ADPCM input buffer
  xaBufL.fill(0);
  xaBufR.fill(0);
  xaWritePos = 0;
  xaReadPos = 0;
  xaCount = 0;
  xaSampleRate = 37800;
  xaFracAccum = 0;
  xaHeldL = 0;
  xaHeldR = 0;
  transferAddr = 0;
  cycleAccumulator = 0;
  spuIRQFired = false;
  reverbRegs.fill(0);
  reverbBufferAddr = 0;
  reverbCycleAccum = 0;
  noiseLevel = 0;
  noiseTimer = 0;
  lastVoiceOutput.fill(0);
}

// ─── Register Interface ─────────────────────────────────────────────────

uint16_t PS1SPU::ReadRegister(uint32_t addr) const {
  // Voice registers: 0x1F801C00 - 0x1F801D7F
  if (addr >= IO::SPU_VOICE_BASE &&
      addr < IO::SPU_VOICE_BASE + SPU::NUM_VOICES * IO::SPU_VOICE_SIZE) {
    uint32_t voiceIndex = (addr - IO::SPU_VOICE_BASE) / IO::SPU_VOICE_SIZE;
    uint32_t reg = (addr - IO::SPU_VOICE_BASE) % IO::SPU_VOICE_SIZE;

    const auto &v = voices[voiceIndex];
    switch (reg) {
    case 0x00:
      return static_cast<uint16_t>(v.volumeL);
    case 0x02:
      return static_cast<uint16_t>(v.volumeR);
    case 0x04:
      return v.sampleRate;
    case 0x06:
      return v.startAddr;
    case 0x08:
      return v.adsrLo;
    case 0x0A:
      return v.adsrHi;
    case 0x0C:
      return static_cast<uint16_t>(v.adsrVolume);
    case 0x0E:
      return v.repeatAddr;
    default:
      return 0;
    }
  }

  // Global registers
  // Reverb config registers: 0x1F801DC0 - 0x1F801DFE (NOCASH PSX §SPU)
  if (addr >= IO::SPU_REVERB_CONFIG_BASE && addr <= IO::SPU_REVERB_CONFIG_END) {
    uint32_t regIndex = (addr - IO::SPU_REVERB_CONFIG_BASE) / 2;
    if (regIndex < 32)
      return static_cast<uint16_t>(reverbRegs[regIndex]);
    return 0;
  }

  switch (addr) {
  case IO::SPU_MAIN_VOL_L:
    return static_cast<uint16_t>(mainVolumeL);
  case IO::SPU_MAIN_VOL_R:
    return static_cast<uint16_t>(mainVolumeR);
  case IO::SPU_REVERB_VOL_L:
    return static_cast<uint16_t>(reverbVolumeL);
  case IO::SPU_REVERB_VOL_R:
    return static_cast<uint16_t>(reverbVolumeR);
  case IO::SPU_KEY_ON:
    return static_cast<uint16_t>(keyOnShadow);
  case IO::SPU_KEY_ON + 2:
    return static_cast<uint16_t>(keyOnShadow >> 16);
  case IO::SPU_KEY_OFF:
    return static_cast<uint16_t>(keyOffShadow);
  case IO::SPU_KEY_OFF + 2:
    return static_cast<uint16_t>(keyOffShadow >> 16);
  case IO::SPU_FM_MODE:
    return static_cast<uint16_t>(fmMode);
  case IO::SPU_FM_MODE + 2:
    return static_cast<uint16_t>(fmMode >> 16);
  case IO::SPU_NOISE_MODE:
    return static_cast<uint16_t>(noiseMode);
  case IO::SPU_NOISE_MODE + 2:
    return static_cast<uint16_t>(noiseMode >> 16);
  case IO::SPU_REVERB_ON:
    return static_cast<uint16_t>(reverbOn);
  case IO::SPU_REVERB_ON + 2:
    return static_cast<uint16_t>(reverbOn >> 16);
  case IO::SPU_VOICE_STATUS:
    return static_cast<uint16_t>(voiceStatus);
  case IO::SPU_VOICE_STATUS + 2:
    return static_cast<uint16_t>(voiceStatus >> 16);
  case IO::SPU_REVERB_BASE:
    return reverbBase;
  case IO::SPU_IRQ_ADDR:
    return irqAddr;
  case IO::SPU_DATA_ADDR:
    return dataAddr;
  case IO::SPU_CTRL:
    return spuCtrl;
  case IO::SPU_STATUS:
    return spuStat;
  case IO::SPU_TRANSFER_CTRL:
    return transferCtrl;
  case IO::SPU_CD_VOL_L:
    return static_cast<uint16_t>(cdVolumeL);
  case IO::SPU_CD_VOL_R:
    return static_cast<uint16_t>(cdVolumeR);
  default:
    if constexpr (Trace::SPU_TRACE) {
      LogWarn("Unhandled SPU read addr=%08X", addr);
    }
    return 0;
  }
}

void PS1SPU::WriteRegister(uint32_t addr, uint16_t value) {
  // Voice registers
  if (addr >= IO::SPU_VOICE_BASE &&
      addr < IO::SPU_VOICE_BASE + SPU::NUM_VOICES * IO::SPU_VOICE_SIZE) {
    uint32_t voiceIndex = (addr - IO::SPU_VOICE_BASE) / IO::SPU_VOICE_SIZE;
    uint32_t reg = (addr - IO::SPU_VOICE_BASE) % IO::SPU_VOICE_SIZE;

    auto &v = voices[voiceIndex];
    switch (reg) {
    case 0x00:
      v.volumeL = static_cast<int16_t>(value);
      break;
    case 0x02:
      v.volumeR = static_cast<int16_t>(value);
      break;
    case 0x04:
      v.sampleRate = value;
      break;
    case 0x06:
      v.startAddr = value;
      break;
    case 0x08:
      v.adsrLo = value;
      break;
    case 0x0A:
      v.adsrHi = value;
      break;
    case 0x0C:
      v.adsrVolume = static_cast<int16_t>(value);
      break;
    case 0x0E:
      v.repeatAddr = value;
      break;
    }
    return;
  }

  // Reverb config registers: 0x1F801DC0 - 0x1F801DFE (NOCASH PSX §SPU)
  if (addr >= IO::SPU_REVERB_CONFIG_BASE && addr <= IO::SPU_REVERB_CONFIG_END) {
    uint32_t regIndex = (addr - IO::SPU_REVERB_CONFIG_BASE) / 2;
    if (regIndex < 32)
      reverbRegs[regIndex] = static_cast<int16_t>(value);
    return;
  }

  // Global registers
  switch (addr) {
  case IO::SPU_MAIN_VOL_L:
    mainVolumeL = static_cast<int16_t>(value);
    break;
  case IO::SPU_MAIN_VOL_R:
    mainVolumeR = static_cast<int16_t>(value);
    break;
  case IO::SPU_REVERB_VOL_L:
    reverbVolumeL = static_cast<int16_t>(value);
    break;
  case IO::SPU_REVERB_VOL_R:
    reverbVolumeR = static_cast<int16_t>(value);
    break;
  case IO::SPU_KEY_ON:
    keyOnShadow = (keyOnShadow & 0xFFFF0000) | value;
    ProcessKeyOn();
    break;
  case IO::SPU_KEY_ON + 2:
    keyOnShadow =
        (keyOnShadow & 0x0000FFFF) | (static_cast<uint32_t>(value) << 16);
    ProcessKeyOn();
    break;
  case IO::SPU_KEY_OFF:
    keyOffShadow = (keyOffShadow & 0xFFFF0000) | value;
    ProcessKeyOff();
    break;
  case IO::SPU_KEY_OFF + 2:
    keyOffShadow =
        (keyOffShadow & 0x0000FFFF) | (static_cast<uint32_t>(value) << 16);
    ProcessKeyOff();
    break;
  case IO::SPU_FM_MODE:
    fmMode = (fmMode & 0xFFFF0000) | value;
    break;
  case IO::SPU_FM_MODE + 2:
    fmMode = (fmMode & 0x0000FFFF) | (static_cast<uint32_t>(value) << 16);
    break;
  case IO::SPU_NOISE_MODE:
    noiseMode = (noiseMode & 0xFFFF0000) | value;
    break;
  case IO::SPU_NOISE_MODE + 2:
    noiseMode = (noiseMode & 0x0000FFFF) | (static_cast<uint32_t>(value) << 16);
    break;
  case IO::SPU_REVERB_ON:
    reverbOn = (reverbOn & 0xFFFF0000) | value;
    break;
  case IO::SPU_REVERB_ON + 2:
    reverbOn = (reverbOn & 0x0000FFFF) | (static_cast<uint32_t>(value) << 16);
    break;
  case IO::SPU_REVERB_BASE:
    reverbBase = value;
    // mBASE write also resets the circular buffer pointer (NOCASH PSX)
    reverbBufferAddr = static_cast<uint32_t>(value) * 8;
    break;
  case IO::SPU_IRQ_ADDR:
    irqAddr = value;
    // Writing a new IRQ address re-arms the IRQ so the next address match
    // fires.
    spuIRQFired = false;
    spuStat &= ~0x40;
    break;
  case IO::SPU_DATA_ADDR:
    dataAddr = value;
    transferAddr = static_cast<uint32_t>(value) * 8;
    break;
  case IO::SPU_DATA_FIFO:
    DMAWrite(value);
    break;
  case IO::SPU_CTRL: {
    uint16_t prev = spuCtrl;
    spuCtrl = value;
    // Update status to mirror some control bits
    spuStat = (spuStat & ~0x3F) | (value & 0x3F);
    // Re-arm IRQ when bit 6 transitions from 0 to 1 (game re-enables SPU IRQ)
    if (!(prev & 0x40) && (value & 0x40)) {
      spuIRQFired = false;
      spuStat &= ~0x40;
    }
    // Disable IRQ clears the latch
    if (!(value & 0x40)) {
      spuIRQFired = false;
      spuStat &= ~0x40;
    }
    break;
  }
  case IO::SPU_TRANSFER_CTRL:
    transferCtrl = value;
    break;
  case IO::SPU_CD_VOL_L:
    cdVolumeL = static_cast<int16_t>(value);
    break;
  case IO::SPU_CD_VOL_R:
    cdVolumeR = static_cast<int16_t>(value);
    break;
  case IO::SPU_VOICE_STATUS:
    voiceStatus &= 0xFFFF0000;
    break;
  case IO::SPU_VOICE_STATUS + 2:
    voiceStatus &= 0x0000FFFF;
    break;
  default:
    if constexpr (Trace::SPU_TRACE) {
      LogWarn("Unhandled SPU write addr=%08X val=%04X", addr, value);
    }
    break;
  }
}

// ─── SPU RAM Access ─────────────────────────────────────────────────────

uint16_t PS1SPU::ReadSPURAM(uint32_t addr) const {
  addr = (addr / 2) % spuRAM.size();
  return spuRAM[addr];
}

void PS1SPU::WriteSPURAM(uint32_t addr, uint16_t value) {
  addr = (addr / 2) % spuRAM.size();
  spuRAM[addr] = value;
}

void PS1SPU::DMAWrite(uint16_t value) {
  uint32_t wordAddr = (transferAddr / 2) % spuRAM.size();
  spuRAM[wordAddr] = value;
  transferAddr += 2;
}

uint16_t PS1SPU::DMARead() {
  uint32_t wordAddr = (transferAddr / 2) % spuRAM.size();
  uint16_t value = spuRAM[wordAddr];
  transferAddr += 2;
  return value;
}

// ─── Key On / Key Off ──────────────────────────────────────────────────

void PS1SPU::ProcessKeyOn() {
  for (uint32_t i = 0; i < SPU::NUM_VOICES; i++) {
    if (keyOnShadow & (1u << i)) {
      auto &v = voices[i];
      v.keyOn = true;
      v.keyOff = false;
      v.adsrPhase = ADSRPhase::Attack;
      v.adsrLevel = 0;
      v.currentAddr = static_cast<uint32_t>(v.startAddr) * 8;
      voiceStatus &= ~(1u << i);
      v.sampleCounter = 0;
      v.prevSamples[0] = 0;
      v.prevSamples[1] = 0;

      if constexpr (Trace::SPU_TRACE) {
        LogDebug("KeyOn voice %u addr=%08X rate=%04X", i, v.currentAddr,
                 v.sampleRate);
      }
    }
  }
  keyOnShadow = 0;
}

void PS1SPU::ProcessKeyOff() {
  for (uint32_t i = 0; i < SPU::NUM_VOICES; i++) {
    if (keyOffShadow & (1u << i)) {
      voices[i].keyOff = true;
      voices[i].adsrPhase = ADSRPhase::Release;

      if constexpr (Trace::SPU_TRACE) {
        LogDebug("KeyOff voice %u", i);
      }
    }
  }
  keyOffShadow = 0;
}

// ─── Timing ─────────────────────────────────────────────────────────────

void PS1SPU::Tick(uint32_t cpuCycles) {
  cycleAccumulator += cpuCycles;
  // Generate one sample per (CPU_HZ / SAMPLE_RATE) cycles ≈ 768
  constexpr uint32_t cyclesPerSample = Clock::CPU_HZ / Clock::SPU_SAMPLE_RATE;
  while (cycleAccumulator >= cyclesPerSample) {
    cycleAccumulator -= cyclesPerSample;
    // Advance noise LFSR each 44100Hz sample (NOCASH PSX)
    TickNoise();
    // Process one sample tick for all voices
    for (uint32_t i = 0; i < SPU::NUM_VOICES; i++) {
      if (voices[i].adsrPhase != ADSRPhase::Off) {
        UpdateADSR(voices[i]);
      }
    }
  }
}

// ─── Audio Output ──────────────────────────────────────────────────────

uint32_t PS1SPU::GetSamples(int16_t *buffer, uint32_t maxSamples) {
  // Mix all active voices into stereo interleaved buffer
  for (uint32_t s = 0; s < maxSamples; s++) {
    int32_t reverbInL = 0;
    int32_t reverbInR = 0;
    int32_t mixL = 0;
    int32_t mixR = 0;

    for (uint32_t v = 0; v < SPU::NUM_VOICES; v++) {
      if (voices[v].adsrPhase == ADSRPhase::Off) {
        lastVoiceOutput[v] = 0;
        continue;
      }

      uint16_t effectiveSampleRate = voices[v].sampleRate;

      // FM pitch modulation: factor from previous voice amplitude (NOCASH PSX)
      if (v > 0 && (fmMode & (1u << v))) {
        int32_t factor = static_cast<int32_t>(lastVoiceOutput[v - 1]) + 0x8000;
        int32_t step =
            (static_cast<int32_t>(voices[v].sampleRate) * factor) >> 15;
        if (step > 0x3FFF)
          step = 0x4000;
        if (step < 0)
          step = 0;
        effectiveSampleRate = static_cast<uint16_t>(step);
      }

      int16_t sample;
      // Noise output: substitute LFSR level for ADPCM sample (NOCASH PSX)
      if (noiseMode & (1u << v)) {
        // Advance sampleCounter as usual so voice timing stays consistent
        voices[v].sampleCounter += effectiveSampleRate;
        while (voices[v].sampleCounter >= SPU::SAMPLE_RATE_BASE)
          voices[v].sampleCounter -= SPU::SAMPLE_RATE_BASE;
        sample = noiseLevel;
      } else {
        uint16_t savedRate = voices[v].sampleRate;
        voices[v].sampleRate = effectiveSampleRate;
        sample = DecodeSample(voices[v], v);
        voices[v].sampleRate = savedRate;
      }

      int32_t vol = voices[v].adsrLevel;
      int32_t scaledSample = (static_cast<int32_t>(sample) * vol) >> 15;
      lastVoiceOutput[v] =
          static_cast<int16_t>(std::clamp(scaledSample, -32768, 32767));

      int32_t voiceL = (scaledSample * voices[v].volumeL) >> 15;
      int32_t voiceR = (scaledSample * voices[v].volumeR) >> 15;
      mixL += voiceL;
      mixR += voiceR;

      // Accumulate reverb input for voices with reverb enabled
      if (reverbOn & (1u << v)) {
        reverbInL += voiceL;
        reverbInR += voiceR;
      }
    }

    // Process reverb at the SPU sample rate (using cycle accumulator)
    reverbCycleAccum++;
    if (reverbCycleAccum >= 2) {
      reverbCycleAccum = 0;
      int32_t reverbL = 0, reverbR = 0;
      ProcessReverb(reverbInL, reverbInR, reverbL, reverbR);
      mixL += reverbL;
      mixR += reverbR;
    }

    // Mix XA-ADPCM (CD audio) — rate-convert from xaSampleRate to 44100 Hz.
    // Consume one XA sample for every (44100 / xaSampleRate) output samples.
    xaFracAccum += xaSampleRate;
    while (xaFracAccum >= Clock::SPU_SAMPLE_RATE) {
      xaFracAccum -= Clock::SPU_SAMPLE_RATE;
      if (xaCount > 0) {
        xaHeldL = xaBufL[xaReadPos];
        xaHeldR = xaBufR[xaReadPos];
        xaReadPos = (xaReadPos + 1) & (XA_BUFFER_CAPACITY - 1);
        xaCount--;
      }
    }
    mixL += (static_cast<int32_t>(xaHeldL) * cdVolumeL) >> 15;
    mixR += (static_cast<int32_t>(xaHeldR) * cdVolumeR) >> 15;

    // Apply main volume
    mixL = (mixL * mainVolumeL) >> 15;
    mixR = (mixR * mainVolumeR) >> 15;
    buffer[s * 2] = static_cast<int16_t>(std::clamp(mixL, -32768, 32767));
    buffer[s * 2 + 1] = static_cast<int16_t>(std::clamp(mixR, -32768, 32767));
  }
  return maxSamples;
}

// ─── XA-ADPCM Input ──────────────────────────────────────────────────────
// Called by CDROM decoder to push decoded stereo XA samples into the circular
// buffer.  Rate conversion to 44100 Hz happens in GetSamples().

void PS1SPU::FeedXASamples(const int16_t *left, const int16_t *right,
                           uint32_t count, uint32_t sampleRate) {
  xaSampleRate = sampleRate;
  for (uint32_t i = 0; i < count; i++) {
    if (xaCount < XA_BUFFER_CAPACITY) {
      xaBufL[xaWritePos] = left[i];
      xaBufR[xaWritePos] = right[i];
      xaWritePos = (xaWritePos + 1) & (XA_BUFFER_CAPACITY - 1);
      xaCount++;
    }
    // Buffer full: silently drop. Keeps audio in sync even if the host
    // outruns the consumer (no burst of old samples on next drain).
  }
}

// ─── Noise Generator (NOCASH PSX §SPU Noise) ─────────────────────────────

void PS1SPU::TickNoise() {
  // LFSR: ParityBit = Level.Bit15 XOR Bit12 XOR Bit11 XOR Bit10 XOR 1
  // IF Timer < 0: Level = Level*2 + ParityBit; Timer += (0x20000 >> NoiseShift)
  uint32_t noiseShift = (spuCtrl >> 8) & 0x3F;
  int32_t reloadPeriod = static_cast<int32_t>(0x20000u >> noiseShift);
  noiseTimer -= 1;
  if (noiseTimer < 0) {
    uint16_t lvl = static_cast<uint16_t>(noiseLevel);
    int parity =
        (((lvl >> 15) ^ (lvl >> 12) ^ (lvl >> 11) ^ (lvl >> 10)) & 1) ^ 1;
    noiseLevel =
        static_cast<int16_t>(static_cast<uint16_t>((lvl << 1) | parity));
    noiseTimer += reloadPeriod;
  }
}

// ─── Reverb Engine (NOCASH PSX §SPU Reverb Formula) ─────────────────────

// Reverb register indices (from the 32 reverb config regs at 0x1F801DC0):
enum ReverbReg {
  vIIR = 0,
  vCOMB1,
  vCOMB2,
  vCOMB3,
  vCOMB4,
  vWALL,
  vAPF1,
  vAPF2,
  mLSAME,
  mRSAME,
  mLCOMB1,
  mRCOMB1,
  mLCOMB2,
  mRCOMB2,
  dLSAME,
  dRSAME,
  mLDIFF,
  mRDIFF,
  mLCOMB3,
  mRCOMB3,
  mLCOMB4,
  mRCOMB4,
  dLDIFF,
  dRDIFF,
  mLAPF1,
  mRAPF1,
  mLAPF2,
  mRAPF2,
  vLIN,
  vRIN,
  vLOUT,
  vROUT
};

uint32_t PS1SPU::ReverbWrap(int32_t rawByteAddr) const {
  uint32_t base = static_cast<uint32_t>(reverbBase) * 8;
  uint32_t spuSize = static_cast<uint32_t>(spuRAM.size()) * 2; // in bytes
  if (base >= spuSize)
    base = 0;
  uint32_t bufSize = spuSize - base;
  if (bufSize == 0)
    return 0;
  // Wrap within [base .. spuSize)
  int32_t offset = rawByteAddr % static_cast<int32_t>(bufSize);
  if (offset < 0)
    offset += static_cast<int32_t>(bufSize);
  return base + static_cast<uint32_t>(offset);
}

int16_t PS1SPU::ReadReverbBuf(int32_t s16RegIndex, int32_t extraBytes) const {
  // s16RegIndex: 0-based index into reverbRegs[]
  // address = reverbBufferAddr + reverbRegs[s16RegIndex]*8 + extraBytes,
  // wrapped
  int32_t offset =
      static_cast<int32_t>(reverbRegs[s16RegIndex]) * 8 + extraBytes;
  int32_t rawAddr = static_cast<int32_t>(reverbBufferAddr) + offset;
  uint32_t addr = ReverbWrap(rawAddr);
  uint32_t wordAddr = (addr / 2) % spuRAM.size();
  return static_cast<int16_t>(spuRAM[wordAddr]);
}

void PS1SPU::WriteReverbBuf(int32_t s16RegIndex, int16_t value) {
  int32_t offset = static_cast<int32_t>(reverbRegs[s16RegIndex]) * 8;
  int32_t rawAddr = static_cast<int32_t>(reverbBufferAddr) + offset;
  uint32_t addr = ReverbWrap(rawAddr);
  uint32_t wordAddr = (addr / 2) % spuRAM.size();
  spuRAM[wordAddr] = static_cast<uint16_t>(value);
}

void PS1SPU::ProcessReverb(int32_t inputL, int32_t inputR, int32_t &outL,
                           int32_t &outR) {
  // Full NOCASH PSX reverb formula
  // All multiplications are 1.15 fixed-point (>>15 for result)
  auto mul15 = [](int32_t a, int16_t b) -> int32_t {
    return (a * static_cast<int32_t>(b)) >> 15;
  };

  int32_t Lin = mul15(inputL, reverbRegs[vLIN]);
  int32_t Rin = mul15(inputR, reverbRegs[vRIN]);

  // Same-side reflections
  int16_t tmpL = static_cast<int16_t>(std::clamp(
      mul15(Lin + mul15(ReadReverbBuf(dLSAME, 0), reverbRegs[vWALL]) -
                ReadReverbBuf(mLSAME, -2),
            reverbRegs[vIIR]) +
          ReadReverbBuf(mLSAME, -2),
      -32768, 32767));
  WriteReverbBuf(mLSAME, tmpL);

  int16_t tmpR = static_cast<int16_t>(std::clamp(
      mul15(Rin + mul15(ReadReverbBuf(dRSAME, 0), reverbRegs[vWALL]) -
                ReadReverbBuf(mRSAME, -2),
            reverbRegs[vIIR]) +
          ReadReverbBuf(mRSAME, -2),
      -32768, 32767));
  WriteReverbBuf(mRSAME, tmpR);

  // Cross reflections
  int16_t tmpLD = static_cast<int16_t>(std::clamp(
      mul15(Lin + mul15(ReadReverbBuf(dRDIFF, 0), reverbRegs[vWALL]) -
                ReadReverbBuf(mLDIFF, -2),
            reverbRegs[vIIR]) +
          ReadReverbBuf(mLDIFF, -2),
      -32768, 32767));
  WriteReverbBuf(mLDIFF, tmpLD);

  int16_t tmpRD = static_cast<int16_t>(std::clamp(
      mul15(Rin + mul15(ReadReverbBuf(dLDIFF, 0), reverbRegs[vWALL]) -
                ReadReverbBuf(mRDIFF, -2),
            reverbRegs[vIIR]) +
          ReadReverbBuf(mRDIFF, -2),
      -32768, 32767));
  WriteReverbBuf(mRDIFF, tmpRD);

  // Comb filters
  int32_t Lout = mul15(ReadReverbBuf(mLCOMB1, 0), reverbRegs[vCOMB1]) +
                 mul15(ReadReverbBuf(mLCOMB2, 0), reverbRegs[vCOMB2]) +
                 mul15(ReadReverbBuf(mLCOMB3, 0), reverbRegs[vCOMB3]) +
                 mul15(ReadReverbBuf(mLCOMB4, 0), reverbRegs[vCOMB4]);
  int32_t Rout = mul15(ReadReverbBuf(mRCOMB1, 0), reverbRegs[vCOMB1]) +
                 mul15(ReadReverbBuf(mRCOMB2, 0), reverbRegs[vCOMB2]) +
                 mul15(ReadReverbBuf(mRCOMB3, 0), reverbRegs[vCOMB3]) +
                 mul15(ReadReverbBuf(mRCOMB4, 0), reverbRegs[vCOMB4]);

  // All-pass filter 1
  Lout -=
      mul15(ReadReverbBuf(mLAPF1, static_cast<int32_t>(reverbRegs[vAPF1]) * -8),
            reverbRegs[vAPF1]);
  int16_t apf1L = static_cast<int16_t>(std::clamp(Lout, -32768, 32767));
  WriteReverbBuf(mLAPF1, apf1L);
  Lout = mul15(Lout, reverbRegs[vAPF1]) +
         ReadReverbBuf(mLAPF1, static_cast<int32_t>(reverbRegs[vAPF1]) * -8);

  Rout -=
      mul15(ReadReverbBuf(mRAPF1, static_cast<int32_t>(reverbRegs[vAPF1]) * -8),
            reverbRegs[vAPF1]);
  int16_t apf1R = static_cast<int16_t>(std::clamp(Rout, -32768, 32767));
  WriteReverbBuf(mRAPF1, apf1R);
  Rout = mul15(Rout, reverbRegs[vAPF1]) +
         ReadReverbBuf(mRAPF1, static_cast<int32_t>(reverbRegs[vAPF1]) * -8);

  // All-pass filter 2
  Lout -=
      mul15(ReadReverbBuf(mLAPF2, static_cast<int32_t>(reverbRegs[vAPF2]) * -8),
            reverbRegs[vAPF2]);
  int16_t apf2L = static_cast<int16_t>(std::clamp(Lout, -32768, 32767));
  WriteReverbBuf(mLAPF2, apf2L);
  Lout = mul15(Lout, reverbRegs[vAPF2]) +
         ReadReverbBuf(mLAPF2, static_cast<int32_t>(reverbRegs[vAPF2]) * -8);

  Rout -=
      mul15(ReadReverbBuf(mRAPF2, static_cast<int32_t>(reverbRegs[vAPF2]) * -8),
            reverbRegs[vAPF2]);
  int16_t apf2R = static_cast<int16_t>(std::clamp(Rout, -32768, 32767));
  WriteReverbBuf(mRAPF2, apf2R);
  Rout = mul15(Rout, reverbRegs[vAPF2]) +
         ReadReverbBuf(mRAPF2, static_cast<int32_t>(reverbRegs[vAPF2]) * -8);

  // Output volume scaling
  outL = mul15(Lout, reverbRegs[vLOUT]);
  outR = mul15(Rout, reverbRegs[vROUT]);

  // Advance circular buffer pointer (wraps within reverb buffer region)
  uint32_t newAddr = reverbBufferAddr + 2;
  // Wrap to mBASE if we've exceeded SPU RAM or the buffer base
  uint32_t topAddr = static_cast<uint32_t>(spuRAM.size()) * 2;
  if (newAddr >= topAddr)
    newAddr = static_cast<uint32_t>(reverbBase) * 8;
  reverbBufferAddr = newAddr;
}

int16_t PS1SPU::DecodeSample(SPUVoice &voice, uint32_t voiceIndex) {
  // Advance sample position using fixed-point counter
  voice.sampleCounter += voice.sampleRate;

  // When counter overflows the base rate, move to next sample
  while (voice.sampleCounter >= SPU::SAMPLE_RATE_BASE) {
    voice.sampleCounter -= SPU::SAMPLE_RATE_BASE;
    voice.sampleIndex++;

    // Need to decode a new ADPCM block (28 samples per 16-byte block)
    if (voice.sampleIndex >= 28) {
      voice.sampleIndex = 0;

      // Read the 16-byte ADPCM block from SPU RAM (wrap within bounds)
      uint32_t blockAddr = voice.currentAddr;

      // SPU IRQ fires when a voice reads from the programmed IRQ address.
      // Uses a latch so it fires once per re-arm (game re-arms by writing
      // a new IRQ address or toggling SPUCTRL bit 6).
      if ((spuCtrl & 0x40) && !spuIRQFired &&
          (blockAddr == static_cast<uint32_t>(irqAddr) * 8)) {
        spuIRQFired = true;
        spuStat |= 0x40;
        interrupts.RequestIRQ(IRQ::SPU);
      }

      uint8_t block[16];
      for (int i = 0; i < 16; i++) {
        uint32_t byteAddr = blockAddr + i;
        uint32_t wordAddr = (byteAddr / 2) % spuRAM.size();
        uint16_t word = spuRAM[wordAddr];
        block[i] = byteAddr & 1 ? (word >> 8) : (word & 0xFF);
      }

      // Check flags (byte 1): bit 0=loop end, bit 1=loop repeat, bit 2=loop
      // start
      uint8_t flags = block[1];
      if (flags & 0x04) {
        // Loop start — save this address as repeat point
        voice.repeatAddr = static_cast<uint16_t>(voice.currentAddr / 8);
      }

      DecodeADPCMBlock(block, voice.decodedSamples, voice.prevSamples[0],
                       voice.prevSamples[1]);

      // Advance to next block, wrapping within SPU RAM (512KB = 0x80000 bytes)
      voice.currentAddr = (voice.currentAddr + 16) % (spuRAM.size() * 2);

      if (flags & 0x01) {
        voiceStatus |= (1u << voiceIndex);
        if (flags & 0x02) {
          // Loop repeat — jump back to loop start
          voice.currentAddr = static_cast<uint32_t>(voice.repeatAddr) * 8;
        } else {
          // No loop — silence and stop
          voice.adsrPhase = ADSRPhase::Off;
          voice.adsrLevel = 0;
          return 0;
        }
      }
    }
  }

  // Return the current decoded sample
  if (voice.sampleIndex < 28) {
    return voice.decodedSamples[voice.sampleIndex];
  }
  return 0;
}

void PS1SPU::DecodeADPCMBlock(const uint8_t *block, int16_t *samples,
                              int16_t &prev1, int16_t &prev2) {
  // PS1 ADPCM: 16 bytes → 28 samples
  // Byte 0: shift/filter
  // Byte 1: flags (loop start/end/repeat)
  // Bytes 2-15: packed nibbles

  uint8_t shift = block[0] & 0x0F;
  uint8_t filter = (block[0] >> 4) & 0x07;

  // ADPCM filter coefficients (fixed-point, ÷64)
  static constexpr int32_t filterPos[5] = {0, 60, 115, 98, 122};
  static constexpr int32_t filterNeg[5] = {0, 0, -52, -55, -60};

  int32_t f0 = (filter < 5) ? filterPos[filter] : 0;
  int32_t f1 = (filter < 5) ? filterNeg[filter] : 0;

  for (int i = 0; i < 28; i++) {
    // 2 nibbles per byte, starting at byte 2
    uint8_t byte = block[2 + i / 2];
    int32_t nibble;
    if (i & 1) {
      nibble = static_cast<int32_t>(byte >> 4);
    } else {
      nibble = static_cast<int32_t>(byte & 0x0F);
    }

    // Sign-extend nibble
    if (nibble >= 8)
      nibble -= 16;

    // Apply shift and filter
    int32_t sample =
        (nibble << (12 - shift)) + (prev1 * f0 + prev2 * f1 + 32) / 64;

    // Clamp to 16-bit
    sample = std::clamp(sample, -32768, 32767);

    samples[i] = static_cast<int16_t>(sample);
    prev2 = prev1;
    prev1 = static_cast<int16_t>(sample);
  }
}

void PS1SPU::UpdateADSR(SPUVoice &voice) {
  // PS1 ADSR register layout:
  // adsrLo (16 bits): sustain_level[3:0] | decay_shift[7:4] | attack_step[9:8]
  // | attack_shift[14:10] | attack_mode[15] adsrHi (16 bits):
  // release_shift[4:0] | release_mode[5] | (unused[12:6]) | sustain_step[15:14]
  // | sustain_shift[13] | sustain_dir[14] | sustain_mode[15] Simplified but
  // correct field extraction:

  switch (voice.adsrPhase) {
  case ADSRPhase::Attack: {
    bool attackMode = (voice.adsrLo >> 15) & 1; // 0=linear, 1=exponential
    int32_t attackShift = (voice.adsrLo >> 10) & 0x1F;
    int32_t attackStep = (voice.adsrLo >> 8) & 3;

    int32_t step = 7 - attackStep;
    if (attackShift < 11) {
      step <<= (11 - attackShift);
    } else {
      step >>= (attackShift - 11);
    }
    if (step < 1)
      step = 1;

    // Exponential attack slows down above 0x6000
    if (attackMode && voice.adsrLevel > 0x6000) {
      step >>= 2;
      if (step < 1)
        step = 1;
    }

    voice.adsrLevel += step;
    if (voice.adsrLevel >= 0x7FFF) {
      voice.adsrLevel = 0x7FFF;
      voice.adsrPhase = ADSRPhase::Decay;
    }
    break;
  }
  case ADSRPhase::Decay: {
    // Decay is always exponential decrease
    int32_t decayShift = (voice.adsrLo >> 4) & 0x0F;
    int32_t sustainLevel =
        std::min(((voice.adsrLo & 0x0F) + 1) * 0x800, 0x7FFF);

    int32_t step = -1;
    if (decayShift < 11) {
      step <<= (11 - decayShift);
    } else {
      step >>= (decayShift - 11);
    }
    // Exponential: step proportional to current level
    step = (step * voice.adsrLevel) >> 15;
    if (step > -1)
      step = -1;

    voice.adsrLevel += step;
    if (voice.adsrLevel <= sustainLevel) {
      voice.adsrLevel = sustainLevel;
      voice.adsrPhase = ADSRPhase::Sustain;
    }
    break;
  }
  case ADSRPhase::Sustain: {
    bool sustainMode = (voice.adsrHi >> 15) & 1; // 0=linear, 1=exponential
    bool sustainDir = (voice.adsrHi >> 14) & 1;  // 0=increase, 1=decrease
    int32_t sustainShift = (voice.adsrHi >> 8) & 0x1F;
    int32_t sustainStep = (voice.adsrHi >> 6) & 3;

    int32_t step;
    if (sustainDir) {
      // Decrease
      step = -(8 - sustainStep);
    } else {
      // Increase
      step = 7 - sustainStep;
    }

    if (sustainShift < 11) {
      step <<= (11 - sustainShift);
    } else {
      step >>= (sustainShift - 11);
    }

    // Exponential scaling
    if (sustainMode) {
      if (sustainDir) {
        step = (step * voice.adsrLevel) >> 15;
        if (step > -1)
          step = -1;
      } else if (voice.adsrLevel > 0x6000) {
        step >>= 2;
        if (step < 1)
          step = 1;
      }
    }

    voice.adsrLevel += step;
    voice.adsrLevel = std::clamp(voice.adsrLevel, 0, 0x7FFF);
    break;
  }
  case ADSRPhase::Release: {
    bool releaseMode = (voice.adsrHi >> 5) & 1; // 0=linear, 1=exponential
    int32_t releaseShift = voice.adsrHi & 0x1F;

    int32_t step = -1;
    if (releaseShift < 11) {
      step <<= (11 - releaseShift);
    } else {
      step >>= (releaseShift - 11);
    }

    if (releaseMode) {
      step = (step * voice.adsrLevel) >> 15;
      if (step > -1)
        step = -1;
    }

    voice.adsrLevel += step;
    if (voice.adsrLevel <= 0) {
      voice.adsrLevel = 0;
      voice.adsrPhase = ADSRPhase::Off;
    }
    break;
  }
  case ADSRPhase::Off:
    break;
  }

  voice.adsrVolume =
      static_cast<int16_t>(std::clamp(voice.adsrLevel, 0, 0x7FFF));
}

// ─── Debug ──────────────────────────────────────────────────────────────

void PS1SPU::DumpState(std::ostream &os) const {
  os << "=== PS1 SPU ===" << std::endl;
  os << "CTRL: " << std::hex << spuCtrl << " STAT: " << spuStat << std::endl;
  os << "Main Volume: L=" << mainVolumeL << " R=" << mainVolumeR << std::endl;
  os << "Transfer Addr: " << transferAddr << std::endl;

  int activeVoices = 0;
  for (uint32_t i = 0; i < SPU::NUM_VOICES; i++) {
    if (voices[i].adsrPhase != ADSRPhase::Off)
      activeVoices++;
  }
  os << "Active Voices: " << std::dec << activeVoices << "/" << SPU::NUM_VOICES
     << std::endl;
}

std::string PS1SPU::GetDebugSummary() const {
  std::ostringstream os;
  int activeVoices = 0;
  for (const auto &v : voices) {
    if (v.adsrPhase != ADSRPhase::Off)
      activeVoices++;
  }
  os << "SPU voices=" << activeVoices << " ctrl=" << std::hex << spuCtrl;
  return os.str();
}

} // namespace AIO::Emulator::PS1
