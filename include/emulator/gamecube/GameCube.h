#pragma once

#include "emulator/gamecube/Gecko.h"
#include "emulator/gamecube/Flipper.h"
#include "emulator/gamecube/GameCubeMemory.h"
#include <string>

namespace GameCubeEmulator {

class GameCube {
 public:
  GameCube();
  ~GameCube() = default;

  void Load(const std::string& disc_path);
  void Reset();

  uint32_t Step();
  void RunFrame();

  uint32_t GetFrameCount()         const { return flipper_.GetFrameCount(); }
  const uint32_t* GetFramebuffer() const { return flipper_.GetFramebuffer(); }

  Gecko*          GetCPU()    { return &cpu_; }
  Flipper*        GetFlipper(){ return &flipper_; }
  GameCubeMemory* GetMemory() { return &memory_; }

  struct State {
    Gecko::State          cpu;
    Flipper::State        flipper;
    GameCubeMemory::State memory;
    bool running;
  };

  State SaveState() const;
  void  LoadState(const State& state);

 private:
  GameCubeMemory memory_;
  Flipper        flipper_;
  Gecko          cpu_;
  bool           running_;

  uint32_t scanline_cycle_acc_;
  uint16_t current_scanline_;
};

}  // namespace GameCubeEmulator
