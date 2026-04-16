#include "emulator/gamecube/GameCube.h"
#include "emulator/gamecube/GameCubeConstants.h"

namespace GameCubeEmulator {

GameCube::GameCube()
    : memory_()
    , flipper_(&memory_)
    , cpu_(&memory_)
    , running_(false)
    , scanline_cycle_acc_(0)
    , current_scanline_(0) {
    memory_.Init(&flipper_);
}

void GameCube::Load(const std::string& /*disc_path*/) {
    // Disc loading stub — scaffold only.
    Reset();
}

void GameCube::Reset() {
    cpu_.Reset();
    flipper_.Reset();
    running_             = true;
    scanline_cycle_acc_  = 0;
    current_scanline_    = 0;
}

uint32_t GameCube::Step() {
    uint32_t cycles = cpu_.Step();
    scanline_cycle_acc_ += cycles;

    if (scanline_cycle_acc_ >= kCyclesPerLine) {
        scanline_cycle_acc_ -= kCyclesPerLine;
        current_scanline_++;

        if (current_scanline_ >= kScanlinesNtsc) {
            current_scanline_ = 0;
            flipper_.IncrementFrame();
        }
    }
    return cycles;
}

void GameCube::RunFrame() {
    uint32_t start_frame = flipper_.GetFrameCount();
    while (flipper_.GetFrameCount() == start_frame) {
        Step();
    }
}

GameCube::State GameCube::SaveState() const {
    return { cpu_.SaveState(), flipper_.SaveState(), memory_.SaveState(), running_ };
}

void GameCube::LoadState(const State& s) {
    cpu_.LoadState(s.cpu);
    flipper_.LoadState(s.flipper);
    memory_.LoadState(s.memory);
    running_             = s.running;
    scanline_cycle_acc_  = 0;
    current_scanline_    = 0;
}

}  // namespace GameCubeEmulator
