#include <cstdlib>
#include <emulator/common/Logger.h>
#include <emulator/gba/APU.h>
#include <emulator/gba/ARM7TDMI.h>
#include <emulator/gba/GBA.h>
#include <emulator/gba/GBAMemory.h>
#include <emulator/gba/ROMMetadataAnalyzer.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

namespace AIO::Emulator::GBA {

namespace {
inline bool EnvTruthy(const char *v) {
  return v != nullptr && v[0] != '\0' && v[0] != '0';
}

// Cache per-call-site (not per string length).
#define EnvFlagCached(name_literal)                                            \
  ([]() -> bool {                                                              \
    static const bool enabled = EnvTruthy(std::getenv(name_literal));          \
    return enabled;                                                            \
  }())

bool TraceGbaSpam() { return EnvFlagCached("AIO_TRACE_GBA_SPAM"); }
} // namespace

void GBA::WriteMem16(uint32_t addr, uint16_t val) {
  if (memory)
    memory->Write16(addr, val);
}

GBA::GBA() {
  memory = std::make_unique<GBAMemory>();
  cpu = std::make_unique<ARM7TDMI>(*memory);
  ppu = std::make_unique<PPU>(*memory);
  apu = std::make_unique<APU>(*memory);

  // Wire up APU to memory for timer overflow callbacks
  memory->SetAPU(apu.get());
  // Wire up PPU to memory for DMA updates
  memory->SetPPU(ppu.get());
  // Wire up CPU to memory for debug
  memory->SetCPU(cpu.get());

  // NOTE: Do NOT call Reset() here!
  // CPU must reset AFTER ROM is loaded, so that BIOS boot code
  // can properly jump to ROM entry at 0x08000000.
  // Instead, Reset() is called in LoadROM() after ROM is loaded.
}

GBA::~GBA() { SaveGame(); }

bool GBA::LoadROM(const std::string &path) {
  std::cout << "[LoadROM] Attempting to load: " << path << std::endl;

  // Try to open the ROM file. If it doesn't exist, try common locations
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  std::string resolvedPath = path;

  if (!file.is_open()) {
    std::cout << "[LoadROM] File not found at: " << path
              << ", trying alternate paths" << std::endl;

    // Try looking in current directory with just the filename
    if (path.find('/') == std::string::npos &&
        path.find('\\') == std::string::npos) {
      std::string filename = path;
      std::vector<std::string> searchPaths = {
          filename,           // Current directory
          "./" + filename,    // Explicit current directory
          "../" + filename,   // Parent directory
          "../../" + filename // Two levels up
      };

      for (const auto &tryPath : searchPaths) {
        std::cout << "[LoadROM] Trying: " << tryPath << std::endl;
        std::ifstream tryFile(tryPath, std::ios::binary | std::ios::ate);
        if (tryFile.is_open()) {
          std::cout << "[LoadROM] Found at: " << tryPath << std::endl;
          file = std::move(tryFile);
          resolvedPath = tryPath;
          break;
        }
      }
    }

    if (!file.is_open()) {
      std::cerr << "Failed to open ROM file: " << path << std::endl;
      return false;
    }
  } else {
    std::cout << "[LoadROM] Found at original path: " << path << std::endl;
  }

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> buffer(size);
  if (file.read(reinterpret_cast<char *>(buffer.data()), size)) {
    memory->LoadGamePak(buffer);
    romLoaded = true;

    // Analyze ROM metadata intelligently (must happen early)
    std::cerr << "[LoadROM] ===== Analyzing ROM Metadata =====" << std::endl;
    std::cerr.flush();
    ROMMetadata metadata = ROMMetadataAnalyzer::Analyze(buffer);
    std::cerr << "[LoadROM] ===== Metadata Analysis Complete ====="
              << std::endl;
    std::cerr.flush();

    // Apply game-specific ROM patches for known compatibility issues
    ApplyROMPatches(metadata);

    // Store metadata in memory for boot state configuration
    romMetadata = metadata;

    // Apply intelligent boot configuration based on detected metadata
    // THIS MUST HAPPEN BEFORE LoadSave so eepromIs64Kbit is set correctly!
    ConfigureBootStateFromMetadata(metadata);

    // Load Save - always save next to the ROM file
    const std::string &saveBase = resolvedPath.empty() ? path : resolvedPath;
    size_t lastDot = saveBase.find_last_of('.');
    if (lastDot != std::string::npos) {
      savePath = saveBase.substr(0, lastDot) + ".sav";
    } else {
      savePath = saveBase + ".sav";
    }

    std::cout << "[LoadROM] Save path resolved to: " << savePath << std::endl;

    // Set save path early so LoadSave can immediately flush/format when needed
    memory->SetSavePath(savePath);

    std::ifstream saveFile(savePath, std::ios::binary | std::ios::ate);
    std::cout << "[LoadROM] Attempting to open save file, is_open="
              << saveFile.is_open() << std::endl;
    if (saveFile.is_open()) {
      std::streamsize saveSize = saveFile.tellg();
      std::cout << "[LoadROM] Save file found: " << savePath << " (" << saveSize
                << " bytes)" << std::endl;
      saveFile.seekg(0, std::ios::beg);

      // Verify seek worked
      std::streampos pos = saveFile.tellg();
      std::cout << "[LoadROM] After seekg(0), position is: " << pos
                << std::endl;

      std::vector<uint8_t> saveData(saveSize);
      std::cout << "[LoadROM] Created saveData vector of size: "
                << saveData.size() << std::endl;

      if (saveFile.read(reinterpret_cast<char *>(saveData.data()), saveSize)) {
        std::cout << "[LoadROM] Successfully read " << saveSize
                  << " bytes from save file" << std::endl;
        std::cout << "[LoadROM] First 32 bytes of saveData: " << std::hex;
        for (size_t i = 0; i < std::min((size_t)saveSize, size_t(32)); i++) {
          std::cout << std::setw(2) << std::setfill('0') << (int)saveData[i]
                    << " ";
        }
        std::cout << std::dec << std::endl;
        std::cout << "[LoadROM] Bytes at offset 0x10-0x17: " << std::hex;
        for (size_t i = 0x10; i < std::min((size_t)saveSize, size_t(0x18));
             i++) {
          std::cout << std::setw(2) << std::setfill('0') << (int)saveData[i]
                    << " ";
        }
        std::cout << std::dec << std::endl;
        std::cout << "[LoadROM] Bytes at offset 0x18-0x2f: " << std::hex;
        for (size_t i = 0x18; i < std::min((size_t)saveSize, size_t(0x30));
             i++) {
          std::cout << std::setw(2) << std::setfill('0') << (int)saveData[i]
                    << " ";
        }
        std::cout << std::dec << std::endl;

        // DEBUG: Verify saveData immediately before passing to LoadSave
        std::cout
            << "[LoadROM DEBUG] About to call LoadSave. saveData[0x10-0x27]: "
            << std::hex;
        for (size_t i = 0x10; i < std::min((size_t)saveSize, size_t(0x28));
             i++) {
          std::cout << std::setw(2) << std::setfill('0') << (int)saveData[i]
                    << " ";
        }
        std::cout << std::dec << std::endl;
        memory->LoadSave(saveData);

        // Verify the load worked correctly
        auto loadedData = memory->GetSaveData();
        std::cout << "[LoadROM] Verification: GetSaveData() returns "
                  << loadedData.size() << " bytes" << std::endl;
        std::cout << "[LoadROM] Verification: First 32 bytes: " << std::hex;
        for (size_t i = 0; i < std::min(loadedData.size(), size_t(32)); i++) {
          std::cout << std::setw(2) << std::setfill('0') << (int)loadedData[i]
                    << " ";
        }
        std::cout << std::dec << std::endl;
        std::cout << "[LoadROM] Verification: Bytes at 0x10-0x17: " << std::hex;
        for (size_t i = 0x10; i < 0x18 && i < loadedData.size(); i++) {
          std::cout << std::setw(2) << std::setfill('0') << (int)loadedData[i]
                    << " ";
        }
        std::cout << std::dec << std::endl;
      } else {
        std::cout << "[LoadROM] Failed to read save file" << std::endl;
        // Failed to read save file, use default initialization
        memory->LoadSave(std::vector<uint8_t>());
      }
    } else {
      // Save file doesn't exist - this is normal on first run
      // Call LoadSave with empty data to ensure proper initialization
      memory->LoadSave(std::vector<uint8_t>());
    }

    // Optional verbose EEPROM logging via env var
    if (EnvFlagCached("AIO_VERBOSE_EEPROM")) {
      std::cout << "[LoadROM] Enabling verbose EEPROM logs" << std::endl;
      memory->SetVerboseLogs(true);
    }

    Reset();

    std::cout << "[LoadROM] CPU Reset complete. PC=0x" << std::hex
              << cpu->GetRegister(15) << " CPSR=0x" << cpu->GetCPSR()
              << std::dec << std::endl;
    std::cout << "[LoadROM] IME=" << memory->Read16(0x04000208) << " IE=0x"
              << std::hex << memory->Read16(0x04000200) << " IF=0x"
              << memory->Read16(0x04000202) << std::dec << std::endl;
    return true;
  }

  std::cerr << "Failed to read ROM file: " << path << std::endl;
  return false;
}

void GBA::ConfigureBootStateFromMetadata(const ROMMetadata &metadata) {
  std::cout
      << "[ConfigureBoot] Configuring boot state based on detected metadata..."
      << std::endl;

  // Apply region-specific BIOS settings
  std::cout << "[ConfigureBoot] Region: ";
  switch (metadata.region) {
  case Region::Japan:
    std::cout << "Japan" << std::endl;
    // Japanese cartridges may have different BIOS behavior
    break;
  case Region::NorthAmerica:
    std::cout << "North America" << std::endl;
    break;
  case Region::PAL:
    std::cout << "PAL/Europe" << std::endl;
    // PAL runs at 50Hz instead of 60Hz
    break;
  case Region::Korea:
    std::cout << "Korea" << std::endl;
    break;
  default:
    std::cout << "Unknown" << std::endl;
  }

  // Configure save type in memory subsystem
  std::cout << "[ConfigureBoot] Save Type: ";
  switch (metadata.saveType) {
  case SaveType::SRAM:
    std::cout << "SRAM" << std::endl;
    memory->SetSaveType(SaveType::SRAM);
    break;
  case SaveType::Flash512:
    std::cout << "Flash 512K" << std::endl;
    memory->SetSaveType(SaveType::Flash512);
    break;
  case SaveType::Flash1M:
    std::cout << "Flash 1M" << std::endl;
    memory->SetSaveType(SaveType::Flash1M);
    break;
  case SaveType::EEPROM_4K:
    std::cout << "EEPROM 4K" << std::endl;
    memory->SetSaveType(SaveType::EEPROM_4K);
    break;
  case SaveType::EEPROM_64K:
    std::cout << "EEPROM 64K" << std::endl;
    memory->SetSaveType(SaveType::EEPROM_64K);
    break;
  case SaveType::Auto:
    std::cout << "Auto (will detect at runtime)" << std::endl;
    memory->SetSaveType(SaveType::Auto);
    break;
  default:
    std::cout << "Unknown" << std::endl;
  }

  // Apply language-specific configuration if needed
  std::cout << "[ConfigureBoot] Language: ";
  switch (metadata.language) {
  case Language::English:
    std::cout << "English";
    break;
  case Language::Japanese:
    std::cout << "Japanese";
    break;
  case Language::French:
    std::cout << "French";
    break;
  case Language::German:
    std::cout << "German";
    break;
  case Language::Spanish:
    std::cout << "Spanish";
    break;
  case Language::Italian:
    std::cout << "Italian";
    break;
  case Language::Dutch:
    std::cout << "Dutch";
    break;
  case Language::Korean:
    std::cout << "Korean";
    break;
  default:
    std::cout << "Unknown";
    break;
  }
  std::cout << std::endl;

  // Apply game-specific boot configurations based on metadata
  // No hardcoded patches - everything is derived from the ROM's actual
  // structure
  std::cout << "[ConfigureBoot] Boot configuration complete" << std::endl;
}

void GBA::SaveGame() {
  if (savePath.empty() || !memory)
    return;

  std::vector<uint8_t> data = memory->GetSaveData();
  if (data.empty())
    return;

  std::ofstream file(savePath, std::ios::binary);
  if (file.is_open()) {
    file.write(reinterpret_cast<const char *>(data.data()), data.size());
  }
}

void GBA::Reset() {
  cpu->Reset();
  memory->Reset();
  apu->Reset();
  // ppu->Reset(); // TODO: Implement PPU Reset
  lastPcForStall = cpu->GetRegister(15);
  stallCycleAccumulator = 0;
  stallCrashTriggered = false;
  pendingPeripheralCycles = 0;
  totalCyclesExecuted.store(0, std::memory_order_relaxed);
}

int GBA::Step() {
  if (!romLoaded)
    return 0;

  uint32_t prevPc = cpu->GetRegister(15);

  // Loop detection for debugging stuck games
  static uint32_t lastPC = 0;
  static int pcRepeatCount = 0;
  static int totalSteps = 0;
  totalSteps++;

  // Verbose boot trace: useful for bring-up, but extremely expensive when
  // stdout is redirected to disk.
  if (TraceGbaSpam() && totalSteps <= 100) {
    if (totalSteps % 10 == 0 || totalSteps <= 10) {
      uint32_t pc = cpu->GetRegister(15);
      uint16_t ime = memory->Read16(0x04000208);
      uint16_t ie = memory->Read16(0x04000200);
      uint16_t if_reg = memory->Read16(0x04000202);
      std::cout << "[Step " << totalSteps << "] PC=0x" << std::hex << pc
                << " IME=" << std::dec << ime << " IE=0x" << std::hex << ie
                << " IF=0x" << if_reg << " Halted=" << std::dec
                << cpu->IsHalted() << std::endl;
    }
  }

  // Periodic progress trace: helps diagnose titles that run past boot but
  // remain in forced blank or never enable expected IRQs.
  // Enable with: AIO_TRACE_PROGRESS=1
  static const bool traceProgress = EnvFlagCached("AIO_TRACE_PROGRESS");
  if (traceProgress && (totalSteps % 500000u) == 0u) {
    const uint32_t pc = cpu->GetRegister(15);
    const uint32_t pcAligned = pc & ~1u;
    const uint16_t op0 = memory->Read16(pcAligned);
    const uint16_t op1 = memory->Read16(pcAligned + 2u);
    const uint32_t r2 = cpu->GetRegister(2);
    const uint32_t r3 = cpu->GetRegister(3);
    const uint32_t r4 = cpu->GetRegister(4);
    const uint32_t r5 = cpu->GetRegister(5);
    const uint32_t sp = cpu->GetRegister(13);
    const uint16_t ime = memory->Read16(0x04000208);
    const uint16_t ie = memory->Read16(0x04000200);
    const uint16_t if_reg = memory->Read16(0x04000202);
    const uint16_t dispcnt = memory->Read16(0x04000000);
    AIO::Emulator::Common::Logger::Instance().LogFmt(
        AIO::Emulator::Common::LogLevel::Info, "GBA",
      "PROGRESS step=%d PC=0x%08x thumb=%u halted=%u op0=0x%04x op1=0x%04x R2=0x%08x R3=0x%08x R4=0x%08x R5=0x%08x SP=0x%08x DISPCNT=0x%04x IME=%u IE=0x%04x IF=0x%04x",
      totalSteps, (unsigned)pc, (unsigned)(cpu->IsThumbModeFlag() ? 1 : 0),
      (unsigned)(cpu->IsHalted() ? 1 : 0), (unsigned)op0, (unsigned)op1,
      (unsigned)r2, (unsigned)r3, (unsigned)r4, (unsigned)r5, (unsigned)sp,
      (unsigned)dispcnt, (unsigned)(ime & 1), (unsigned)ie,
      (unsigned)if_reg);
  }

  if (prevPc == lastPC) {
    pcRepeatCount++;
    if (pcRepeatCount == 10000 && TraceGbaSpam()) {
      std::cout << "[LOOP DETECTED] PC=0x" << std::hex << prevPc
                << " stuck for 10k steps. Total steps: " << std::dec
                << totalSteps << std::endl;
      std::cout << "  R0=0x" << std::hex << cpu->GetRegister(0) << " R1=0x"
                << cpu->GetRegister(1) << " R2=0x" << cpu->GetRegister(2)
                << " R3=0x" << cpu->GetRegister(3) << std::endl;
      std::cout << "  SP=0x" << cpu->GetRegister(13) << " LR=0x"
                << cpu->GetRegister(14) << " CPSR=0x" << cpu->GetCPSR()
                << std::dec << std::endl;
    }
  } else {
    lastPC = prevPc;
    pcRepeatCount = 0;
  }

  // Step CPU by one instruction
  uint32_t pcBefore = cpu->GetRegister(15);
  cpu->Step();

  // Some HLE paths (notably BIOS SWIs) may advance peripheral time in bulk.
  // Account those cycles in the frame budget so we don't overrun CPU work.
  const int hleCycles = cpu->ConsumeHLECycles();

  // Calculate actual instruction cycles with memory wait states:
  // 1. Base instruction execution (Thumb=1, ARM=1 for most operations)
  // 2. Memory access penalties based on region (ROM is slow, IWRAM is fast)
  // 3. Pipeline refill penalties for branches

  const bool isThumb = cpu->IsThumbModeFlag();
  int cpuCycles = isThumb ? 1 : 1; // Base execution cycle

  // Add memory access cycles for instruction fetch from the PC before execution
  cpuCycles += memory->GetAccessCycles(pcBefore, isThumb ? 2 : 4);

  // Add branch penalty if PC changed non-sequentially (taken branch/jump)
  uint32_t pcAfter = cpu->GetRegister(15);
  uint32_t expectedNextPC = (pcBefore & ~0x1) + (isThumb ? 2 : 4);
  if ((pcAfter & ~0x1) != expectedNextPC) {
    // Branch taken - add pipeline refill penalty (2S cycles)
    cpuCycles += 2;
  }

  int dmaCycles = memory->GetLastDMACycles();

  // DMA cycles were already applied to timers/PPU/APU inside PerformDMA().
  // HLE cycles must be applied by the outer loop (us).
  // When CPU is halted, fast-forward time so PPU/timers can reach VBlank/IRQs.
  int totalCycles = cpuCycles + dmaCycles + hleCycles;

  int peripheralCycles = cpuCycles + hleCycles;
  if (cpu->IsHalted()) {
    totalCycles = 1232;
    peripheralCycles = totalCycles;
  }

  uint32_t currPc = cpu->GetRegister(15);
  if (currPc == prevPc) {
    stallCycleAccumulator += static_cast<uint64_t>(totalCycles);
    if (!stallCrashTriggered &&
        stallCycleAccumulator >= STALL_CYCLE_THRESHOLD) {
      Common::Logger::Instance().LogFmt(
          Common::LogLevel::Fatal, "GBA",
          "PC stall detected: PC=0x%08X Thumb=%d Cycles=%llu", currPc,
          IsThumbMode() ? 1 : 0,
          static_cast<unsigned long long>(stallCycleAccumulator));
      stallCrashTriggered = true;
      if (CrashPopupCallback)
        CrashPopupCallback("crash_log.txt");
    }
  } else {
    lastPcForStall = currPc;
    stallCycleAccumulator = 0;
    stallCrashTriggered = false;
  }

  // Batch peripheral time advancement to avoid doing 3 updates + IRQ polling
  // on every instruction.
  //
  // IMPORTANT: Some games rely on raster-timed work (HBlank/VCount IRQ or
  // HBlank/VBlank DMA) to update VRAM/scroll state every scanline. In those
  // cases, letting the CPU run too far ahead of the PPU can delay IRQ/DMA
  // signaling enough to miss the expected window, causing visible artifacts.
  //
  // We therefore dynamically disable batching (cycle-accurate peripherals)
  // when raster timing is in use.
  int effectiveBatchCycles = PERIPHERAL_BATCH_CYCLES;
  {
    // Optional override:
    //   AIO_PERIPHERAL_BATCH_CYCLES=<n>
    // Use 1 for maximum accuracy (slowest), or 64 (default) for speed.
    static int cachedOverride = -2; // -2 = uninitialized, -1 = none
    if (cachedOverride == -2) {
      cachedOverride = -1;
      if (const char *s = std::getenv("AIO_PERIPHERAL_BATCH_CYCLES")) {
        const int v = std::atoi(s);
        if (v > 0) {
          cachedOverride = v;
        }
      }
    }
    if (cachedOverride > 0) {
      effectiveBatchCycles = cachedOverride;
    } else {
      // Raster-timing sensitivity heuristics:
      // - HBlank IRQ enable (DISPSTAT bit 4)
      // - VCount IRQ enable (DISPSTAT bit 5)
      // - Any DMA channel enabled with VBlank/HBlank start timing
      const uint16_t dispstat = memory->Read16(0x04000004);
      const bool rasterIrqEnabled = (dispstat & 0x10u) || (dispstat & 0x20u);

      auto dmaIsRasterTimed = [&](uint32_t cntHAddr) -> bool {
        const uint16_t cntH = memory->Read16(cntHAddr);
        if ((cntH & 0x8000u) == 0) {
          return false;
        }
        const uint16_t timing = (cntH >> 12) & 0x3u;
        return (timing == 1u) || (timing == 2u); // VBlank or HBlank
      };

      const bool rasterDmaEnabled =
          dmaIsRasterTimed(0x040000BAu) || // DMA0CNT_H
          dmaIsRasterTimed(0x040000C6u) || // DMA1CNT_H
          dmaIsRasterTimed(0x040000D2u) || // DMA2CNT_H
          dmaIsRasterTimed(0x040000DEu);   // DMA3CNT_H

      if (rasterIrqEnabled || rasterDmaEnabled) {
        effectiveBatchCycles = 1;
      }
    }
  }

  pendingPeripheralCycles += peripheralCycles;
  if (pendingPeripheralCycles >= effectiveBatchCycles || cpu->IsHalted()) {
    memory->AdvanceCycles(pendingPeripheralCycles);
    pendingPeripheralCycles = 0;
    cpu->PollInterrupts();
  }

  totalCyclesExecuted.fetch_add((uint64_t)totalCycles,
                                std::memory_order_relaxed);
  return totalCycles;
}

bool GBA::IsCPUHalted() const { return cpu && cpu->IsHalted(); }

void GBA::UpdateInput(uint16_t keyState) {
  if (memory) {
    memory->SetKeyInput(keyState);
  }
}

uint32_t GBA::ReadMem(uint32_t addr) {
  if (memory)
    return memory->Read32(addr);
  return 0;
}

uint16_t GBA::ReadMem16(uint32_t addr) {
  if (memory)
    return memory->Read16(addr);
  return 0;
}

uint32_t GBA::ReadMem32(uint32_t addr) {
  if (memory)
    return memory->Read32(addr);
  return 0;
}

void GBA::WriteMem(uint32_t addr, uint32_t val) {
  if (memory)
    memory->Write32(addr, val);
}

uint32_t GBA::GetPC() const {
  if (cpu)
    return cpu->GetRegister(15);
  return 0;
}

bool GBA::IsThumbMode() const {
  if (cpu)
    return (cpu->GetCPSR() & 0x20) != 0;
  return false;
}

uint32_t GBA::GetRegister(int reg) const { return cpu->GetRegister(reg); }

uint32_t GBA::GetCPSR() const {
  if (cpu)
    return cpu->GetCPSR();
  return 0;
}

void GBA::PatchROM(uint32_t addr, uint32_t val) {
  std::cout << "[PatchROM] Addr=" << std::hex << addr << " Val=" << val
            << std::dec << std::endl;
  memory->WriteROM32(addr, val);
}

void GBA::ApplyROMPatches(const ROMMetadata &metadata) {
  // Apply game-specific ROM patches for known compatibility issues
  // Currently no patches needed - EEPROM implementation is now correct
  (void)metadata; // Suppress unused parameter warning
}

// Debugger controls
void GBA::AddBreakpoint(uint32_t addr) {
  if (cpu)
    cpu->AddBreakpoint(addr);
}

void GBA::ClearBreakpoints() {
  if (cpu)
    cpu->ClearBreakpoints();
}

void GBA::SetSingleStep(bool enabled) {
  if (cpu)
    cpu->SetSingleStep(enabled);
}

bool GBA::IsHalted() const {
  if (!cpu)
    return false;
  return cpu->IsHalted();
}

void GBA::Continue() {
  if (cpu)
    cpu->Continue();
}

void GBA::DumpCPUState(std::ostream &os) const {
  if (cpu)
    cpu->DumpState(os);
}

void GBA::StepBack() {
  if (cpu)
    cpu->StepBack();
}

} // namespace AIO::Emulator::GBA
