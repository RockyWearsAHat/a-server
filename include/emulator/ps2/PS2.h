#pragma once

#include "emulator/ps2/R5900.h"
#include "emulator/ps2/R3000A.h"
#include "emulator/ps2/GS.h"
#include "emulator/ps2/PS2Memory.h"
#include <string>

namespace PS2Emulator {

// PlayStation 2 top-level system
class PS2 {
 public:
  PS2();
  ~PS2() = default;

  void Load(const std::string& disc_path);
  void Reset();

  uint32_t Step();
  void RunFrame();

  uint32_t GetFrameCount()         const { return gs_.GetFrameCount(); }
  const uint32_t* GetFramebuffer() const { return gs_.GetFramebuffer(); }

  R5900*    GetEE()     { return &ee_; }
  R3000A*   GetIOP()    { return &iop_; }
  GS*       GetGS()     { return &gs_; }
  PS2Memory* GetMemory() { return &memory_; }

  // State
  struct State {
    R5900::State   ee;
    R3000A::State  iop;
    GS::State      gs;
    PS2Memory::State memory;
    bool running;
  };

  State SaveState() const;
  void  LoadState(const State& state);

 private:
  PS2Memory memory_;
  GS        gs_;
  R5900     ee_;
  R3000A    iop_;
  bool      running_;

  uint32_t scanline_cycle_acc_;
  uint16_t current_scanline_;
};

}  // namespace PS2Emulator
