#pragma once

#include <cstdint>
#include <vector>

namespace PS2Emulator {

class GS;

// PS2Memory — EE physical address space dispatcher.
// EE main RAM (32 MB), IOP RAM (2 MB), scratch pad (16 KB),
// GS privileged registers, EE hardware registers.
class PS2Memory {
 public:
  PS2Memory();
  ~PS2Memory() = default;

  // Wire GS after construction
  void Init(GS* gs);

  uint8_t  Read8 (uint32_t addr);
  uint16_t Read16(uint32_t addr);
  uint32_t Read32(uint32_t addr);
  void Write8 (uint32_t addr, uint8_t  val);
  void Write16(uint32_t addr, uint16_t val);
  void Write32(uint32_t addr, uint32_t val);

  // IOP-side access (IOP uses its own 24-bit window; scaffold just aliases EE RAM)
  uint32_t IopRead32 (uint32_t addr);
  void     IopWrite32(uint32_t addr, uint32_t val);

  // Direct pointers for DMA
  uint8_t* GetEERamPtr()  { return ee_ram_.data(); }
  uint8_t* GetIopRamPtr() { return iop_ram_.data(); }

  // State
  struct State {
    std::vector<uint8_t> ee_ram;
    std::vector<uint8_t> iop_ram;
    std::vector<uint8_t> scratch;
  };

  State SaveState() const;
  void  LoadState(const State& state);

 private:
  std::vector<uint8_t> ee_ram_;    // 32 MB
  std::vector<uint8_t> iop_ram_;   //  2 MB
  std::vector<uint8_t> scratch_;   // 16 KB scratch pad

  GS* gs_;
};

}  // namespace PS2Emulator
