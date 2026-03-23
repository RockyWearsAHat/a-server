#pragma once

#include "PS1Constants.h"
#include "emulator/common/Loggable.h"
#include <array>
#include <cstdint>
#include <ostream>
#include <string>

namespace AIO::Emulator::PS1 {

class PS1Memory;
class PS1GPU;
class PS1SPU;
class CDROM;
class InterruptController;
class PS1MDEC;

struct DMAChannel {
  uint32_t baseAddr = 0;       // MADR
  uint32_t blockControl = 0;   // BCR
  uint32_t channelControl = 0; // CHCR

  // Derived from CHCR
  bool directionFromRAM() const { return channelControl & 1; }
  bool stepBackward() const { return (channelControl >> 1) & 1; }
  bool choppingEnabled() const { return (channelControl >> 8) & 1; }
  uint32_t syncMode() const { return (channelControl >> 9) & 3; }
  bool isActive() const { return (channelControl >> 24) & 1; }
  bool isTrigger() const { return (channelControl >> 28) & 1; }

  uint32_t blockSize() const { return blockControl & 0xFFFF; }
  uint32_t blockCount() const { return (blockControl >> 16) & 0xFFFF; }
};

class PS1DMA : public Common::Loggable {
public:
  PS1DMA(PS1Memory &memory, PS1GPU &gpu, PS1SPU &spu,
         InterruptController &interrupts);
  ~PS1DMA() = default;

  void Reset();

  // ─── Register Interface ─────────────────────────────────────────────
  uint32_t Read32(uint32_t addr) const;
  void Write32(uint32_t addr, uint32_t value);

  // ─── CDROM reference (set after construction) ───────────────────────
  void SetCDROM(CDROM *cdrom) { this->cdrom = cdrom; }

  // ─── MDEC reference (set after construction) ────────────────────────
  void SetMDEC(PS1MDEC *mdec) { this->mdec = mdec; }

  // ─── Debug ──────────────────────────────────────────────────────────
  void DumpState(std::ostream &os) const;
  std::string GetDebugSummary() const;

  uint32_t GetTransferCount(uint32_t channel) const {
    return transferCounts[channel];
  }

  uint32_t GetDICR() const { return dicr; }
  void AcknowledgeDICRFlags(uint32_t channelMask) {
    dicr &= ~(channelMask << 24);
    UpdateMasterIRQ();
  }

private:
  PS1Memory &memory;
  PS1GPU &gpu;
  PS1SPU &spu;
  InterruptController &interrupts;
  CDROM *cdrom = nullptr;
  PS1MDEC *mdec = nullptr;

  std::array<DMAChannel, DMA::NUM_CHANNELS> channels{};
  uint32_t dpcr = 0x07654321; // Default DPCR value
  uint32_t dicr = 0;

  // Transfer counters for debugging
  std::array<uint32_t, DMA::NUM_CHANNELS> transferCounts{};

  // ─── Cycle Stall Tracking ────────────────────────────────────────────
  // Cycles consumed by the last DMA transfer; consumed by PS1::Step.
  uint32_t pendingCycles = 0;

public:
  // Returns and clears accumulated DMA stall cycles for this step.
  uint32_t GetAndClearPendingCycles() {
    uint32_t c = pendingCycles;
    pendingCycles = 0;
    return c;
  }

private:
  // ─── Transfer Execution ─────────────────────────────────────────────
  void CheckAndStartTransfer(uint32_t channelIndex);
  void DoTransfer(uint32_t channelIndex);
  uint32_t DoBlockTransfer(uint32_t channelIndex);      // Returns word count
  uint32_t DoLinkedListTransfer(uint32_t channelIndex); // Returns word count

  bool IsChannelEnabled(uint32_t channelIndex) const;
  void SetIRQFlag(uint32_t channelIndex);
  void UpdateMasterIRQ();
};

} // namespace AIO::Emulator::PS1
