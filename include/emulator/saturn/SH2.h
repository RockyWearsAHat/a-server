#pragma once

#include <cstdint>
#include <array>

namespace SaturnEmulator {

class SaturnMemory;

// Hitachi SH-2 CPU — 32-bit RISC, 16-bit fixed-width instructions, variable-length delay slots.
// Used as both Master SH-2 and Slave SH-2 (same architecture, different IRQ routing).
class SH2 {
 public:
  explicit SH2(SaturnMemory* memory, bool is_slave = false);
  ~SH2() = default;

  void Reset();

  // Execute one instruction; returns cycle count
  uint32_t Step();

  // Register access
  uint32_t GetGPR(uint8_t reg) const { return r_[reg]; }
  void     SetGPR(uint8_t reg, uint32_t val) { r_[reg] = val; }
  uint32_t GetPC() const { return pc_; }
  void     SetPC(uint32_t pc) { pc_ = pc; }
  uint32_t GetSR() const { return sr_; }  // Status register (T, S, I, Q, M bits)
  uint32_t GetSP() const { return r_[15]; }

  // State
  struct State {
    std::array<uint32_t, 16> r;  // R0–R15 (R15 = SP)
    uint32_t pc;
    uint32_t pr;   // Procedure register (link register)
    uint32_t sr;   // Status register
    uint32_t gbr;  // Global base register
    uint32_t vbr;  // Vector base register
    uint32_t mach; // Multiply accumulate high
    uint32_t macl; // Multiply accumulate low
    bool halted;
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  std::array<uint32_t, 16> r_;
  uint32_t pc_, pr_, sr_, gbr_, vbr_, mach_, macl_;
  bool delay_slot_;
  uint32_t delay_target_;
  bool halted_;
  bool is_slave_;

  SaturnMemory* memory_;

  // Memory helpers
  uint8_t  Read8 (uint32_t addr);
  uint16_t Read16(uint32_t addr);
  uint32_t Read32(uint32_t addr);
  void Write8 (uint32_t addr, uint8_t  val);
  void Write16(uint32_t addr, uint16_t val);
  void Write32(uint32_t addr, uint32_t val);

  // Instruction decode helpers
  uint32_t Execute(uint16_t op);
};

}  // namespace SaturnEmulator
