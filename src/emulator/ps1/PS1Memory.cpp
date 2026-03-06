#include "emulator/ps1/PS1Memory.h"
#include "emulator/ps1/CDROM.h"
#include "emulator/ps1/InterruptController.h"
#include "emulator/ps1/PS1.h"
#include "emulator/ps1/PS1Controller.h"
#include "emulator/ps1/PS1DMA.h"
#include "emulator/ps1/PS1GPU.h"
#include "emulator/ps1/PS1SPU.h"
#include "emulator/ps1/PS1Timer.h"
#include "emulator/ps1/R3000A.h"
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace AIO::Emulator::PS1 {

PS1Memory::PS1Memory()
    : Loggable("PS1.MEM"), ram(MemSize::RAM, 0), bios(MemSize::BIOS, 0),
      scratchpad(MemSize::SCRATCHPAD, 0), expansion1(MemSize::EXPANSION1, 0xFF),
      expansion2(MemSize::EXPANSION2, 0) {}

void PS1Memory::Reset() {
  std::fill(ram.begin(), ram.end(), 0);
  std::fill(scratchpad.begin(), scratchpad.end(), 0);
  cacheIsolated = false;
  memCtrl1.fill(0);
  ramSizeReg = 0;
  cacheCtrlReg = 0;
}

bool PS1Memory::LoadBIOS(const std::string &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    LogError("Failed to open BIOS file: %s", path.c_str());
    return false;
  }

  auto size = file.tellg();
  if (size != MemSize::BIOS) {
    LogError("Invalid BIOS size: %zu (expected %u)", static_cast<size_t>(size),
             MemSize::BIOS);
    return false;
  }

  file.seekg(0);
  file.read(reinterpret_cast<char *>(bios.data()), MemSize::BIOS);
  biosLoaded = true;
  LogInfo("BIOS loaded: %s (%zu bytes)", path.c_str(),
          static_cast<size_t>(size));
  return true;
}

// ─── Address Translation ────────────────────────────────────────────────

uint32_t PS1Memory::TranslateAddress(uint32_t virtualAddr) const {
  // KSEG2 (0xC0000000–0xFFFFFFFF) is NOT masked — it maps to special
  // hardware registers like the cache control register at 0xFFFE0130
  if (virtualAddr >= MemMap::KSEG2_START)
    return virtualAddr;
  // KUSEG/KSEG0/KSEG1 → mask to physical address
  return virtualAddr & MemMap::KSEG_MASK;
}

// ─── Bus Read ───────────────────────────────────────────────────────────

uint8_t PS1Memory::Read8(uint32_t addr) {
  uint32_t phys = TranslateAddress(addr);

  if (phys < MemMap::RAM_REGION_SIZE) {
    return ram[phys & (MemSize::RAM - 1)];
  }
  if (phys >= MemMap::BIOS_START && phys <= MemMap::BIOS_END) {
    return bios[phys - MemMap::BIOS_START];
  }
  if (phys >= MemMap::SCRATCHPAD_START && phys <= MemMap::SCRATCHPAD_END) {
    return scratchpad[phys - MemMap::SCRATCHPAD_START];
  }
  if (phys >= MemMap::IO_START && phys <= MemMap::IO_END) {
    return ReadIO8(phys);
  }
  if (phys >= MemMap::EXPANSION1_START && phys <= MemMap::EXPANSION1_END) {
    return 0xFF; // Expansion 1 not connected
  }
  if (phys >= MemMap::EXPANSION2_START && phys <= MemMap::EXPANSION2_END) {
    return 0xFF;
  }

  if constexpr (Trace::MEMORY) {
    LogWarn("Unhandled Read8 addr=%08X (phys=%08X)", addr, phys);
  }
  return 0xFF;
}

