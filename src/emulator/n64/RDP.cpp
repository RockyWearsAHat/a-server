#include "emulator/n64/RDP.h"
#include "emulator/n64/N64Memory.h"

namespace N64Emulator {

RDP::RDP(N64Memory* memory)
    : frame_count_(0),
      memory_(memory) {
  framebuffer_.resize(kFramebufferWidth * kFramebufferHeight, 0xFF000000U);
}

void RDP::Reset() {
  frame_count_ = 0;
  std::fill(framebuffer_.begin(), framebuffer_.end(), 0xFF000000U);
}

void RDP::ProcessCommand(uint64_t cmd_word) {
  // Command type in bits 61:56
  uint8_t cmd = static_cast<uint8_t>((cmd_word >> 56) & 0x3F);
  switch (cmd) {
    case 0x27:  // SYNC_FULL — marks end of frame in real hardware
      IncrementFrame();
      break;
    default:
      break;  // Scaffold: other commands are NOP
  }
}

uint32_t RDP::ReadReg(uint32_t reg_offset) {
  // Minimal PI registers for scaffold
  return 0;
}

void RDP::WriteReg(uint32_t reg_offset, uint32_t val) {
  // Scaffold: ignore writes
}

RDP::State RDP::SaveState() const {
  return State{
    .frame_count = frame_count_,
    .framebuffer = framebuffer_,
  };
}

void RDP::LoadState(const State& state) {
  frame_count_ = state.frame_count;
  framebuffer_ = state.framebuffer;
}

}  // namespace N64Emulator
