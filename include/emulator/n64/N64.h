#pragma once

#include "emulator/n64/R4300i.h"
#include "emulator/n64/RSP.h"
#include "emulator/n64/RDP.h"
#include "emulator/n64/N64Memory.h"
#include "emulator/n64/N64Cartridge.h"
#include <string>

namespace N64Emulator {

// N64 top-level system emulator
class N64 {
 public:
  N64();
  ~N64() = default;

  // Load ROM and reset
  void Load(const std::string& rom_path);
  void Reset();

  // Execution
  uint32_t Step();      // One CPU instruction
  void RunFrame();      // Run until next VI frame

  // Video output
  uint32_t GetFrameCount() const { return rdp_.GetFrameCount(); }
  const uint32_t* GetFramebuffer() const { return rdp_.GetFramebuffer(); }

  // Peripheral accessors
  R4300i*      GetCPU()       { return &cpu_; }
  RSP*         GetRSP()       { return &rsp_; }
  RDP*         GetRDP()       { return &rdp_; }
  N64Memory*   GetMemory()    { return &memory_; }
  N64Cartridge* GetCartridge() { return &cartridge_; }

  // State save/restore
  struct State {
    R4300i::State cpu;
    RSP::State    rsp;
    RDP::State    rdp;
    N64Memory::State memory;
    N64Cartridge::State cartridge;
    bool running;
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  N64Cartridge cartridge_;
  N64Memory    memory_;
  RDP          rdp_;
  RSP          rsp_;
  R4300i       cpu_;
  bool         running_;
  uint32_t     last_frame_count_;

  // VI counter: increments to trigger frame completion
  uint32_t vi_cycle_acc_;
};

}  // namespace N64Emulator