uint16_t PS1Memory::Read16(uint32_t addr) {
  uint32_t phys = TranslateAddress(addr);

  if (phys < MemMap::RAM_REGION_SIZE) {
    uint32_t offset = phys & (MemSize::RAM - 1);
    uint16_t value;
    std::memcpy(&value, &ram[offset], sizeof(uint16_t));
    return value;
  }
  if (phys >= MemMap::BIOS_START && phys <= MemMap::BIOS_END) {
    uint16_t value;
    std::memcpy(&value, &bios[phys - MemMap::BIOS_START], sizeof(uint16_t));
    return value;
  }
  if (phys >= MemMap::SCRATCHPAD_START && phys <= MemMap::SCRATCHPAD_END) {
    uint16_t value;
    std::memcpy(&value, &scratchpad[phys - MemMap::SCRATCHPAD_START],
                sizeof(uint16_t));
    return value;
  }
  if (phys >= MemMap::IO_START && phys <= MemMap::IO_END) {
    return ReadIO16(phys);
  }

  if constexpr (Trace::MEMORY) {
    LogWarn("Unhandled Read16 addr=%08X (phys=%08X)", addr, phys);
  }
  return 0xFFFF;
}

uint32_t PS1Memory::Read32(uint32_t addr) {
  uint32_t phys = TranslateAddress(addr);

  if (phys < MemMap::RAM_REGION_SIZE) {
    uint32_t offset = phys & (MemSize::RAM - 1);
    uint32_t value;
    std::memcpy(&value, &ram[offset], sizeof(uint32_t));
    return value;
  }
  if (phys >= MemMap::BIOS_START && phys <= MemMap::BIOS_END) {
    uint32_t value;
    std::memcpy(&value, &bios[phys - MemMap::BIOS_START], sizeof(uint32_t));
    return value;
  }
  if (phys >= MemMap::SCRATCHPAD_START && phys <= MemMap::SCRATCHPAD_END) {
    uint32_t value;
    std::memcpy(&value, &scratchpad[phys - MemMap::SCRATCHPAD_START],
                sizeof(uint32_t));
    return value;
  }
  if (phys >= MemMap::IO_START && phys <= MemMap::IO_END) {
    return ReadIO32(phys);
  }
  if (phys >= MemMap::CACHE_CTRL_START && phys <= MemMap::CACHE_CTRL_END) {
    return cacheCtrlReg;
  }

  if constexpr (Trace::MEMORY) {
    LogWarn("Unhandled Read32 addr=%08X (phys=%08X)", addr, phys);
  }
  return 0;
}

// ─── Bus Write ──────────────────────────────────────────────────────────

void PS1Memory::Write8(uint32_t addr, uint8_t value) {
  uint32_t phys = TranslateAddress(addr);

  static uint64_t tileW8 = 0;
  static bool tileW8Populated = false;
  if (phys >= 0x001041BC && phys < 0x001042BC) {
    tileW8++;
    if (value != 0 && !tileW8Populated) {
      tileW8Populated = true;
      LogInfo(
          "WATCH Write8 FIRST POPULATE phys=%08X val=%02X (addr=%08X) iso=%d",
          phys, value, addr, cacheIsolated ? 1 : 0);
    }
    if (tileW8 <= 10)
      LogInfo("WATCH Write8 tile phys=%08X val=%02X (addr=%08X) #%llu iso=%d",
              phys, value, addr, tileW8, cacheIsolated ? 1 : 0);
    if (tileW8 == 256) {
      uint32_t baseOff = 0x001041BC & (MemSize::RAM - 1);
      LogInfo("WATCH Write8 READBACK after 256 writes: %02X %02X %02X %02X "
              "%02X %02X %02X %02X",
              ram[baseOff], ram[baseOff + 1], ram[baseOff + 2],
              ram[baseOff + 3], ram[baseOff + 4], ram[baseOff + 5],
              ram[baseOff + 6], ram[baseOff + 7]);
    }
    if (tileW8 == 512 || tileW8 == 1024 || tileW8 == 2048 || tileW8 == 4096) {
      uint32_t baseOff = 0x001041BC & (MemSize::RAM - 1);
      LogInfo("WATCH Write8 READBACK at #%llu: %02X %02X %02X %02X %02X %02X "
              "%02X %02X",
              tileW8, ram[baseOff], ram[baseOff + 1], ram[baseOff + 2],
              ram[baseOff + 3], ram[baseOff + 4], ram[baseOff + 5],
              ram[baseOff + 6], ram[baseOff + 7]);
    }
  }

  if (phys < MemMap::RAM_REGION_SIZE) {
    if (!cacheIsolated) {
      if (phys >= MemSize::RAM && ((ramSizeReg >> 9) & 7) == 4)
        return;
      uint32_t offset = phys & (MemSize::RAM - 1);
      ram[offset] = value;
    }
    return;
  }
  if (phys >= MemMap::SCRATCHPAD_START && phys <= MemMap::SCRATCHPAD_END) {
    scratchpad[phys - MemMap::SCRATCHPAD_START] = value;
    return;
  }
  if (phys >= MemMap::IO_START && phys <= MemMap::IO_END) {
    WriteIO8(phys, value);
    return;
  }
  if (phys >= MemMap::EXPANSION2_START && phys <= MemMap::EXPANSION2_END) {
    return; // POST output, ignore
  }

  if constexpr (Trace::MEMORY) {
    LogWarn("Unhandled Write8 addr=%08X val=%02X", addr, value);
  }
}

