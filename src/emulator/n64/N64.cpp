#include "emulator/n64/N64.h"
#include "emulator/n64/N64Constants.h"

namespace N64Emulator {

N64::N64()
    : rdp_(&memory_),
      rsp_(&memory_),
      cpu_(&memory_),
      running_(false),
      last_frame_count_(0),
      vi_cycle_acc_(0) {
  memory_.Init(&cartridge_, &rdp_, &rsp_);
}

void N64::Load(const std::string& rom_path) {
  cartridge_.Load(rom_path);
  Reset();
}

void N64::Reset() {
  cpu_.Reset();
  rsp_.Reset();
  rdp_.Reset();
  running_ = true;
  last_frame_count_ = rdp_.GetFrameCount();
  vi_cycle_acc_ = 0;
}

uint32_t N64::Step() {
  if (!running_) return 0;

  uint32_t cpu_cycles = cpu_.Step();

  // Tick RSP (runs at same clock as RCP, which is 2/3 of CPU clock)
  if (!rsp_.IsHalted()) {
    rsp_.Step();
  }

  // VI frame counter: one frame every ~525 lines × 3093 cycles = ~1,623,825 cycles
  vi_cycle_acc_ += cpu_cycles;
  if (vi_cycle_acc_ >= (kViLinesPerFrame * kViCyclesPerLine)) {
    vi_cycle_acc_ = 0;
    rdp_.IncrementFrame();
  }

  return cpu_cycles;
}

void N64::RunFrame() {
  last_frame_count_ = rdp_.GetFrameCount();
  while (rdp_.GetFrameCount() == last_frame_count_ && running_) {
    Step();
  }
}

N64::State N64::SaveState() const {
  return State{
    .cpu       = cpu_.SaveState(),
    .rsp       = rsp_.SaveState(),
    .rdp       = rdp_.SaveState(),
    .memory    = memory_.SaveState(),
    .cartridge = cartridge_.SaveState(),
    .running   = running_,
  };
}

void N64::LoadState(const State& state) {
  cpu_.LoadState(state.cpu);
  rsp_.LoadState(state.rsp);
  rdp_.LoadState(state.rdp);
  memory_.LoadState(state.memory);
  cartridge_.LoadState(state.cartridge);
  running_ = state.running;
  last_frame_count_ = rdp_.GetFrameCount();
  vi_cycle_acc_ = 0;
}

}  // namespace N64Emulator
