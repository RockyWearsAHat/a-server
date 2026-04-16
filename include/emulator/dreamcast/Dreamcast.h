#pragma once

#include "emulator/dreamcast/SH4.h"
#include "emulator/dreamcast/PowerVR2.h"
#include "emulator/dreamcast/DreamcastMemory.h"
#include <string>

namespace DreamcastEmulator {

// Dreamcast top-level system
class Dreamcast {
 public:
  Dreamcast();
  ~Dreamcast() = default;

  void Load(const std::string& disc_path);
  void Reset();

  uint32_t Step();
  void RunFrame();

  // Video output
  uint32_t GetFrameCount()         const { return pvr_.GetFrameCount(); }
  const uint32_t* GetFramebuffer() const { return pvr_.GetFramebuffer(); }

  // Peripheral accessors
  SH4*             GetCPU()    { return &cpu_; }
  PowerVR2*        GetPVR()    { return &pvr_; }
  DreamcastMemory* GetMemory() { return &memory_; }

  // State
  struct State {
    SH4::State             cpu;
    PowerVR2::State        pvr;
    DreamcastMemory::State memory;
    bool running;
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  DreamcastMemory memory_;
  PowerVR2        pvr_;
  SH4             cpu_;
  bool            running_;

  uint32_t scanline_cycle_acc_;
  uint16_t current_scanline_;
};

}  // namespace DreamcastEmulator