void PS1Memory::Write16(uint32_t addr, uint16_t value) {
  uint32_t phys = TranslateAddress(addr);

  static uint64_t tileW16 = 0;
  if (phys >= 0x001041BC && phys < 0x001042BC && value != 0) {
    tileW16++;
    if (tileW16 <= 5)
      LogInfo("WATCH Write16 tile phys=%08X val=%04X (addr=%08X)", phys, value,
              addr);
  }

  if (phys < MemMap::RAM_REGION_SIZE) {
    if (!cacheIsolated) {
      if (phys >= MemSize::RAM && ((ramSizeReg >> 9) & 7) == 4)
        return;
      uint32_t offset = phys & (MemSize::RAM - 1);
      std::memcpy(&ram[offset], &value, sizeof(uint16_t));
    }
    return;
  }
  if (phys >= MemMap::SCRATCHPAD_START && phys <= MemMap::SCRATCHPAD_END) {
    std::memcpy(&scratchpad[phys - MemMap::SCRATCHPAD_START], &value,
                sizeof(uint16_t));
    return;
  }
  if (phys >= MemMap::IO_START && phys <= MemMap::IO_END) {
    WriteIO16(phys, value);
    return;
  }

  if constexpr (Trace::MEMORY) {
    LogWarn("Unhandled Write16 addr=%08X val=%04X", addr, value);
  }
}

