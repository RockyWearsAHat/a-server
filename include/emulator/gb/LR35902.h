#pragma once

#include <cstdint>
#include <memory>

namespace GBEmulator {

class GBMemory;

// 8-bit CPU (Game Boy variant of Z80)
class LR35902 {
 public:
  explicit LR35902(GBMemory* memory);
  ~LR35902() = default;

  // Reset to power-on state (PC=0x0100)
  void Reset();

  // Execute one instruction; returns cycle count (4-16 cycles)
  uint32_t Step();

  // State save/restore
  struct State {
    uint16_t pc;      // Program counter
    uint8_t a;        // Accumulator
    uint8_t b;        // B register
    uint8_t c;        // C register
    uint8_t d;        // D register
    uint8_t e;        // E register
    uint8_t h;        // H register
    uint8_t l;        // L register
    uint8_t f;        // Flags (znhc--- 0)
    uint16_t sp;      // Stack pointer
    bool halt_mode;   // Halted waiting for interrupt
  };

  State SaveState() const;
  void LoadState(const State& state);

  // Register access
  uint16_t GetPC() const { return pc_; }
  void SetPC(uint16_t pc) { pc_ = pc; }
  uint16_t GetSP() const { return sp_; }
  uint8_t GetA() const { return a_; }
  uint8_t GetF() const { return f_; }
  bool IsHalted() const { return halt_mode_; }

 private:
  // Registers
  uint16_t pc_;       // Program counter, starts at 0x0100 after boot
  uint8_t a_, b_, c_, d_, e_, h_, l_;  // 8-bit registers
  uint8_t f_;         // Flags (z n h c 0 0 0 0): bit 7=Z, 6=N, 5=H, 4=C
  uint16_t sp_;       // Stack pointer
  bool halt_mode_;    // CPU halted, waiting for interrupt

  GBMemory* memory_;  // Unowned reference to memory

  // Flag helpers
  bool GetFlagZ() const { return (f_ & 0x80) != 0; }
  bool GetFlagN() const { return (f_ & 0x40) != 0; }
  bool GetFlagH() const { return (f_ & 0x20) != 0; }
  bool GetFlagC() const { return (f_ & 0x10) != 0; }

  void SetFlagZ(bool set) { f_ = set ? (f_ | 0x80) : (f_ & 0x7F); }
  void SetFlagN(bool set) { f_ = set ? (f_ | 0x40) : (f_ & 0xBF); }
  void SetFlagH(bool set) { f_ = set ? (f_ | 0x20) : (f_ & 0xDF); }
  void SetFlagC(bool set) { f_ = set ? (f_ | 0x10) : (f_ & 0xEF); }

  // Memory operations
  uint8_t Read8(uint16_t addr);
  void Write8(uint16_t addr, uint8_t val);
  uint16_t Read16(uint16_t addr);
  void Write16(uint16_t addr, uint16_t val);

  // Stack operations
  void Push8(uint8_t val);
  uint8_t Pop8();
  void Push16(uint16_t val);
  uint16_t Pop16();

  // Opcode dispatch
  uint32_t Execute(uint8_t opcode);
};

}  // namespace GBEmulator
