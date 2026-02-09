#include "emulator/ps1/PS1SPU.h"
#include "emulator/ps1/PS1Memory.h"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace AIO::Emulator::PS1 {

PS1SPU::PS1SPU(PS1Memory &memory)
    : Loggable("PS1.SPU"), memory(memory), spuRAM(MemSize::SPU_RAM / 2, 0) {
} // SPU RAM in 16-bit words

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
  transferAddr = 0;
  cycleAccumulator = 0;
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
    break;
  case IO::SPU_IRQ_ADDR:
    irqAddr = value;
    break;
  case IO::SPU_DATA_ADDR:
    dataAddr = value;
    transferAddr = static_cast<uint32_t>(value) * 8;
    break;
  case IO::SPU_DATA_FIFO:
    DMAWrite(value);
    break;
  case IO::SPU_CTRL:
    spuCtrl = value;
    // Update status to mirror some control bits
    spuStat = (spuStat & ~0x3F) | (value & 0x3F);
    break;
  case IO::SPU_TRANSFER_CTRL:
    transferCtrl = value;
    break;
  case IO::SPU_CD_VOL_L:
    cdVolumeL = static_cast<int16_t>(value);
    break;
  case IO::SPU_CD_VOL_R:
    cdVolumeR = static_cast<int16_t>(value);
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
    int32_t mixL = 0;
    int32_t mixR = 0;

    for (uint32_t v = 0; v < SPU::NUM_VOICES; v++) {
      if (voices[v].adsrPhase == ADSRPhase::Off)
        continue;

      int16_t sample = DecodeSample(voices[v]);
      int32_t vol = voices[v].adsrLevel;

      // Apply voice volume and ADSR
      int32_t scaledSample = (static_cast<int32_t>(sample) * vol) >> 15;
      mixL += (scaledSample * voices[v].volumeL) >> 15;
      mixR += (scaledSample * voices[v].volumeR) >> 15;
    }

    // Apply main volume
    mixL = (mixL * mainVolumeL) >> 15;
    mixR = (mixR * mainVolumeR) >> 15;

    // Clamp to 16-bit
    buffer[s * 2] = static_cast<int16_t>(std::clamp(mixL, -32768, 32767));
    buffer[s * 2 + 1] = static_cast<int16_t>(std::clamp(mixR, -32768, 32767));
  }
  return maxSamples;
}

// ─── ADPCM Decoding ────────────────────────────────────────────────────

int16_t PS1SPU::DecodeSample(SPUVoice &voice) {
  // Advance sample position using fixed-point counter
  voice.sampleCounter += voice.sampleRate;

  // When counter overflows the base rate, move to next sample
  while (voice.sampleCounter >= SPU::SAMPLE_RATE_BASE) {
    voice.sampleCounter -= SPU::SAMPLE_RATE_BASE;
    // Advance to next ADPCM sample position (simplified)
  }

  // Read and decode ADPCM block from SPU RAM (simplified)
  uint32_t blockAddr = voice.currentAddr / 2;
  if (blockAddr >= spuRAM.size())
    return 0;

  // Return decoded sample (simplified — actual ADPCM decoding below)
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
  // Simplified ADSR envelope processing
  switch (voice.adsrPhase) {
  case ADSRPhase::Attack: {
    int32_t rate = (voice.adsrLo >> 8) & 0x7F;
    voice.adsrLevel += std::max(1, 0x7FFF / std::max(1, (rate + 1)));
    if (voice.adsrLevel >= 0x7FFF) {
      voice.adsrLevel = 0x7FFF;
      voice.adsrPhase = ADSRPhase::Decay;
    }
    break;
  }
  case ADSRPhase::Decay: {
    int32_t sustainLevel = ((voice.adsrLo & 0x0F) + 1) * 0x800;
    voice.adsrLevel -= voice.adsrLevel >> 4;
    if (voice.adsrLevel <= sustainLevel) {
      voice.adsrLevel = sustainLevel;
      voice.adsrPhase = ADSRPhase::Sustain;
    }
    break;
  }
  case ADSRPhase::Sustain: {
    bool decrease = (voice.adsrHi >> 14) & 1;
    if (decrease) {
      voice.adsrLevel -= voice.adsrLevel >> 5;
      if (voice.adsrLevel < 0)
        voice.adsrLevel = 0;
    }
    break;
  }
  case ADSRPhase::Release: {
    voice.adsrLevel -= voice.adsrLevel >> 4;
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