void PS1Memory::Write32(uint32_t addr, uint32_t value) {
  uint32_t phys = TranslateAddress(addr);

  // Watchpoint: first tile pixel data address (any write, including zero)
  uint32_t watchAddr = 0x001041BC;
  if (phys >= watchAddr && phys < watchAddr + 256) {
    static uint64_t watchWrites = 0;
    static bool wasPopulated = false;
    watchWrites++;
    // Track when first non-zero write arrives
    if (value != 0 && !wasPopulated) {
      wasPopulated = true;
      LogInfo("WATCH Write32 FIRST POPULATE phys=%08X val=%08X (addr=%08X)",
              phys, value, addr);
    }
    // Log when zero is written AFTER data was populated (the clear!)
    if (value == 0 && wasPopulated && watchWrites <= 200) {
      LogInfo("WATCH Write32 CLEARING phys=%08X val=0 (addr=%08X) write#%llu",
              phys, addr, watchWrites);
    }
    if (watchWrites <= 10) {
      LogInfo("WATCH Write32 phys=%08X val=%08X (addr=%08X) #%llu", phys, value,
              addr, watchWrites);
    }
  }
  // Broader check: any write (including zero) to tile entry region
  static uint64_t tileRegionWrites = 0;
  if (phys >= 0x00104000 && phys < 0x00120000) {
    tileRegionWrites++;
    if (tileRegionWrites <= 10) {
      LogInfo("TILE_REGION Write32 #%llu: phys=%08X val=%08X (addr=%08X)",
              tileRegionWrites, phys, value, addr);
    }
    if (tileRegionWrites == 100000) {
      LogInfo("TILE_REGION total writes so far: %llu", tileRegionWrites);
    }
  }

  if (phys < MemMap::RAM_REGION_SIZE) {
    if (!cacheIsolated) {
      if (phys >= MemSize::RAM && ((ramSizeReg >> 9) & 7) == 4)
        return;
      uint32_t offset = phys & (MemSize::RAM - 1);
      std::memcpy(&ram[offset], &value, sizeof(uint32_t));
    }
    return;
  }
  if (phys >= MemMap::SCRATCHPAD_START && phys <= MemMap::SCRATCHPAD_END) {
    std::memcpy(&scratchpad[phys - MemMap::SCRATCHPAD_START], &value,
                sizeof(uint32_t));
    return;
  }
  if (phys >= MemMap::IO_START && phys <= MemMap::IO_END) {
    WriteIO32(phys, value);
    return;
  }
  if (phys >= MemMap::CACHE_CTRL_START && phys <= MemMap::CACHE_CTRL_END) {
    cacheCtrlReg = value;
    return;
  }

  if constexpr (Trace::MEMORY) {
    LogWarn("Unhandled Write32 addr=%08X val=%08X", addr, value);
  }
}

// ─── I/O Register Dispatch ──────────────────────────────────────────────

uint32_t PS1Memory::ReadIO32(uint32_t addr) {
  // Memory control registers
  if (addr >= IO::MEM_CTRL1_START && addr < IO::MEM_CTRL1_START + 0x24) {
    uint32_t index = (addr - IO::MEM_CTRL1_START) / 4;
    return memCtrl1[index];
  }
  if (addr == IO::RAM_SIZE)
    return ramSizeReg;

  // Interrupt controller
  if (addr == IO::I_STAT && interrupts)
    return interrupts->ReadStat();
  if (addr == IO::I_MASK && interrupts)
    return interrupts->ReadMask();

  // DMA
  if (addr >= IO::DMA_BASE && addr <= IO::DMA_DICR && dma)
    return dma->Read32(addr);

  // Timer
  if (addr >= IO::TIMER_BASE &&
      addr < IO::TIMER_BASE + Timer::NUM_TIMERS * IO::TIMER_CHANNEL_SIZE &&
      timers) {
    return timers->Read32(addr);
  }

  // GPU
  if (addr == IO::GPU_GPUREAD && gpu)
    return gpu->ReadGPUREAD();
  if (addr == IO::GPU_GPUSTAT && gpu)
    return gpu->ReadGPUSTAT();

  // Controller (SIO0) - 32-bit reads
  if (addr == IO::SIO0_DATA && controller) {
    return static_cast<uint32_t>(controller->ReadData());
  }
  if (addr == IO::SIO0_STAT && controller) {
    return controller->ReadStat();
  }
  if (addr == IO::SIO0_MODE && controller) {
    return static_cast<uint32_t>(controller->ReadMode());
  }

  if constexpr (Trace::MEMORY) {
    LogWarn("Unhandled IO Read32 addr=%08X", addr);
  }
  return 0;
}

uint16_t PS1Memory::ReadIO16(uint32_t addr) {
  // SPU
  if (addr >= IO::SPU_START && addr <= IO::SPU_END && spu) {
    return spu->ReadRegister(addr);
  }

  // Timer
  if (addr >= IO::TIMER_BASE &&
      addr < IO::TIMER_BASE + Timer::NUM_TIMERS * IO::TIMER_CHANNEL_SIZE &&
      timers) {
    return static_cast<uint16_t>(timers->Read32(addr));
  }

  // Controller (SIO0)
  if (addr == IO::SIO0_MODE && controller)
    return controller->ReadMode();
  if (addr == IO::SIO0_CTRL && controller)
    return controller->ReadCtrl();
  if (addr == IO::SIO0_BAUD && controller)
    return controller->ReadBaud();

  // Interrupt controller
  if (addr == IO::I_STAT && interrupts)
    return static_cast<uint16_t>(interrupts->ReadStat());
  if (addr == IO::I_MASK && interrupts)
    return static_cast<uint16_t>(interrupts->ReadMask());

  if constexpr (Trace::MEMORY) {
    LogWarn("Unhandled IO Read16 addr=%08X", addr);
  }
  return 0;
}

