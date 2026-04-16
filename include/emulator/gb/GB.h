#pragma once

#include "emulator/gb/LR35902.h"
#include "emulator/gb/GBPPU.h"
#include "emulator/gb/GBMemory.h"
#include "emulator/gb/GBCartridge.h"
#include "emulator/gb/GBAPU.h"
#include <span>
#include <string>

namespace GBEmulator {

// Game Boy system emulator
class GB {
 public:
  GB();
  ~GB() = default;

  // Load ROM and reset to start state
  void Load(const std::string& rom_path);
  void Load(std::span<const uint8_t> rom_data);

  // Execute one CPU instruction; returns cycle count
  uint32_t Step();

  // Run until next frame boundary
  void RunFrame();

  // System state
  uint32_t GetFrameCount() const { return ppu_.GetFrameCount(); }
  const uint32_t* GetFramebuffer() const { return ppu_.GetFramebuffer(); }
  bool IsRunning() const { return running_; }

  // Peripheral accessors
  LR35902* GetCPU() { return &cpu_; }
  GBPPU* GetPPU() { return &ppu_; }
  GBMemory* GetMemory() { return &memory_; }
  GBCartridge* GetCartridge() { return &cartridge_; }
  GBAPU* GetAPU() { return &apu_; }

  // System reset
  void Reset();

  // State save/restore (cascades through all subsystems)
  struct State {
    LR35902::State cpu;
    GBPPU::State ppu;
    GBMemory::State memory;
    GBCartridge::State cartridge;
    GBAPU::State apu;
    bool running;
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  GBCartridge cartridge_;
  GBMemory memory_;
  GBPPU ppu_;
  LR35902 cpu_;
  GBAPU apu_;
  bool running_;

  uint32_t last_frame_count_;  // For RunFrame() termination
};

}  // namespace GBEmulator
