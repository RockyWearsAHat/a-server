#include "emulator/ps2/GS.h"
#include "emulator/ps2/PS2Constants.h"

namespace PS2Emulator {

GS::GS(PS2Memory* memory)
    : vram_(kGsVramSize, 0),
      framebuffer_(kWidth * kHeight, 0xFF000000U),
      priv_regs_(64, 0),
      frame_count_(0),
      memory_(memory) {}

void GS::Reset() {
    vram_.assign(kGsVramSize, 0);
    framebuffer_.assign(kWidth * kHeight, 0xFF000000U);
    priv_regs_.assign(64, 0);
    frame_count_ = 0;
}

void GS::WriteGifPacket(uint64_t /*data*/, uint64_t /*addr*/) {
    // GIF A+D packet stub — real impl would dispatch to GS register set
}

uint64_t GS::ReadPrivReg(uint32_t offset) {
    uint32_t idx = (offset >> 3) & 0x3FU;
    return priv_regs_[idx];
}

void GS::WritePrivReg(uint32_t offset, uint64_t val) {
    uint32_t idx = (offset >> 3) & 0x3FU;
    priv_regs_[idx] = val;
}

uint8_t GS::ReadVram8(uint32_t offset) {
    if (offset >= kGsVramSize) return 0xFF;
    return vram_[offset];
}

void GS::WriteVram8(uint32_t offset, uint8_t val) {
    if (offset < kGsVramSize) vram_[offset] = val;
}

GS::State GS::SaveState() const {
    return {vram_, framebuffer_, priv_regs_, frame_count_};
}

void GS::LoadState(const State& state) {
    vram_        = state.vram;
    framebuffer_ = state.framebuffer;
    priv_regs_   = state.priv_regs;
    frame_count_ = state.frame_count;
}

}  // namespace PS2Emulator
