#include "emulator/dreamcast/Dreamcast.h"
#include "emulator/dreamcast/DreamcastConstants.h"

namespace DreamcastEmulator {

Dreamcast::Dreamcast()
    : memory_(),
      pvr_(&memory_),
      cpu_(&memory_),
      running_(false),
      scanline_cycle_acc_(0),
      current_scanline_(0) {
    memory_.Init(&pvr_);
}

void Dreamcast::Load(const std::string& /*disc_path*/) {
    // GD-ROM scaffold — load & parse GDI/BIN+CUE would go here
    Reset();
}

void Dreamcast::Reset() {
    cpu_.Reset();
    pvr_.Reset();
    scanline_cycle_acc_ = 0;
    current_scanline_   = 0;
    running_            = true;
}

uint32_t Dreamcast::Step() {
    uint32_t cycles = cpu_.Step();
    scanline_cycle_acc_ += cycles;
    if (scanline_cycle_acc_ >= kCyclesPerScanline) {
        scanline_cycle_acc_ -= kCyclesPerScanline;
        current_scanline_++;
        if (current_scanline_ >= kScanlinesPerFrame) {
            current_scanline_ = 0;
            pvr_.IncrementFrame();
        }
    }
    return cycles;
}

void Dreamcast::RunFrame() {
    uint32_t start = pvr_.GetFrameCount();
    while (pvr_.GetFrameCount() == start) {
        Step();
    }
}

Dreamcast::State Dreamcast::SaveState() const {
    return {cpu_.SaveState(), pvr_.SaveState(), memory_.SaveState(), running_};
}

void Dreamcast::LoadState(const State& state) {
    cpu_.LoadState(state.cpu);
    pvr_.LoadState(state.pvr);
    memory_.LoadState(state.memory);
    running_ = state.running;
}

}  // namespace DreamcastEmulator
