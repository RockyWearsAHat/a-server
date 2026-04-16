#pragma once

#include <cstdint>
#include <vector>

namespace N64Emulator {

class N64Cartridge;
class RDP;
class RSP;

// N64 memory map dispatcher
class N64Memory {
 public:
  N64Memory();
  ~N64Memory() = default;

  // Wire peripherals after construction
  void Init(N64Cartridge* cart, RDP* rdp, RSP* rsp);

  // 8, 16, 32-bit physical address reads/writes
  uint8_t  PhysRead8 (uint32_t paddr);
  uint16_t PhysRead16(uint32_t paddr);
  uint32_t PhysRead32(uint32_t paddr);
  void PhysWrite8 (uint32_t paddr, uint8_t  val);
  void PhysWrite16(uint32_t paddr, uint16_t val);
  void PhysWrite32(uint32_t paddr, uint32_t val);

  // Direct RDRAM pointer for DMA
  uint8_t* GetRdramPtr() { return rdram_.data(); }

  // State save/restore
  struct State {
    std::vector<uint8_t> rdram;
    std::vector<uint32_t> mi_regs;   // MIPS interface registers
    std::vector<uint32_t> vi_regs;   // Video interface registers
    std::vector<uint32_t> ai_regs;   // Audio interface registers
    std::vector<uint32_t> pi_regs;   // Peripheral interface registers
    std::vector<uint32_t> si_regs;   // Serial interface registers
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  std::vector<uint8_t>  rdram_;        // 8 MB–16 MB RDRAM
  std::vector<uint32_t> mi_regs_;      // MIPS interface (4 regs)
  std::vector<uint32_t> vi_regs_;      // Video interface (14 regs)
  std::vector<uint32_t> ai_regs_;      // Audio interface (6 regs)
  std::vector<uint32_t> pi_regs_;      // Peripheral interface (13 regs)
  std::vector<uint32_t> si_regs_;      // Serial interface (6 regs)

  N64Cartridge* cart_;                 // Unowned
  RDP* rdp_;                           // Unowned
  RSP* rsp_;                           // Unowned
};

}  // namespace N64Emulator
