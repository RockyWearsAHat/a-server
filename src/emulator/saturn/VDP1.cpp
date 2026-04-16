#include "emulator/saturn/VDP1.h"
#include "emulator/saturn/SaturnMemory.h"

namespace SaturnEmulator {

VDP1::VDP1(SaturnMemory* memory)
    : memory_(memory) {
  vram_.resize(0x80000, 0);           // 512 KB
  framebuffer_.resize(kWidth * kHeight, 0xFF000000U);
  regs_.resize(8, 0);
}

void VDP1::Reset() {
  std::fill(vram_.begin(), vram_.end(), 0);
  std::fill(framebuffer_.begin(), framebuffer_.end(), 0xFF000000U);
  std::fill(regs_.begin(), regs_.end(), 0);
}

void VDP1::RenderFrame() {
  // Scaffold — output solid black each frame
  std::fill(framebuffer_.begin(), framebuffer_.end(), 0xFF000000U);
}

uint16_t VDP1::ReadReg(uint32_t reg_offset) {
  uint32_t idx = (reg_offset >> 1) & 0x7;
  return regs_[idx];
}

void VDP1::WriteReg(uint32_t reg_offset, uint16_t val) {
  uint32_t idx = (reg_offset >> 1) & 0x7;
  regs_[idx] = val;
}

uint16_t VDP1::ReadVram(uint32_t offset) {
  offset &= 0x7FFFE;
  return (vram_[offset] << 8) | vram_[offset + 1];
}

void VDP1::WriteVram(uint32_t offset, uint16_t val) {
  offset &= 0x7FFFE;
  vram_[offset]     = (val >> 8) & 0xFF;
  vram_[offset + 1] = val & 0xFF;
}

VDP1::State VDP1::SaveState() const {
  return State{
    .vram        = vram_,
    .framebuffer = framebuffer_,
    .regs        = regs_,
  };
}

void VDP1::LoadState(const State& state) {
  vram_        = state.vram;
  framebuffer_ = state.framebuffer;
  regs_        = state.regs;
}

}  // namespace SaturnEmulator
