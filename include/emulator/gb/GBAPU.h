#pragma once

#include <cstdint>
#include <vector>

namespace GBEmulator {

class GBMemory;

// Game Boy Audio Processing Unit - 4 channels (2 square, 1 triangle, 1 noise)
class GBAPU {
 public:
  explicit GBAPU(GBMemory* memory);
  ~GBAPU() = default;

  // Tick APU by one master cycle
  void Tick();

  // Audio register I/O (0xFF10–0xFF3F)
  uint8_t ReadReg(uint8_t reg_offset);
  void WriteReg(uint8_t reg_offset, uint8_t val);

  // State save/restore
  struct State {
    uint32_t cycle_accumulator;
    uint8_t nr10, nr11, nr12, nr13, nr14;  // CH1 pulse (square)
    uint8_t nr20, nr21, nr22, nr23, nr24;  // CH2 pulse (square)
    uint8_t nr30, nr31, nr32, nr33, nr34;  // CH3 wave (triangle)
    uint8_t nr40, nr41, nr42, nr43, nr44;  // CH4 noise
    uint8_t nr50, nr51, nr52;              // Control registers
    std::vector<uint8_t> wave_ram;         // 16 bytes wave pattern
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  uint32_t cycle_accumulator_;

  // Channel 1: Pulse with sweep and envelope
  uint8_t nr10_, nr11_, nr12_, nr13_, nr14_;
  
  // Channel 2: Pulse with envelope
  uint8_t nr20_, nr21_, nr22_, nr23_, nr24_;
  
  // Channel 3: Wave (wavetable)
  uint8_t nr30_, nr31_, nr32_, nr33_, nr34_;
  std::vector<uint8_t> wave_ram_;  // 16 bytes @ 0xFF30–0xFF3F
  
  // Channel 4: Noise
  uint8_t nr40_, nr41_, nr42_, nr43_, nr44_;

  // Control/volume
  uint8_t nr50_, nr51_, nr52_;  // Master volume, output select, enable

  GBMemory* memory_;  // Unowned reference
};

}  // namespace GBEmulator
