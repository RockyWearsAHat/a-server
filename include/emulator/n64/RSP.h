#pragma once

#include <cstdint>
#include <vector>

namespace N64Emulator {

class N64Memory;

// Reality Signal Processor — RSP (vector DSP for graphics/audio commands)
class RSP {
 public:
  explicit RSP(N64Memory* memory);
  ~RSP() = default;

  // Reset RSP to idle state
  void Reset();

  // Execute one RSP instruction (from IMEM); returns cycle count
  uint32_t Step();

  // RSP status
  bool IsHalted() const { return halted_; }
  void SetHalted(bool halted) { halted_ = halted; }

  // State save/restore
  struct State {
    std::vector<uint8_t> dmem;        // 4 KB data memory
    std::vector<uint8_t> imem;        // 4 KB instruction memory
    uint32_t pc;                      // RSP program counter
    std::array<uint32_t, 32> gpr;     // RSP scalar registers
    bool halted;
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  std::vector<uint8_t> dmem_;         // 4 KB data memory
  std::vector<uint8_t> imem_;         // 4 KB instruction memory
  uint32_t pc_;                       // RSP program counter (12-bit)
  std::array<uint32_t, 32> gpr_;      // 32 scalar integer registers
  bool halted_;                       // RSP halt (set by CPU via status register)

  N64Memory* memory_;                 // Unowned reference

  uint32_t Execute(uint32_t instr);
};

}  // namespace N64Emulator
