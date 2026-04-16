#pragma once

#include <cstdint>
#include <array>

namespace PS2Emulator {

class PS2Memory;

// IOP — PlayStation 1-derived R3000A MIPS processor @ 36.864 MHz.
// Handles I/O, CD/DVD, USB, FireWire, legacy PS1 compatibility.
// The scaffold runs as a simple 32-bit MIPS-I core.
class R3000A {
 public:
  explicit R3000A(PS2Memory* memory);
  ~R3000A() = default;

  void Reset();

  // Execute one instruction; returns cycle count
  uint32_t Step();

  uint32_t GetGPR(uint8_t reg) const { return gpr_[reg]; }
  void     SetGPR(uint8_t reg, uint32_t val) { if (reg) gpr_[reg] = val; }
  uint32_t GetPC() const { return pc_; }

  struct State {
    std::array<uint32_t, 32> gpr;
    uint32_t pc, hi, lo;
    bool halted;
  };

  State SaveState() const;
  void  LoadState(const State& state);

 private:
  std::array<uint32_t, 32> gpr_{};
  uint32_t pc_{}, hi_{}, lo_{};
  bool delay_slot_{false};
  uint32_t delay_target_{};
  bool halted_{false};

  PS2Memory* memory_;

  uint32_t Read32(uint32_t addr);
  void     Write32(uint32_t addr, uint32_t val);

  uint32_t Execute(uint32_t op);
};

}  // namespace PS2Emulator
