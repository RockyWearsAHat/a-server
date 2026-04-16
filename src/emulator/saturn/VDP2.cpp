#include "emulator/saturn/VDP2.h"
#include "emulator/saturn/SaturnMemory.h"

namespace SaturnEmulator {

VDP2::VDP2(SaturnMemory* memory)
    : frame_count_(0), memory_(memory) {
  vram_.resize(0x80000, 0);   // 512 KB
  cram_.resize(0x1000, 0);    // 4 KB colour RAM
  regs_.resize(128, 0);
}

void VDP2::Reset() {
  std::fill(vram_.begin(), vram_.end(), 0);
  std::fill(cram_.begin(), cram_.end(), 0);
  std::fill(regs_.begin(), regs_.end(), 0);
  frame_count_ = 0;
}

void VDP2::TickScanline(uint16_t y) {
  // VBlank starts at line 224 (NTSC)
  if (y == 224) {
    IncrementFrame();
  }
}

uint16_t VDP2::ReadReg(uint32_t reg_offset) {
  uint32_t idx = (reg_offset >> 1) & 0x7F;
  return regs_[idx];
}

void VDP2::WriteReg(uint32_t reg_offset, uint16_t val) {
  uint32_t idx = (reg_offset >> 1) & 0x7F;
  regs_[idx] = val;
}

uint16_t VDP2::ReadVram(uint32_t offset) {
  offset &= 0x7FFFE;
  return (vram_[offset] << 8) | vram_[offset + 1];
}

void VDP2::WriteVram(uint32_t offset, uint16_t val) {
  offset &= 0x7FFFE;
  vram_[offset]     = (val >> 8) & 0xFF;
  vram_[offset + 1] = val & 0xFF;
}

uint16_t VDP2::ReadCram(uint32_t offset) {
  offset &= 0xFFE;
  return (cram_[offset] << 8) | cram_[offset + 1];
}

void VDP2::WriteCram(uint32_t offset, uint16_t val) {
  offset &= 0xFFE;
  cram_[offset]     = (val >> 8) & 0xFF;
  cram_[offset + 1] = val & 0xFF;
}

VDP2::State VDP2::SaveState() const {
  return State{
    .vram        = vram_,
    .cram        = cram_,
    .regs        = regs_,
    .frame_count = frame_count_,
  };
}

void VDP2::LoadState(const State& state) {
  vram_        = state.vram;
  cram_        = state.cram;
  regs_        = state.regs;
  frame_count_ = state.frame_count;
}

}  // namespace SaturnEmulator
