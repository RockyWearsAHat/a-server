#pragma once

#include "emulator/saturn/SH2.h"
#include "emulator/saturn/VDP1.h"
#include "emulator/saturn/VDP2.h"
#include "emulator/saturn/SaturnMemory.h"
#include <string>

namespace SaturnEmulator {

// Saturn top-level system
class Saturn {
 public:
  Saturn();
  ~Saturn() = default;

  void Load(const std::string& disc_path);
  void Reset();

  uint32_t Step();     // One master SH-2 instruction
  void RunFrame();     // Run until next VBlank

  // Video output
  uint32_t GetFrameCount()   const { return vdp2_.GetFrameCount(); }
  const uint32_t* GetFramebuffer() const { return vdp1_.GetFramebuffer(); }

  // Peripheral accessors
  SH2*          GetMasterSH2() { return &master_; }
  SH2*          GetSlaveSH2()  { return &slave_; }
  VDP1*         GetVDP1()      { return &vdp1_; }
  VDP2*         GetVDP2()      { return &vdp2_; }
  SaturnMemory* GetMemory()    { return &memory_; }

  // State
  struct State {
    SH2::State         master;
    SH2::State         slave;
    VDP1::State        vdp1;
    VDP2::State        vdp2;
    SaturnMemory::State memory;
    bool running;
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  SaturnMemory memory_;
  VDP1         vdp1_;
  VDP2         vdp2_;
  SH2          master_;
  SH2          slave_;
  bool         running_;

  uint32_t scanline_cycle_acc_;
  uint16_t current_scanline_;
};

}  // namespace SaturnEmulator