uint8_t PS1Memory::ReadIO8(uint32_t addr) {
  // CD-ROM
  if (addr >= IO::CDROM_BASE && addr <= IO::CDROM_REG3 && cdrom) {
    return cdrom->Read8(addr);
  }

  // Controller data (SIO0)
  if (addr == IO::SIO0_DATA && controller)
    return controller->ReadData();

  if constexpr (Trace::MEMORY) {
    LogWarn("Unhandled IO Read8 addr=%08X", addr);
  }
  return 0xFF;
}

void PS1Memory::WriteIO32(uint32_t addr, uint32_t value) {
  // Memory control registers
  if (addr >= IO::MEM_CTRL1_START && addr < IO::MEM_CTRL1_START + 0x24) {
    uint32_t index = (addr - IO::MEM_CTRL1_START) / 4;
    memCtrl1[index] = value;
    return;
  }
  if (addr == IO::RAM_SIZE) {
    ramSizeReg = value;
    return;
  }

  // Interrupt controller
  if (addr == IO::I_STAT && interrupts) {
    interrupts->WriteStat(value);
    return;
  }
  if (addr == IO::I_MASK && interrupts) {
    interrupts->WriteMask(value);
    return;
  }

  // DMA
  if (addr >= IO::DMA_BASE && addr <= IO::DMA_DICR && dma) {
    dma->Write32(addr, value);
    return;
  }

  // Timer
  if (addr >= IO::TIMER_BASE &&
      addr < IO::TIMER_BASE + Timer::NUM_TIMERS * IO::TIMER_CHANNEL_SIZE &&
      timers) {
    timers->Write32(addr, value);
    return;
  }

  // GPU
  if (addr == IO::GPU_GP0 && gpu) {
    // Track CopyRect writes: log all 3 words (cmd, dest, size)
    static int copyRectState = 0;
    static int copyRectCount = 0;
    if ((value >> 24) == 0xA0 && cpu) {
      copyRectCount++;
      if (copyRectCount <= 5) {
        LogInfo("GPU_GP0 CopyRect#%d cmd from PC=0x%08X val=0x%08X",
                copyRectCount, cpu->GetPC(), value);
      }
      copyRectState = 2; // Expect 2 more words
    } else if (copyRectState > 0 && cpu && copyRectCount <= 5) {
      LogInfo(
          "GPU_GP0 CopyRect#%d param from PC=0x%08X val=0x%08X (remaining=%d)",
          copyRectCount, cpu->GetPC(), value, copyRectState);
      copyRectState--;
    }
    gpu->WriteGP0(value);
    return;
  }
  if (addr == IO::GPU_GP1 && gpu) {
    gpu->WriteGP1(value);
    return;
  }

  // Controller (SIO0) - 32-bit writes
  if (addr == IO::SIO0_DATA && controller) {
    controller->WriteData(static_cast<uint8_t>(value));
    return;
  }

  if constexpr (Trace::MEMORY) {
    LogWarn("Unhandled IO Write32 addr=%08X val=%08X", addr, value);
  }
}

