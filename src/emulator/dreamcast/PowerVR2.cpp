#include "emulator/dreamcast/PowerVR2.h"
#include "emulator/dreamcast/DreamcastConstants.h"

namespace DreamcastEmulator {

PowerVR2::PowerVR2(DreamcastMemory* memory)
    : vram_(kVramSize, 0),
      framebuffer_(kWidth * kHeight, 0xFF000000U),
      regs_(256, 0),
      frame_count_(0),
      memory_(memory) {}

void PowerVR2::Reset() {
    vram_.assign(kVramSize, 0);
    framebuffer_.assign(kWidth * kHeight, 0xFF000000U);
    regs_.assign(256, 0);
    frame_count_ = 0;
}

void PowerVR2::WriteTA(uint32_t /*addr_offset*/, uint32_t /*val*/) {
    // TA FIFO stub — tile accelerator receives polygon lists here
}

void PowerVR2::Render() {
    // Tile-based deferred render pass stub
    // Real implementation: traverse tile lists in VRAM, rasterise, write framebuffer
    frame_count_++;
}

uint32_t PowerVR2::ReadReg(uint32_t reg_offset) {
    uint32_t idx = (reg_offset >> 2) & 0xFFU;
    return regs_[idx];
}

void PowerVR2::WriteReg(uint32_t reg_offset, uint32_t val) {
    uint32_t idx = (reg_offset >> 2) & 0xFFU;
    regs_[idx] = val;
}

uint8_t PowerVR2::ReadVram8(uint32_t offset) {
    if (offset >= kVramSize) return 0xFF;
    return vram_[offset];
}

void PowerVR2::WriteVram8(uint32_t offset, uint8_t val) {
    if (offset < kVramSize) vram_[offset] = val;
}

uint32_t PowerVR2::ReadVram32(uint32_t offset) {
    if (offset + 3 >= kVramSize) return 0xFFFFFFFFU;
    return (static_cast<uint32_t>(vram_[offset])     |
           (static_cast<uint32_t>(vram_[offset + 1]) << 8)  |
           (static_cast<uint32_t>(vram_[offset + 2]) << 16) |
           (static_cast<uint32_t>(vram_[offset + 3]) << 24));
}

void PowerVR2::WriteVram32(uint32_t offset, uint32_t val) {
    if (offset + 3 >= kVramSize) return;
    vram_[offset]     = static_cast<uint8_t>(val);
    vram_[offset + 1] = static_cast<uint8_t>(val >> 8);
    vram_[offset + 2] = static_cast<uint8_t>(val >> 16);
    vram_[offset + 3] = static_cast<uint8_t>(val >> 24);
}

PowerVR2::State PowerVR2::SaveState() const {
    return {vram_, framebuffer_, regs_, frame_count_};
}

void PowerVR2::LoadState(const State& state) {
    vram_        = state.vram;
    framebuffer_ = state.framebuffer;
    regs_        = state.regs;
    frame_count_ = state.frame_count;
}

}  // namespace DreamcastEmulator
