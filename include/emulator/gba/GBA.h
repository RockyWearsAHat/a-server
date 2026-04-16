#pragma once
#include "APU.h"
#include "GBAMemory.h"
#include "PPU.h"
#include "ROMMetadataAnalyzer.h"
#include <atomic>
#include <cstdint>
#include <memory>

namespace AIO::Emulator::GBA {

} // namespace AIO::Emulator::GBA

namespace GBEmulator {
class GB;
}

namespace AIO::Emulator::GBA {

class ARM7TDMI;

class GBA {
public:
  enum class LoadedSystem {
    None,
    GameBoy,
    GameBoyAdvance,
  };

  GBA();
  ~GBA();

  bool LoadROM(const std::string &path);
  void Reset();
  int Step(); // Run one instruction/cycle, returns cycles consumed
  void UpdateInput(uint16_t keyState);
  void SaveGame();
  bool IsCPUHalted() const; // Check if CPU is waiting for interrupt

  const PPU &GetPPU() const { return *ppu; }
  PPU &GetPPU() { return *ppu; }
  APU &GetAPU() { return *apu; }
  GBAMemory &GetMemory() { return *memory; }
  const GBAMemory &GetMemory() const { return *memory; }

  uint32_t ReadMem(uint32_t addr);              // Debug helper
  uint16_t ReadMem16(uint32_t addr);            // Debug helper
  uint32_t ReadMem32(uint32_t addr);            // Debug helper
  void WriteMem(uint32_t addr, uint32_t val);   // Debug helper
  void WriteMem16(uint32_t addr, uint16_t val); // Debug helper
  uint32_t GetPC() const;                       // Debug helper
  bool IsThumbMode() const;                     // Debug helper
  uint32_t GetRegister(int reg) const;          // Debug helper
  void SetRegister(int reg, uint32_t val);      // Debug helper
  uint32_t GetCPSR() const;                     // Debug helper
  void PatchROM(uint32_t addr, uint32_t val);

  LoadedSystem GetLoadedSystem() const { return loadedSystem; }
  bool IsGameBoyFamilyMode() const {
    return loadedSystem == LoadedSystem::GameBoy;
  }
  int GetVideoWidth() const;
  int GetVideoHeight() const;
  int GetCyclesPerFrame() const;
  uint64_t GetNominalCpuHz() const;
  void CopyFramebufferTo(uint32_t *dst, size_t count) const;
  void SetOutputSampleRate(float hz);
  int GetAudioSamples(int16_t *buffer, int numSamples);
  void FlushSave();
  bool SupportsFrameHistory() const;
  bool SupportsAdvancedDebugging() const;

  // Total cycles executed since last Reset(); useful for deterministic tooling.
  uint64_t GetTotalCycles() const {
    return totalCyclesExecuted.load(std::memory_order_relaxed);
  }

  // Debugger controls (forwarded to ARM7TDMI)
  void AddBreakpoint(uint32_t addr);
  void ClearBreakpoints();
  void SetSingleStep(bool enabled);
  bool IsHalted() const; // CPU halted or debugger break
  void Continue();
  void DumpCPUState(std::ostream &os) const;
  void StepBack();

  // Flush pending peripheral cycles - called when graphics memory is written
  void FlushPendingPeripheralCycles();

private:
  std::unique_ptr<ARM7TDMI> cpu;
  std::unique_ptr<GBAMemory> memory;
  std::unique_ptr<PPU> ppu;
  std::unique_ptr<APU> apu;
  std::unique_ptr<GBEmulator::GB> gb;

  bool romLoaded = false;
  std::string savePath;
  ROMMetadata romMetadata;
  LoadedSystem loadedSystem = LoadedSystem::None;

  // Configure boot state based on intelligently detected ROM metadata
  void ConfigureBootStateFromMetadata(const ROMMetadata &metadata);

  // PC stall detection (treat long stalls as crash-equivalent)
  uint32_t lastPcForStall = 0;
  uint64_t stallCycleAccumulator = 0;
  bool stallCrashTriggered = false;
  static constexpr uint64_t STALL_CYCLE_THRESHOLD =
      167800000ULL; // ~10s @16.78MHz

  // Peripheral cycle batching: instead of updating PPU/APU/timers after
  // every CPU instruction, we accumulate cycles and flush right before
  // the next PPU event (HBlank at cycle 960, or end-of-line at cycle 1232).
  // This gives the CPU maximum lead time before HBlank DMA fires, which
  // is critical for games whose CPU fills per-scanline DMA source tables
  // (e.g., Classic NES Series scroll tables).  Timing-sensitive IO reads
  // (DISPSTAT/VCOUNT/timers) call FlushPendingPeripheralCycles() before
  // returning, so batching doesn't affect observable accuracy.
  int pendingPeripheralCycles = 0;

  std::atomic<uint64_t> totalCyclesExecuted{0};
};

} // namespace AIO::Emulator::GBA
