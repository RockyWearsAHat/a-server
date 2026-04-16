#include "emulator/n64/RSP.h"
#include "emulator/n64/N64Memory.h"

namespace N64Emulator {

RSP::RSP(N64Memory* memory)
    : pc_(0),
      halted_(true),
      memory_(memory) {
  dmem_.resize(0x1000);
  imem_.resize(0x1000);
  gpr_.fill(0);
}

void RSP::Reset() {
  pc_ = 0;
  halted_ = true;
  gpr_.fill(0);
  std::fill(dmem_.begin(), dmem_.end(), 0);
  std::fill(imem_.begin(), imem_.end(), 0);
}

uint32_t RSP::Step() {
  if (halted_) return 1;

  // Fetch instruction from IMEM
  uint32_t pc = pc_ & 0xFFC;  // 12-bit, word-aligned
  if (pc + 3 >= imem_.size()) return 1;

  uint32_t instr = (imem_[pc] << 24) | (imem_[pc+1] << 16) | (imem_[pc+2] << 8) | imem_[pc+3];
  pc_ = (pc_ + 4) & 0xFFF;

  return Execute(instr);
}

uint32_t RSP::Execute(uint32_t instr) {
  if (instr == 0) return 1;  // NOP

  uint8_t opcode = (instr >> 26) & 0x3F;
  switch (opcode) {
    case 0x00: {  // SPECIAL (SLL, etc.)
      uint8_t fn = instr & 0x3F;
      uint8_t rt = (instr >> 16) & 0x1F;
      uint8_t rd = (instr >> 11) & 0x1F;
      uint8_t sa = (instr >> 6) & 0x1F;
      if (fn == 0x00) gpr_[rd] = gpr_[rt] << sa;  // SLL
      return 1;
    }
    case 0x08: {  // J target (halt via jump-to-self)
      halted_ = true;
      return 1;
    }
    default:
      return 1;  // NOP for unimplemented
  }
}

RSP::State RSP::SaveState() const {
  return State{
    .dmem = dmem_,
    .imem = imem_,
    .pc = pc_,
    .gpr = gpr_,
    .halted = halted_,
  };
}

void RSP::LoadState(const State& state) {
  dmem_ = state.dmem;
  imem_ = state.imem;
  pc_ = state.pc;
  gpr_ = state.gpr;
  halted_ = state.halted;
}

}  // namespace N64Emulator
