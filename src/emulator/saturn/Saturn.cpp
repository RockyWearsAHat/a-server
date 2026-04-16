#include "emulator/saturn/Saturn.h"
#include "emulator/saturn/SaturnConstants.h"

namespace SaturnEmulator {

Saturn::Saturn()
    : vdp1_(&memory_), vdp2_(&memory_),
      master_(&memory_, false), slave_(&memory_, true),
      running_(false),
      scanline_cycle_acc_(0), current_scanline_(0) {
  memory_.Init(&vdp1_, &vdp2_);
}

void Saturn::Load(const std::string& disc_path) {
  // Scaffold: no actual CD-ROM loading
  (void)disc_path;
  Reset();
}

void Saturn::Reset() {
  master_.Reset();
  slave_.Reset();
  vdp1_.Reset();
  vdp2_.Reset();
  running_ = true;
  scanline_cycle_acc_ = 0;
  current_scanline_ = 0;
}

uint32_t Saturn::Step() {
  if (!running_) return 0;

  uint32_t cycles = master_.Step();

  // Accumulate cycles and tick scanlines
  scanline_cycle_acc_ += cycles;
  if (scanline_cycle_acc_ >= kCyclesPerScanline) {
    scanline_cycle_acc_ -= kCyclesPerScanline;
    vdp2_.TickScanline(current_scanline_);
    current_scanline_++;
    if (current_scanline_ >= kScanlines) {
      current_scanline_ = 0;
      vdp1_.RenderFrame();
    }
  }

  return cycles;
}

void Saturn::RunFrame() {
  uint32_t start_frame = GetFrameCount();
  while (GetFrameCount() == start_frame && running_) {
    Step();
  }
}

Saturn::State Saturn::SaveState() const {
  return State{
    .master  = master_.SaveState(),
    .slave   = slave_.SaveState(),
    .vdp1    = vdp1_.SaveState(),
    .vdp2    = vdp2_.SaveState(),
    .memory  = memory_.SaveState(),
    .running = running_,
  };
}

void Saturn::LoadState(const State& state) {
  master_.LoadState(state.master);
  slave_.LoadState(state.slave);
  vdp1_.LoadState(state.vdp1);
  vdp2_.LoadState(state.vdp2);
  memory_.LoadState(state.memory);
  running_ = state.running;
  scanline_cycle_acc_ = 0;
  current_scanline_   = 0;
}

}  // namespace SaturnEmulator
