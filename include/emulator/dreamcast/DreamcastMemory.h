#pragma once

#include <cstdint>
#include <vector>

namespace DreamcastEmulator {

class PowerVR2;

// DreamcastMemory — SH-4 physical address space dispatcher.
// Area-decoded: Area 0 = boot ROM/flash/IO, Area 1 = VRAM, Area 3 = system RAM.
class DreamcastMemory {
 public:
  DreamcastMemory();
  ~DreamcastMemory() = default;

  // Wire PowerVR2 after construction
  void Init(PowerVR2* pvr);

  uint8_t  Read8 (uint32_t addr);
  uint16_t Read16(uint32_t addr);
  uint32_t Read32(uint32_t addr);
  void Write8 (uint32_t addr, uint8_t  val);
  void Write16(uint32_t addr, uint16_t val);
  void Write32(uint32_t addr, uint32_t val);

  // Direct RAM pointer for DMA
  uint8_t* GetRamPtr() { return ram_.data(); }

  // State
  struct State {
    std::vector<uint8_t> ram;
    std::vector<uint8_t> aica_ram;
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  std::vector<uint8_t> ram_;       // 16 MB system SDRAM
  std::vector<uint8_t> aica_ram_;  // 8 MB AICA wave memory

  PowerVR2* pvr_;
};

}  // namespace DreamcastEmulator