void PS1Memory::WriteIO16(uint32_t addr, uint16_t value) {
  // SPU
  if (addr >= IO::SPU_START && addr <= IO::SPU_END && spu) {
    spu->WriteRegister(addr, value);
    return;
  }

  // Timer
  if (addr >= IO::TIMER_BASE &&
      addr < IO::TIMER_BASE + Timer::NUM_TIMERS * IO::TIMER_CHANNEL_SIZE &&
      timers) {
    timers->Write32(addr, value);
    return;
  }

  // Interrupt controller
  if (addr == IO::I_STAT && interrupts) {
    interrupts->WriteStat(value);
    return;
  }
  if (addr == IO::I_MASK && interrupts) {
    interrupts->WriteMask(value);
    return;
  }

  // Controller (SIO0)
  if (addr == IO::SIO0_MODE && controller) {
    controller->WriteMode(value);
    return;
  }
  if (addr == IO::SIO0_CTRL && controller) {
    controller->WriteCtrl(value);
    return;
  }
  if (addr == IO::SIO0_BAUD && controller) {
    controller->WriteBaud(value);
    return;
  }

  if constexpr (Trace::MEMORY) {
    LogWarn("Unhandled IO Write16 addr=%08X val=%04X", addr, value);
  }
}

void PS1Memory::WriteIO8(uint32_t addr, uint8_t value) {
  // CD-ROM
  if (addr >= IO::CDROM_BASE && addr <= IO::CDROM_REG3 && cdrom) {
    cdrom->Write8(addr, value);
    return;
  }

  // Expansion 2 POST output
  if (addr == 0x1F802041)
    return;

  // Controller data (SIO0)
  if (addr == IO::SIO0_DATA && controller) {
    controller->WriteData(value);
    return;
  }

  if constexpr (Trace::MEMORY) {
    LogWarn("Unhandled IO Write8 addr=%08X val=%02X", addr, value);
  }
}

// ─── Direct Memory Access ───────────────────────────────────────────────

uint32_t PS1Memory::ReadRAM32(uint32_t offset) const {
  uint32_t val;
  std::memcpy(&val, &ram[offset & (MemSize::RAM - 1)], sizeof(uint32_t));
  return val;
}

void PS1Memory::WriteRAM32(uint32_t offset, uint32_t value) {
  uint32_t actualOffset = offset & (MemSize::RAM - 1);
  // Watchpoint for tile pixel data via DMA WriteRAM32 path
  static uint64_t ramWatchCount = 0;
  if (actualOffset >= 0x00104000 && actualOffset < 0x00120000) {
    ramWatchCount++;
    if (ramWatchCount <= 5) {
      LogInfo("TILE_REGION WriteRAM32 #%llu: offset=%08X val=%08X",
              ramWatchCount, actualOffset, value);
    }
  }
  std::memcpy(&ram[actualOffset], &value, sizeof(uint32_t));
}

void PS1Memory::WriteBIOS32(uint32_t offset, uint32_t value) {
  std::memcpy(&bios[offset & (MemSize::BIOS - 1)], &value, sizeof(uint32_t));
}

uint32_t PS1Memory::ReadScratchpad32(uint32_t offset) const {
  uint32_t val;
  std::memcpy(&val, &scratchpad[offset & (MemSize::SCRATCHPAD - 1)],
              sizeof(uint32_t));
  return val;
}

void PS1Memory::WriteScratchpad32(uint32_t offset, uint32_t value) {
  std::memcpy(&scratchpad[offset & (MemSize::SCRATCHPAD - 1)], &value,
              sizeof(uint32_t));
}

// ─── Debug ──────────────────────────────────────────────────────────────

void PS1Memory::DumpState(std::ostream &os) const {
  os << "=== PS1 Memory State ===" << std::endl;
  os << "Cache Isolated: " << (cacheIsolated ? "YES" : "NO") << std::endl;
  os << "BIOS Loaded: " << (biosLoaded ? "YES" : "NO") << std::endl;
  os << "RAM Size: " << MemSize::RAM << " bytes" << std::endl;
}

std::string PS1Memory::GetDebugSummary() const {
  std::ostringstream os;
  os << "MEM cacheIso=" << cacheIsolated << " biosLoaded=" << biosLoaded;
  return os.str();
}

} // namespace AIO::Emulator::PS1
