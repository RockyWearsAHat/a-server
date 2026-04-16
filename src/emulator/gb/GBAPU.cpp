#include "emulator/gb/GBAPU.h"
#include "emulator/gb/GBMemory.h"

namespace GBEmulator {

GBAPU::GBAPU(GBMemory* memory)
    : cycle_accumulator_(0),
      nr10_(0), nr11_(0), nr12_(0), nr13_(0), nr14_(0),
      nr20_(0), nr21_(0), nr22_(0), nr23_(0), nr24_(0),
      nr30_(0), nr31_(0), nr32_(0), nr33_(0), nr34_(0),
      nr40_(0), nr41_(0), nr42_(0), nr43_(0), nr44_(0),
      nr50_(0), nr51_(0), nr52_(0),
      memory_(memory) {
  wave_ram_.resize(16);
}

void GBAPU::Tick() {
  cycle_accumulator_++;
  // Scaffold: no actual audio synthesis yet
  // Full implementation would sample/synthesize here
}

uint8_t GBAPU::ReadReg(uint8_t reg_offset) {
  switch (reg_offset & 0x0F) {
    case 0x00: return nr10_;
    case 0x01: return nr11_;
    case 0x02: return nr12_;
    case 0x03: return nr13_;
    case 0x04: return nr14_;
    case 0x05: return nr20_;
    case 0x06: return nr21_;
    case 0x07: return nr22_;
    case 0x08: return nr23_;
    case 0x09: return nr24_;
    case 0x0A: return nr30_;
    case 0x0B: return nr31_;
    case 0x0C: return nr32_;
    case 0x0D: return nr33_;
    case 0x0E: return nr34_;
    default: return 0xFF;
  }
}

void GBAPU::WriteReg(uint8_t reg_offset, uint8_t val) {
  switch (reg_offset & 0x0F) {
    case 0x00: nr10_ = val; break;
    case 0x01: nr11_ = val; break;
    case 0x02: nr12_ = val; break;
    case 0x03: nr13_ = val; break;
    case 0x04: nr14_ = val; break;
    case 0x05: nr20_ = val; break;
    case 0x06: nr21_ = val; break;
    case 0x07: nr22_ = val; break;
    case 0x08: nr23_ = val; break;
    case 0x09: nr24_ = val; break;
    case 0x0A: nr30_ = val; break;
    case 0x0B: nr31_ = val; break;
    case 0x0C: nr32_ = val; break;
    case 0x0D: nr33_ = val; break;
    case 0x0E: nr34_ = val; break;
  }
}

GBAPU::State GBAPU::SaveState() const {
  return State{
    .cycle_accumulator = cycle_accumulator_,
    .nr10 = nr10_, .nr11 = nr11_, .nr12 = nr12_, .nr13 = nr13_, .nr14 = nr14_,
    .nr20 = nr20_, .nr21 = nr21_, .nr22 = nr22_, .nr23 = nr23_, .nr24 = nr24_,
    .nr30 = nr30_, .nr31 = nr31_, .nr32 = nr32_, .nr33 = nr33_, .nr34 = nr34_,
    .nr40 = nr40_, .nr41 = nr41_, .nr42 = nr42_, .nr43 = nr43_, .nr44 = nr44_,
    .nr50 = nr50_, .nr51 = nr51_, .nr52 = nr52_,
    .wave_ram = wave_ram_,
  };
}

void GBAPU::LoadState(const State& state) {
  cycle_accumulator_ = state.cycle_accumulator;
  nr10_ = state.nr10;
  nr11_ = state.nr11;
  nr12_ = state.nr12;
  nr13_ = state.nr13;
  nr14_ = state.nr14;
  nr20_ = state.nr20;
  nr21_ = state.nr21;
  nr22_ = state.nr22;
  nr23_ = state.nr23;
  nr24_ = state.nr24;
  nr30_ = state.nr30;
  nr31_ = state.nr31;
  nr32_ = state.nr32;
  nr33_ = state.nr33;
  nr34_ = state.nr34;
  nr40_ = state.nr40;
  nr41_ = state.nr41;
  nr42_ = state.nr42;
  nr43_ = state.nr43;
  nr44_ = state.nr44;
  nr50_ = state.nr50;
  nr51_ = state.nr51;
  nr52_ = state.nr52;
  wave_ram_ = state.wave_ram;
}

}  // namespace GBEmulator
