#pragma once

#include <cstdint>
#include <array>

namespace GameCubeEmulator {

class GameCubeMemory;

// IBM PowerPC 750CL "Gecko" — 32-bit PowerPC G3 derivative.
// 32×32-bit GPRs, 32×64-bit FPRs, CR, LR, CTR, XER, MSR, SRR0/SRR1.
// The scaffold implements the integer core and branch unit.
class Gecko {
 public:
  explicit Gecko(GameCubeMemory* memory);
  ~Gecko() = default;

  void Reset();

  // Execute one instruction; returns cycle count
  uint32_t Step();

  // General-purpose registers
  uint32_t GetGPR(uint8_t reg) const { return gpr_[reg]; }
  void     SetGPR(uint8_t reg, uint32_t val) { gpr_[reg] = val; }
  uint32_t GetPC() const { return pc_; }
  void     SetPC(uint32_t pc) { pc_ = pc; }
  uint32_t GetLR() const { return lr_; }
  uint32_t GetCTR() const { return ctr_; }
  uint32_t GetCR() const { return cr_; }
  uint32_t GetXER() const { return xer_; }
  uint32_t GetMSR() const { return msr_; }

  // State
  struct State {
    std::array<uint32_t, 32> gpr;
    uint32_t pc, lr, ctr, cr, xer, msr;
  };

  State SaveState() const;
  void  LoadState(const State& state);

 private:
  std::array<uint32_t, 32> gpr_{};
  uint32_t pc_{}, lr_{}, ctr_{}, cr_{}, xer_{}, msr_{};
  bool branch_delay_{false};
  uint32_t branch_target_{};

  GameCubeMemory* memory_;

  uint8_t  Read8 (uint32_t addr);
  uint16_t Read16(uint32_t addr);
  uint32_t Read32(uint32_t addr);
  void Write8 (uint32_t addr, uint8_t  val);
  void Write16(uint32_t addr, uint16_t val);
  void Write32(uint32_t addr, uint32_t val);

  uint32_t Execute(uint32_t op);
  uint32_t ExecuteInteger(uint32_t op);    // primary 31 (XO-form)
  uint32_t ExecuteBranch (uint32_t op);
};

}  // namespace GameCubeEmulator
