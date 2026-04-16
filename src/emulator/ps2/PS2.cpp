#include "emulator/ps2/PS2.h"
#include "emulator/ps2/PS2Constants.h"

namespace PS2Emulator {

PS2::PS2()
    : memory_(),
      gs_(&memory_),
      ee_(&memory_),
      iop_(&memory_),
      running_(false),
      scanline_cycle_acc_(0),
      current_scanline_(0) {
    memory_.Init(&gs_);
}

void PS2::Load(const std::string& /*disc_path*/) {
    Reset();
}

void PS2::Reset() {
    ee_.Reset();
    iop_.Reset();
    gs_.Reset();
    scanline_cycle_acc_ = 0;
    current_scanline_   = 0;
    running_            = true;
}

uint32_t PS2::Step() {
    uint32_t ee_cycles = ee_.Step();

    // IOP runs at 1/8 EE clock — approximate with every 8th EE step
    static uint8_t iop_divider = 0;
    if (++iop_divider >= 8) { iop_divider = 0; iop_.Step(); }

    scanline_cycle_acc_ += ee_cycles;
    if (scanline_cycle_acc_ >= kCyclesPerLine) {
        scanline_cycle_acc_ -= kCyclesPerLine;
        current_scanline_++;
        if (current_scanline_ >= kScanlinesNtsc) {
            current_scanline_ = 0;
            gs_.IncrementFrame();
        }
    }
    return ee_cycles;
}

void PS2::RunFrame() {
    uint32_t start = gs_.GetFrameCount();
    while (gs_.GetFrameCount() == start) {
        Step();
    }
}

PS2::State PS2::SaveState() const {
    return {ee_.SaveState(), iop_.SaveState(), gs_.SaveState(), memory_.SaveState(), running_};
}

void PS2::LoadState(const State& state) {
    ee_.LoadState(state.ee);
    iop_.LoadState(state.iop);
    gs_.LoadState(state.gs);
    memory_.LoadState(state.memory);
    running_ = state.running;
}

}  // namespace PS2Emulator
