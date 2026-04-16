#pragma once

#include <cstdint>
#include <array>

namespace DreamcastEmulator {

class DreamcastMemory;

// Hitachi SH-4 — 32-bit superscalar RISC, 16-bit instruction words,
// FPU (single+double), MMU, 8 KB I-cache, 16 KB D-cache.
// Dreamcast runs at 200 MHz.
class SH4 {
 public:
  explicit SH4(DreamcastMemory* memory);
  ~SH4() = default;

  void Reset();

  // Execute one instruction; returns cycle count
  uint32_t Step();

  // General purpose registers (banked: RB0 vs RB1 controlled by SR.RB)
  uint32_t GetGPR(uint8_t reg) const { return r_[reg]; }
  void     SetGPR(uint8_t reg, uint32_t val) { r_[reg] = val; }
  uint32_t GetPC() const { return pc_; }
  void     SetPC(uint32_t pc) { pc_ = pc; }
  uint32_t GetSR() const { return sr_; }

  // Control registers
  uint32_t GetPR() const { return pr_; }   // Procedure register
  uint32_t GetGBR() const { return gbr_; } // Global base register
  uint32_t GetVBR() const { return vbr_; } // Vector base register
  uint32_t GetMACH() const { return mach_; }
  uint32_t GetMACL() const { return macl_; }

  // State
  struct State {
    std::array<uint32_t, 16> r;    // General-purpose (current bank)
    std::array<uint32_t, 8>  r_bank; // Shadow bank (R0–R7)
    uint32_t pc, pr, sr, gbr, vbr, mach, macl;
    bool halted;
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  std::array<uint32_t, 16> r_;
  std::array<uint32_t, 8>  r_bank_;  // R0–R7 alternate bank
  uint32_t pc_, pr_, sr_, gbr_, vbr_, mach_, macl_;
  bool delay_slot_;
  uint32_t delay_target_;
  bool halted_;

  DreamcastMemory* memory_;

  uint8_t  Read8 (uint32_t addr);
  uint16_t Read16(uint32_t addr);
  uint32_t Read32(uint32_t addr);
  void Write8 (uint32_t addr, uint8_t  val);
  void Write16(uint32_t addr, uint16_t val);
  void Write32(uint32_t addr, uint32_t val);

  uint32_t Execute(uint16_t op);
};

}  // namespace DreamcastEmulator
