#pragma once

#include <cstdint>
#include <array>

namespace PS2Emulator {

class PS2Memory;

// MIPS R5900 "Emotion Engine" CPU
// 128-bit SIMD multimedia extension registers (MMI), 64-bit integer path,
// two 32-bit ALUs, hardware multiply-accumulate (HI0/HI1, LO0/LO1).
// The scaffold tracks the 32-bit PC and 32×64-bit GPRs.
class R5900 {
 public:
  explicit R5900(PS2Memory* memory);
  ~R5900() = default;

  void Reset();

  // Execute one instruction; returns cycle count
  uint32_t Step();

  uint64_t GetGPR(uint8_t reg) const { return gpr_[reg]; }
  void     SetGPR(uint8_t reg, uint64_t val) { if (reg) gpr_[reg] = val; }

  uint32_t GetPC() const { return pc_; }
  void     SetPC(uint32_t pc) { pc_ = pc; }

  uint32_t GetHI() const { return hi_; }
  uint32_t GetLO() const { return lo_; }
  uint32_t GetCP0(uint8_t reg) const { return cp0_[reg]; }

  // State
  struct State {
    std::array<uint64_t, 32> gpr;
    uint32_t pc, hi, lo;
    std::array<uint32_t, 32> cp0;
  };

  State SaveState() const;
  void  LoadState(const State& state);

 private:
  std::array<uint64_t, 32> gpr_{};
  uint32_t pc_{}, hi_{}, lo_{};
  std::array<uint32_t, 32> cp0_{};
  bool delay_slot_{false};
  uint32_t delay_target_{};

  PS2Memory* memory_;

  uint8_t  Read8 (uint32_t addr);
  uint16_t Read16(uint32_t addr);
  uint32_t Read32(uint32_t addr);
  void Write32(uint32_t addr, uint32_t val);

  uint32_t Execute(uint32_t op);
  uint32_t ExecuteSpecial(uint32_t op);
  uint32_t ExecuteRegImm(uint32_t op);
  uint32_t ExecuteCop0(uint32_t op);
};

}  // namespace PS2Emulator
