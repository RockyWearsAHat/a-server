#pragma once

#include <cstdint>
#include <vector>

namespace SaturnEmulator {

class SaturnMemory;
class VDP1;
class VDP2;
class SH2;

// SaturnMemory — physical address dispatcher for all Saturn subsystems.
class SaturnMemory {
 public:
  SaturnMemory();
  ~SaturnMemory() = default;

  // Wire subsystem pointers after construction
  void Init(VDP1* vdp1, VDP2* vdp2);

  // 8, 16, 32-bit physical reads/writes
  uint8_t  Read8 (uint32_t addr);
  uint16_t Read16(uint32_t addr);
  uint32_t Read32(uint32_t addr);
  void Write8 (uint32_t addr, uint8_t  val);
  void Write16(uint32_t addr, uint16_t val);
  void Write32(uint32_t addr, uint32_t val);

  // Direct RAM pointers
  uint8_t* GetLowRamPtr()  { return low_ram_.data(); }
  uint8_t* GetHighRamPtr() { return high_ram_.data(); }

  // State
  struct State {
    std::vector<uint8_t> low_ram;
    std::vector<uint8_t> high_ram;
    std::vector<uint8_t> sound_ram;
    std::vector<uint8_t> backup_ram;
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  std::vector<uint8_t> low_ram_;     // Work RAM-L  (1 MB)
  std::vector<uint8_t> high_ram_;    // Work RAM-H  (1 MB)
  std::vector<uint8_t> sound_ram_;   // SCSP sound RAM (512 KB)
  std::vector<uint8_t> backup_ram_;  // Internal backup RAM (32 KB)

  VDP1* vdp1_;
  VDP2* vdp2_;
};

}  // namespace SaturnEmulator
