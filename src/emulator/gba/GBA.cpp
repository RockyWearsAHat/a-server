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
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace AIO::Emulator::GBA {

namespace {
inline bool EnvTruthy(const char *v) {
  return v != nullptr && v[0] != '\0' && v[0] != '0';
}

template <size_t N> inline bool EnvFlagCached(const char (&name)[N]) {
  static const bool enabled = EnvTruthy(std::getenv(name));
  return enabled;
}

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
  // Wire up GBA pointer for timing flush callbacks (used by GBAMemory)
  memory->SetGBA(this);

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
    ROMMetadata metadata = ROMMetadataAnalyzer::Analyze(buffer);

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

    // Optional LLE BIOS support via environment variable.
    // If AIO_GBA_BIOS is set to a valid BIOS image path, load it into the
    // BIOS region so the CPU can execute the real BIOS instead of HLE.
    if (const char *biosPath = std::getenv("AIO_GBA_BIOS")) {
      if (biosPath[0] != '\0') {
        std::cout << "[LoadROM] Attempting to load LLE BIOS from: " << biosPath
                  << std::endl;
        if (!memory->LoadLLEBIOS(biosPath)) {
          std::cout
              << "[LoadROM] LLE BIOS load failed; continuing with HLE BIOS"
              << std::endl;
        }
      }
    }

    Reset();

    // Post-reset configuration: Apply PPU settings that would be cleared by
    // Reset() Classic NES Series games need special palette handling
    if (romMetadata.gameCode.size() >= 2 &&
        romMetadata.gameCode.substr(0, 2) == "FD") {
      std::cout << "[LoadROM] Re-applying Classic NES mode after Reset()"
                << std::endl;
      ppu->SetClassicNesMode(true);
    }

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

  // Detect Classic NES Series games for palette workaround
  // These games have game codes starting with "FD" (FDKE = Donkey Kong, FDME =
  // Mario Bros, etc.)
  if (metadata.gameCode.size() >= 2 && metadata.gameCode.substr(0, 2) == "FD") {
    ppu->SetClassicNesMode(true);
  }

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
  ppu->Reset();
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

      // Also capture display timing state when we detect a tight CPU loop.
      // Many games (including OG-DK style emulation titles) busy-wait on
      // VCOUNT / DISPSTAT, so seeing their instantaneous values helps
      // distinguish a legitimate wait loop from a broken PPU/VCOUNT model.
      uint16_t dispstat = memory->Read16(0x04000004);
      uint16_t vcount = memory->Read16(0x04000006);
      uint16_t ime = memory->Read16(0x04000208);
      uint16_t ie = memory->Read16(0x04000200);
      uint16_t if_reg = memory->Read16(0x04000202);
      std::cout << "  DISPSTAT=0x" << std::hex << dispstat
                << " VCOUNT=" << std::dec << (unsigned)vcount << " IME=" << ime
                << " IE=0x" << std::hex << ie << " IF=0x" << if_reg << std::dec
                << std::endl;
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
  // on every instruction. This is a major speed win and still preserves
  // ordering (peripherals advance after each instruction, just grouped).
  pendingPeripheralCycles += peripheralCycles;
  if (pendingPeripheralCycles >= PERIPHERAL_BATCH_CYCLES || cpu->IsHalted()) {
    memory->AdvanceCycles(pendingPeripheralCycles);
    pendingPeripheralCycles = 0;
    cpu->PollInterrupts();
  }

  totalCyclesExecuted.fetch_add((uint64_t)totalCycles,
                                std::memory_order_relaxed);

  // Optional: periodic PC sampling for regression triage.
  // Enable with: AIO_TRACE_PC_EVERY_CYCLES=<N>
  // Example: AIO_TRACE_PC_EVERY_CYCLES=16777216 (roughly 1 second)
  {
    static bool parsed = false;
    static uint64_t every = 0;
    static uint64_t nextAt = 0;
    if (!parsed) {
      parsed = true;
      if (const char *s = std::getenv("AIO_TRACE_PC_EVERY_CYCLES")) {
        const long long v = std::atoll(s);
        if (v > 0) {
          every = (uint64_t)v;
          nextAt = every;
        }
      }
    }
    if (every != 0) {
      const uint64_t nowCycles =
          totalCyclesExecuted.load(std::memory_order_relaxed);
      if (nowCycles >= nextAt) {
        nextAt += every;
        const uint32_t pc = GetPC();
        const bool thumb = IsThumbMode();
        Common::Logger::Instance().LogFmt(
            Common::LogLevel::Info, "GBA",
            "PC_SAMPLE cycles=%llu PC=0x%08X Thumb=%d halted=%d",
            (unsigned long long)nowCycles, (unsigned)pc, thumb ? 1 : 0,
            (IsCPUHalted() ? 1 : 0));
      }
    }
  }
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

void GBA::SetRegister(int reg, uint32_t val) {
  if (cpu)
    cpu->SetRegister(reg, val);
}

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

void GBA::FlushPendingPeripheralCycles() {
  if (pendingPeripheralCycles > 0) {
    memory->AdvanceCycles(pendingPeripheralCycles);
    pendingPeripheralCycles = 0;
    cpu->PollInterrupts();
  }
}

void GBA::StepBack() {
  if (cpu)
    cpu->StepBack();
}

} // namespace AIO::Emulator::GBA
