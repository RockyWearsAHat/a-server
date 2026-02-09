#include <algorithm>
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
#include <string>
#include <vector>

namespace AIO::Emulator::GBA {

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
  // Wire up GBA to memory for flush callbacks
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

      std::vector<uint8_t> saveData(saveSize);
      if (saveFile.read(reinterpret_cast<char *>(saveData.data()), saveSize)) {
        memory->LoadSave(saveData);
      } else {
        std::cout << "[LoadROM] Failed to read save file" << std::endl;
        memory->LoadSave(std::vector<uint8_t>());
      }
    } else {
      // Save file doesn't exist - this is normal on first run
      // Call LoadSave with empty data to ensure proper initialization
      memory->LoadSave(std::vector<uint8_t>());
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

    // Enable Classic NES mode for FD* games (Donkey Kong, etc.)
    // These games use NES 2bpp tile format stored in GBA VRAM
    if (memory->IsClassicNES()) {
      ppu->SetClassicNesMode(true);
      std::cout << "[LoadROM] Enabled Classic NES mode" << std::endl;
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

  // Classic NES Series games (game codes starting with "FD") use standard GBA
  // rendering - they internally emulate an NES but output through the normal
  // GBA PPU pipeline with standard 4bpp tiles and 2-byte tilemap entries.
  // No special handling is needed.

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

  // Step CPU by one instruction
  uint32_t pcBefore = cpu->GetRegister(15);
  memory->BeginCpuDataAccess();
  cpu->Step();
  int dataAccessCycles = memory->EndCpuDataAccess();

  // Some HLE paths (notably BIOS SWIs) may advance peripheral time in bulk.
  // Account those cycles in the frame budget so we don't overrun CPU work.
  const int hleCycles = cpu->ConsumeHLECycles();

  // ARM7TDMI is a 3-stage pipeline (Fetch/Decode/Execute). Stages overlap,
  // so the visible wall-clock cost per instruction is just the FETCH of the
  // next instruction — there is no separate "execute" cycle to charge.
  const bool isThumb = cpu->IsThumbModeFlag();
  int cpuCycles = memory->GetAccessCycles(pcBefore, isThumb ? 2 : 4, true);

  // Add data access wait states (LDR/STR to slow regions like ROM)
  cpuCycles += dataAccessCycles;

  // Add branch penalty if PC changed non-sequentially (taken branch/jump)
  uint32_t pcAfter = cpu->GetRegister(15);
  uint32_t expectedNextPC = (pcBefore & ~0x1) + (isThumb ? 2 : 4);
  if ((pcAfter & ~0x1) != expectedNextPC) {
    // Branch taken - add pipeline refill penalty (2S cycles)
    cpuCycles += 2;
  }

  int dmaCycles = memory->GetLastDMACycles();

  // DMA cycles were already applied to timers/PPU/APU inside PerformDMA().
  // HLE cycles were already applied inside AdvanceHLECycles() — do NOT
  // add them to peripheralCycles or they will be double-counted.
  int totalCycles = cpuCycles + dmaCycles + hleCycles;

  int peripheralCycles = cpuCycles;
  if (cpu->IsHalted()) {
    // During HALT the CPU stops executing instructions, but hardware
    // peripherals (PPU, timers, APU) keep running. Fast-forward time by one
    // scanline (~1232 cycles) so timers fire at the correct rate and audio
    // FIFOs are fed properly.
    totalCycles = 1232;
    peripheralCycles = totalCycles;
  }

  uint32_t currPc = cpu->GetRegister(15);
  if (currPc == prevPc && !cpu->IsHalted()) {
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

  // DIAG: track average cycles per instruction
  {
    static uint64_t diagSteps = 0;
    static uint64_t diagTotalCyc = 0;
    static uint64_t diagBranchPenalties = 0;
    static uint64_t diagDataCycles = 0;
    static uint64_t diagCpuCycles = 0;
    static uint64_t diagFetchRom = 0;
    static uint64_t diagFetchIwram = 0;
    static uint64_t diagDmaCyc = 0;
    static uint64_t diagHaltSteps = 0;
    static uint64_t diagNonHaltSteps = 0;
    uint8_t fetchRgn = (uint8_t)(pcBefore >> 24);
    diagSteps++;
    diagTotalCyc += totalCycles;
    diagDmaCyc += dmaCycles;
    if (cpu->IsHalted()) {
      diagHaltSteps++;
    } else {
      diagDataCycles += dataAccessCycles;
      diagCpuCycles += cpuCycles;
      diagNonHaltSteps++;
      if (fetchRgn >= 0x08 && fetchRgn <= 0x0D)
        diagFetchRom++;
      else if (fetchRgn == 0x03)
        diagFetchIwram++;
      if ((pcAfter & ~0x1) != expectedNextPC)
        diagBranchPenalties++;
    }
    if (diagSteps % 5000000 == 0) {
      static FILE *cycF = nullptr;
      if (!cycF)
        cycF = fopen("/tmp/cycle_diag.txt", "w");
      if (cycF) {
        uint16_t waitcnt = memory->Read16(0x04000204);
        double avgCpu =
            diagNonHaltSteps ? (double)diagCpuCycles / diagNonHaltSteps : 0;
        double avgData =
            diagNonHaltSteps ? (double)diagDataCycles / diagNonHaltSteps : 0;
        fprintf(cycF,
                "steps=%llu haltSteps=%llu nonHalt=%llu avgCPU=%.2f "
                "branches=%llu dataCyc=%llu avgData=%.2f "
                "fetchRom=%llu fetchIwram=%llu dmaCyc=%llu WAITCNT=0x%04X\n",
                diagSteps, diagHaltSteps, diagNonHaltSteps, avgCpu,
                diagBranchPenalties, diagDataCycles, avgData, diagFetchRom,
                diagFetchIwram, diagDmaCyc, waitcnt);
        fflush(cycF);
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
