#include "emulator/ps1/PS1DMA.h"
#include "emulator/ps1/CDROM.h"
#include "emulator/ps1/InterruptController.h"
#include "emulator/ps1/PS1GPU.h"
#include "emulator/ps1/PS1MDEC.h"
#include "emulator/ps1/PS1Memory.h"
#include "emulator/ps1/PS1SPU.h"
#include <iomanip>
#include <sstream>

namespace AIO::Emulator::PS1 {

PS1DMA::PS1DMA(PS1Memory &memory, PS1GPU &gpu, PS1SPU &spu,
               InterruptController &interrupts)
    : Loggable("PS1.DMA"), memory(memory), gpu(gpu), spu(spu),
      interrupts(interrupts) {}

void PS1DMA::Reset() {
  for (auto &ch : channels) {
    ch.baseAddr = 0;
    ch.blockControl = 0;
    ch.channelControl = 0;
  }
  dpcr = 0x07654321;
  dicr = 0;
  transferCounts.fill(0);
}

uint32_t PS1DMA::Read32(uint32_t addr) const {
  if (addr == IO::DMA_DPCR)
    return dpcr;
  if (addr == IO::DMA_DICR)
    return dicr;

  uint32_t channelIndex = (addr - IO::DMA_BASE) / IO::DMA_CHANNEL_SIZE;
  uint32_t reg = (addr - IO::DMA_BASE) % IO::DMA_CHANNEL_SIZE;

  if (channelIndex >= DMA::NUM_CHANNELS)
    return 0;

  const auto &ch = channels[channelIndex];
  switch (reg) {
  case 0x00:
    return ch.baseAddr;
  case 0x04:
    return ch.blockControl;
  case 0x08:
    return ch.channelControl;
  default:
    return 0;
  }
}

void PS1DMA::Write32(uint32_t addr, uint32_t value) {
  if (addr == IO::DMA_DPCR) {
    dpcr = value;
    LogInfo("DMA DPCR=%08X ch3_enable=%d", value, (value >> 15) & 1);
    return;
  }
  if (addr == IO::DMA_DICR) {
    // Bits 0-5: unknown/unused
    // Bit 6-14: enable flags for channel 0-6
    // Bit 15: force IRQ
    // Bits 16-22: master enable flags
    // Bit 23: master enable
    // Bits 24-30: flags (write 1 to clear)
    // Bit 31: master flag (read-only)

    uint32_t ackBits = value & 0x7F000000; // Bits 24-30 are acknowledge
    dicr = (dicr & ~0x00FF803F) | (value & 0x00FF803F); // Write writable bits
    dicr &= ~ackBits; // Clear acknowledged flags
    UpdateMasterIRQ();

    LogInfo("DICR write: val=%08X → dicr=%08X", value, dicr);
    return;
  }

  uint32_t channelIndex = (addr - IO::DMA_BASE) / IO::DMA_CHANNEL_SIZE;
  uint32_t reg = (addr - IO::DMA_BASE) % IO::DMA_CHANNEL_SIZE;

  if (channelIndex >= DMA::NUM_CHANNELS)
    return;

  auto &ch = channels[channelIndex];
  switch (reg) {
  case 0x00:
    ch.baseAddr = value & 0x00FFFFFF; // 24-bit address
    break;
  case 0x04:
    ch.blockControl = value;
    break;
  case 0x08:
    ch.channelControl = value;
    if (channelIndex == DMA::Channel::CDROM || Trace::DMA_TRACE) {
      LogInfo("DMA ch%u CHCR=%08X (dir=%s sync=%u active=%d trigger=%d "
              "addr=%08X block=%08X)",
              channelIndex, value, ch.directionFromRAM() ? "fromRAM" : "toRAM",
              ch.syncMode(), ch.isActive(), ch.isTrigger(), ch.baseAddr,
              ch.blockControl);
    }
    CheckAndStartTransfer(channelIndex);
    break;
  }
}

bool PS1DMA::IsChannelEnabled(uint32_t channelIndex) const {
  return (dpcr >> (channelIndex * 4 + 3)) & 1;
}

void PS1DMA::CheckAndStartTransfer(uint32_t channelIndex) {
  auto &ch = channels[channelIndex];

  if (!IsChannelEnabled(channelIndex))
    return;
  if (!ch.isActive())
    return;

  // Manual sync mode requires trigger bit
  if (ch.syncMode() == DMA::SyncMode::MANUAL && !ch.isTrigger())
    return;
  DoTransfer(channelIndex);
}

void PS1DMA::DoTransfer(uint32_t channelIndex) {
  auto &ch = channels[channelIndex];

  switch (ch.syncMode()) {
  case DMA::SyncMode::MANUAL:
  case DMA::SyncMode::REQUEST:
    DoBlockTransfer(channelIndex);
    break;
  case DMA::SyncMode::LINKED_LIST:
    DoLinkedListTransfer(channelIndex);
    break;
  default:
    LogWarn("DMA ch%u: unknown sync mode %u", channelIndex, ch.syncMode());
    break;
  }

  // Transfer complete — clear active bit and trigger
  ch.channelControl &= ~((1 << 24) | (1 << 28));

  SetIRQFlag(channelIndex);
  transferCounts[channelIndex]++;
}

void PS1DMA::DoBlockTransfer(uint32_t channelIndex) {
  auto &ch = channels[channelIndex];

  uint32_t wordCount;
  if (ch.syncMode() == DMA::SyncMode::MANUAL) {
    wordCount = ch.blockSize();
  } else {
    wordCount = ch.blockSize() * ch.blockCount();
  }

  // OTC (Ordering Table Clear) — special channel
  if (channelIndex == DMA::Channel::OTC) {
    // Build ordering table: each entry points to previous, last entry is
    // terminator
    uint32_t addr = ch.baseAddr;
    for (uint32_t i = 0; i < wordCount; i++) {
      uint32_t value;
      if (i == wordCount - 1) {
        value = 0x00FFFFFF; // Terminator
      } else {
        value = (addr - 4) & 0x00FFFFFF;
      }
      memory.WriteRAM32(addr, value);
      addr -= 4;
    }

    if constexpr (Trace::DMA_TRACE) {
      LogDebug("DMA OTC: built ordering table, %u entries at %08X", wordCount,
               ch.baseAddr);
    }
    return;
  }

  int32_t step = ch.stepBackward() ? -4 : 4;
  uint32_t addr = ch.baseAddr;

  for (uint32_t i = 0; i < wordCount; i++) {
    uint32_t currentAddr = addr & 0x001FFFFC; // Mask to RAM and align

    if (ch.directionFromRAM()) {
      // From RAM to device
      uint32_t word = memory.ReadRAM32(currentAddr);
      switch (channelIndex) {
      case DMA::Channel::GPU:
        gpu.DMAWrite(word);
        break;
      case DMA::Channel::SPU:
        spu.DMAWrite(static_cast<uint16_t>(word));
        spu.DMAWrite(static_cast<uint16_t>(word >> 16));
        break;
      case DMA::Channel::MDEC_IN:
        if (mdec)
          mdec->WriteDMAWord(word);
        break;
      default:
        break;
      }
    } else {
      // From device to RAM
      uint32_t word = 0;
      switch (channelIndex) {
      case DMA::Channel::GPU:
        word = gpu.DMARead();
        break;
      case DMA::Channel::CDROM:
        if (cdrom) {
          word = cdrom->DMARead();
          word |= static_cast<uint32_t>(cdrom->DMARead()) << 8;
          word |= static_cast<uint32_t>(cdrom->DMARead()) << 16;
          word |= static_cast<uint32_t>(cdrom->DMARead()) << 24;
        }
        if (i == 0) {
          LogInfo("DMA CDROM->RAM: %u words, addr=%08X, first word=%08X",
                  wordCount, channels[channelIndex].baseAddr, word);
        }
        break;
      case DMA::Channel::SPU:
        word = spu.DMARead();
        word |= static_cast<uint32_t>(spu.DMARead()) << 16;
        break;
      case DMA::Channel::MDEC_OUT:
        word = mdec ? mdec->ReadDMAWord() : 0xFFFFFFFF;
        break;
      default:
        break;
      }
      memory.WriteRAM32(currentAddr, word);
    }

    addr = static_cast<uint32_t>(static_cast<int32_t>(addr) + step);
  }

  if constexpr (Trace::DMA_TRACE) {
    LogDebug("DMA ch%u block transfer: %u words, addr=%08X, dir=%s",
             channelIndex, wordCount, ch.baseAddr,
             ch.directionFromRAM() ? "fromRAM" : "toRAM");
  }
}

void PS1DMA::DoLinkedListTransfer(uint32_t channelIndex) {
  // Linked list mode is only used by GPU (channel 2)
  if (channelIndex != DMA::Channel::GPU) {
    LogWarn("DMA ch%u: linked list mode not supported for this channel",
            channelIndex);
    return;
  }

  uint32_t addr = channels[channelIndex].baseAddr & 0x001FFFFC;
  uint32_t safetyCounter = 0;
  constexpr uint32_t MAX_LINKED_LIST_ENTRIES = 0x100000;

  while (safetyCounter < MAX_LINKED_LIST_ENTRIES) {
    uint32_t header = memory.ReadRAM32(addr);
    uint32_t wordCount = header >> 24;

    for (uint32_t i = 0; i < wordCount; i++) {
      addr += 4;
      uint32_t word = memory.ReadRAM32(addr & 0x001FFFFC);
      gpu.DMAWrite(word);
    }

    if (header & 0x00800000)
      break;

    addr = header & 0x001FFFFC;
    safetyCounter++;
  }

  if constexpr (Trace::DMA_TRACE) {
    LogDebug("DMA GPU linked list transfer: %u entries", safetyCounter);
  }
}

void PS1DMA::SetIRQFlag(uint32_t channelIndex) {
  // Set completion flag for this channel (bits 24-30 in DICR)
  bool channelIRQEnabled = (dicr >> (16 + channelIndex)) & 1;
  LogInfo("DMA ch%u SetIRQFlag: dicr=%08X chIRQen=%d", channelIndex, dicr,
          channelIRQEnabled);
  if (channelIRQEnabled) {
    dicr |= (1 << (24 + channelIndex));
    UpdateMasterIRQ();
  }
}

void PS1DMA::UpdateMasterIRQ() {
  // Bit 31 = master flag (read-only)
  // Force IRQ (bit 15) OR (master enable AND any channel flag)
  bool forceIRQ = (dicr >> 15) & 1;
  bool masterEnable = (dicr >> 23) & 1;

  uint32_t enableFlags = (dicr >> 16) & 0x7F;
  uint32_t flagBits = (dicr >> 24) & 0x7F;

  bool masterFlag = forceIRQ || (masterEnable && (enableFlags & flagBits) != 0);

  if (masterFlag) {
    dicr |= (1u << 31);
    interrupts.RequestIRQ(IRQ::DMA);
  } else {
    dicr &= ~(1u << 31);
  }
}

void PS1DMA::DumpState(std::ostream &os) const {
  os << "=== PS1 DMA ===" << std::endl;
  os << "DPCR: " << std::hex << dpcr << " DICR: " << dicr << std::endl;
  for (uint32_t i = 0; i < DMA::NUM_CHANNELS; i++) {
    const auto &ch = channels[i];
    os << "Ch" << i << " MADR=" << std::hex << ch.baseAddr
       << " BCR=" << ch.blockControl << " CHCR=" << ch.channelControl
       << " xfers=" << std::dec << transferCounts[i] << std::endl;
  }
}

std::string PS1DMA::GetDebugSummary() const {
  std::ostringstream os;
  os << "DMA dpcr=" << std::hex << dpcr << " dicr=" << dicr;
  return os.str();
}

} // namespace AIO::Emulator::PS1
