#include "emulator/gamecube/Flipper.h"
#include "emulator/gamecube/GameCubeMemory.h"
#include "emulator/gamecube/GameCubeConstants.h"

namespace GameCubeEmulator {

Flipper::Flipper(GameCubeMemory* memory)
    : memory_(memory)
    , efb_(kEfbWidth * kEfbHeight, 0xFF000000U)
    , regs_(256, 0)
    , frame_count_(0) {}

void Flipper::Reset() {
    std::fill(efb_.begin(), efb_.end(), 0xFF000000U);
    std::fill(regs_.begin(), regs_.end(), 0U);
    frame_count_ = 0;
}

void Flipper::WriteFifo(uint32_t /*val*/) {
    // Stub — full GP FIFO decoding not implemented at scaffold level.
}

uint32_t Flipper::ReadReg(uint32_t reg_offset) {
    uint32_t idx = (reg_offset >> 2) & 0xFF;
    return regs_[idx];
}

void Flipper::WriteReg(uint32_t reg_offset, uint32_t val) {
    uint32_t idx = (reg_offset >> 2) & 0xFF;
    regs_[idx] = val;
}

Flipper::State Flipper::SaveState() const {
    return { efb_, regs_, frame_count_ };
}

void Flipper::LoadState(const State& s) {
    efb_         = s.efb;
    regs_        = s.regs;
    frame_count_ = s.frame_count;
}

}  // namespace GameCubeEmulator
