#pragma once

#include <cstdint>
#include <vector>

namespace GameCubeEmulator {

class Flipper;

// GameCubeMemory — Gecko BAT-decoded physical address space dispatcher.
// Main RAM (24 MB at 0x00000000), Flipper registers (0xCC000000),
// GP FIFO (0x08000000, write-only).
class GameCubeMemory {
 public:
  GameCubeMemory();
  ~GameCubeMemory() = default;

  void Init(Flipper* flipper);

  uint8_t  Read8 (uint32_t addr);
  uint16_t Read16(uint32_t addr);
  uint32_t Read32(uint32_t addr);
  void Write8 (uint32_t addr, uint8_t  val);
  void Write16(uint32_t addr, uint16_t val);
  void Write32(uint32_t addr, uint32_t val);

  uint8_t* GetRamPtr() { return ram_.data(); }

  struct State {
    std::vector<uint8_t> ram;
  };

  State SaveState() const;
  void  LoadState(const State& state);

 private:
  std::vector<uint8_t> ram_;   // 24 MB main RAM
  Flipper* flipper_;
};

}  // namespace GameCubeEmulator
