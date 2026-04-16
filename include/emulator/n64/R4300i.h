#pragma once

#include <cstdint>
#include <array>

namespace N64Emulator {

class N64Memory;

// MIPS R4300i — 64-bit MIPS III ISA (32-bit mode baseline)
class R4300i {
 public:
  explicit R4300i(N64Memory* memory);
  ~R4300i() = default;

  // Cold reset — loads execution from 0x80000000 (BIOS entry)
  void Reset();

  // Execute one instruction; returns cycle count
  uint32_t Step();

  // Register file: 32 general-purpose registers (64-bit)
  uint64_t GetGPR(uint8_t reg) const { return gpr_[reg]; }
  void SetGPR(uint8_t reg, uint64_t val) { if (reg != 0) gpr_[reg] = val; }  // r0 always 0

  uint64_t GetPC() const { return pc_; }
  void SetPC(uint64_t pc) { pc_ = pc; }

  // Coprocessor 0 (system control)
  uint32_t GetCP0(uint8_t reg) const { return cp0_[reg]; }
  void SetCP0(uint8_t reg, uint32_t val) { cp0_[reg] = val; }

  // State save/restore
  struct State {
    std::array<uint64_t, 32> gpr;
    uint64_t pc;
    uint64_t hi;      // MULS hi
    uint64_t lo;      // MULS lo
    std::array<uint32_t, 32> cp0;
    bool ll_bit;      // Load-linked bit (for LL/SC)
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  // Register file
  std::array<uint64_t, 32> gpr_;   // r0–r31 (r0 always 0)
  uint64_t pc_;                    // Program counter
  uint64_t hi_, lo_;               // Multiply/divide result registers
  std::array<uint32_t, 32> cp0_;   // Coprocessor 0 registers
  bool ll_bit_;                    // Load-Linked flag for atomic ops

  N64Memory* memory_;              // Unowned reference

  // Memory access helpers (physical address translation — TLB stub)
  uint32_t VirtToPhys(uint64_t vaddr);
  uint8_t  Read8(uint64_t vaddr);
  uint16_t Read16(uint64_t vaddr);
  uint32_t Read32(uint64_t vaddr);
  uint64_t Read64(uint64_t vaddr);
  void Write8(uint64_t vaddr, uint8_t val);
  void Write16(uint64_t vaddr, uint16_t val);
  void Write32(uint64_t vaddr, uint32_t val);
  void Write64(uint64_t vaddr, uint64_t val);

  // Instruction decode helpers
  uint32_t Execute(uint32_t instr);
  void ExecuteSpecial(uint32_t instr);    // OPCODE=0x00
  void ExecuteRegImm(uint32_t instr);     // OPCODE=0x01
  void ExecuteCop0(uint32_t instr);       // Coprocessor 0

  // Delayed branch slot
  bool branch_delay_;
  uint64_t branch_target_;
};

}  // namespace N64Emulator
