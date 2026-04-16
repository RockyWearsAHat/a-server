#include "emulator/n64/R4300i.h"
#include "emulator/n64/N64Memory.h"

namespace N64Emulator {

R4300i::R4300i(N64Memory* memory)
    : pc_(0x00000000ULL),
      hi_(0), lo_(0),
      ll_bit_(false),
      branch_delay_(false),
      branch_target_(0),
      memory_(memory) {
  gpr_.fill(0);
  cp0_.fill(0);
}

void R4300i::Reset() {
  gpr_.fill(0);
  pc_ = 0xFFFFFFFF80000000ULL;  // BFC00000 physical — boot ROM entry
  hi_ = lo_ = 0;
  ll_bit_ = false;
  branch_delay_ = false;

  // CP0 reset: status register, cause, etc.
  cp0_[12] = 0x34000000U;  // Status: kernel mode, interrupts disabled
  cp0_[15] = 0x00000B00U;  // PRId: R4300i implementation
}

uint32_t R4300i::Step() {
  uint64_t exec_pc = pc_;

  if (branch_delay_) {
    pc_ = branch_target_;
    branch_delay_ = false;
  } else {
    pc_ += 4;
  }

  uint32_t instr = Read32(exec_pc);
  return Execute(instr);
}

R4300i::State R4300i::SaveState() const {
  return State{
    .gpr = gpr_,
    .pc = pc_,
    .hi = hi_,
    .lo = lo_,
    .cp0 = cp0_,
    .ll_bit = ll_bit_,
  };
}

void R4300i::LoadState(const State& state) {
  gpr_ = state.gpr;
  pc_ = state.pc;
  hi_ = state.hi;
  lo_ = state.lo;
  cp0_ = state.cp0;
  ll_bit_ = state.ll_bit;
  branch_delay_ = false;
}

uint32_t R4300i::VirtToPhys(uint64_t vaddr) {
  // KUSEG/KSEG0/KSEG1 simple translation (no TLB for scaffold)
  if ((vaddr & 0xFFFFFFFF00000000ULL) == 0xFFFFFFFF00000000ULL) {
    uint32_t addr32 = static_cast<uint32_t>(vaddr & 0xFFFFFFFF);
    if (addr32 >= 0x80000000U && addr32 < 0xA0000000U) {
      return addr32 & 0x1FFFFFFFU;  // KSEG0
    }
    if (addr32 >= 0xA0000000U && addr32 < 0xC0000000U) {
      return addr32 & 0x1FFFFFFFU;  // KSEG1
    }
    return addr32 & 0x1FFFFFFFU;   // Other — direct map
  }
  return static_cast<uint32_t>(vaddr & 0x1FFFFFFFU);
}

uint8_t R4300i::Read8(uint64_t vaddr) {
  return memory_->PhysRead8(VirtToPhys(vaddr));
}

uint16_t R4300i::Read16(uint64_t vaddr) {
  return memory_->PhysRead16(VirtToPhys(vaddr));
}

uint32_t R4300i::Read32(uint64_t vaddr) {
  return memory_->PhysRead32(VirtToPhys(vaddr));
}

uint64_t R4300i::Read64(uint64_t vaddr) {
  uint32_t hi = Read32(vaddr);
  uint32_t lo = Read32(vaddr + 4);
  return (static_cast<uint64_t>(hi) << 32) | lo;
}

void R4300i::Write8(uint64_t vaddr, uint8_t val) {
  memory_->PhysWrite8(VirtToPhys(vaddr), val);
}

void R4300i::Write16(uint64_t vaddr, uint16_t val) {
  memory_->PhysWrite16(VirtToPhys(vaddr), val);
}

void R4300i::Write32(uint64_t vaddr, uint32_t val) {
  memory_->PhysWrite32(VirtToPhys(vaddr), val);
}

void R4300i::Write64(uint64_t vaddr, uint64_t val) {
  Write32(vaddr,     static_cast<uint32_t>(val >> 32));
  Write32(vaddr + 4, static_cast<uint32_t>(val & 0xFFFFFFFF));
}

uint32_t R4300i::Execute(uint32_t instr) {
  if (instr == 0) return 1;  // NOP (SLL r0, r0, 0)

  uint8_t opcode = (instr >> 26) & 0x3F;

  switch (opcode) {
    case 0x00:  // SPECIAL
      ExecuteSpecial(instr);
      return 1;

    case 0x01:  // REGIMM
      ExecuteRegImm(instr);
      return 1;

    case 0x02: {  // J target
      uint32_t target = (instr & 0x03FFFFFF) << 2;
      uint64_t base = pc_ & 0xFFFFFFFFF0000000ULL;
      branch_delay_ = true;
      branch_target_ = base | target;
      return 1;
    }

    case 0x03: {  // JAL
      uint32_t target = (instr & 0x03FFFFFF) << 2;
      uint64_t base = pc_ & 0xFFFFFFFFF0000000ULL;
      gpr_[31] = pc_ + 4;  // Return address
      branch_delay_ = true;
      branch_target_ = base | target;
      return 1;
    }

    case 0x04: {  // BEQ rs, rt, offset
      uint8_t rs = (instr >> 21) & 0x1F;
      uint8_t rt = (instr >> 16) & 0x1F;
      if (gpr_[rs] == gpr_[rt]) {
        int16_t offset = static_cast<int16_t>(instr & 0xFFFF);
        branch_delay_ = true;
        branch_target_ = pc_ + (static_cast<int64_t>(offset) << 2);
      }
      return 1;
    }

    case 0x05: {  // BNE rs, rt, offset
      uint8_t rs = (instr >> 21) & 0x1F;
      uint8_t rt = (instr >> 16) & 0x1F;
      if (gpr_[rs] != gpr_[rt]) {
        int16_t offset = static_cast<int16_t>(instr & 0xFFFF);
        branch_delay_ = true;
        branch_target_ = pc_ + (static_cast<int64_t>(offset) << 2);
      }
      return 1;
    }

    case 0x08: {  // ADDI rt, rs, imm
      uint8_t rs = (instr >> 21) & 0x1F;
      uint8_t rt = (instr >> 16) & 0x1F;
      int32_t imm = static_cast<int16_t>(instr & 0xFFFF);
      gpr_[rt] = static_cast<int64_t>(static_cast<int32_t>(gpr_[rs]) + imm);
      return 1;
    }

    case 0x09: {  // ADDIU rt, rs, imm
      uint8_t rs = (instr >> 21) & 0x1F;
      uint8_t rt = (instr >> 16) & 0x1F;
      int32_t imm = static_cast<int16_t>(instr & 0xFFFF);
      gpr_[rt] = static_cast<int64_t>(static_cast<int32_t>(gpr_[rs]) + imm);
      return 1;
    }

    case 0x0F: {  // LUI rt, imm
      uint8_t rt = (instr >> 16) & 0x1F;
      int32_t imm = static_cast<int16_t>(instr & 0xFFFF);
      gpr_[rt] = static_cast<int64_t>(imm << 16);
      return 1;
    }

    case 0x10:  // COP0
      ExecuteCop0(instr);
      return 1;

    case 0x23: {  // LW rt, offset(base)
      uint8_t base = (instr >> 21) & 0x1F;
      uint8_t rt   = (instr >> 16) & 0x1F;
      int16_t off  = static_cast<int16_t>(instr & 0xFFFF);
      uint64_t addr = gpr_[base] + off;
      gpr_[rt] = static_cast<int64_t>(static_cast<int32_t>(Read32(addr)));
      return 1;
    }

    case 0x2B: {  // SW rt, offset(base)
      uint8_t base = (instr >> 21) & 0x1F;
      uint8_t rt   = (instr >> 16) & 0x1F;
      int16_t off  = static_cast<int16_t>(instr & 0xFFFF);
      uint64_t addr = gpr_[base] + off;
      Write32(addr, static_cast<uint32_t>(gpr_[rt] & 0xFFFFFFFF));
      return 1;
    }

    default:
      return 1;  // Unknown opcode — treat as NOP for scaffold
  }
}

void R4300i::ExecuteSpecial(uint32_t instr) {
  uint8_t fn = instr & 0x3F;
  uint8_t rs = (instr >> 21) & 0x1F;
  uint8_t rt = (instr >> 16) & 0x1F;
  uint8_t rd = (instr >> 11) & 0x1F;

  switch (fn) {
    case 0x00: {  // SLL rd, rt, sa
      uint8_t sa = (instr >> 6) & 0x1F;
      gpr_[rd] = static_cast<int64_t>(static_cast<int32_t>(gpr_[rt]) << sa);
      break;
    }
    case 0x21: {  // ADDU rd, rs, rt
      gpr_[rd] = static_cast<int64_t>(
          static_cast<int32_t>(gpr_[rs]) + static_cast<int32_t>(gpr_[rt]));
      break;
    }
    case 0x24: {  // AND rd, rs, rt
      gpr_[rd] = gpr_[rs] & gpr_[rt];
      break;
    }
    case 0x25: {  // OR rd, rs, rt
      gpr_[rd] = gpr_[rs] | gpr_[rt];
      break;
    }
    case 0x08: {  // JR rs
      branch_delay_ = true;
      branch_target_ = gpr_[rs];
      break;
    }
    case 0x09: {  // JALR rd, rs
      gpr_[rd] = pc_ + 4;
      branch_delay_ = true;
      branch_target_ = gpr_[rs];
      break;
    }
    case 0x18: {  // MULT rs, rt
      int64_t result = static_cast<int64_t>(static_cast<int32_t>(gpr_[rs]))
                     * static_cast<int64_t>(static_cast<int32_t>(gpr_[rt]));
      lo_ = static_cast<int64_t>(static_cast<int32_t>(result & 0xFFFFFFFF));
      hi_ = static_cast<int64_t>(static_cast<int32_t>(result >> 32));
      break;
    }
    case 0x1A: {  // DIV rs, rt
      if (gpr_[rt] != 0) {
        lo_ = static_cast<int64_t>(static_cast<int32_t>(gpr_[rs]) / static_cast<int32_t>(gpr_[rt]));
        hi_ = static_cast<int64_t>(static_cast<int32_t>(gpr_[rs]) % static_cast<int32_t>(gpr_[rt]));
      }
      break;
    }
    case 0x10: {  // MFHI rd
      gpr_[rd] = hi_;
      break;
    }
    case 0x12: {  // MFLO rd
      gpr_[rd] = lo_;
      break;
    }
    case 0x2A: {  // SLT rd, rs, rt
      gpr_[rd] = (static_cast<int64_t>(gpr_[rs]) < static_cast<int64_t>(gpr_[rt])) ? 1 : 0;
      break;
    }
    case 0x2B: {  // SLTU rd, rs, rt
      gpr_[rd] = (gpr_[rs] < gpr_[rt]) ? 1 : 0;
      break;
    }
    default:
      break;  // Unimplemented SPECIAL — NOP
  }
}

void R4300i::ExecuteRegImm(uint32_t instr) {
  uint8_t rs  = (instr >> 21) & 0x1F;
  uint8_t sub = (instr >> 16) & 0x1F;
  int16_t off = static_cast<int16_t>(instr & 0xFFFF);

  switch (sub) {
    case 0x00:  // BLTZ
      if (static_cast<int64_t>(gpr_[rs]) < 0) {
        branch_delay_  = true;
        branch_target_ = pc_ + (static_cast<int64_t>(off) << 2);
      }
      break;
    case 0x01:  // BGEZ
      if (static_cast<int64_t>(gpr_[rs]) >= 0) {
        branch_delay_  = true;
        branch_target_ = pc_ + (static_cast<int64_t>(off) << 2);
      }
      break;
    default:
      break;
  }
}

void R4300i::ExecuteCop0(uint32_t instr) {
  uint8_t sub = (instr >> 21) & 0x1F;
  uint8_t rt  = (instr >> 16) & 0x1F;
  uint8_t rd  = (instr >> 11) & 0x1F;

  switch (sub) {
    case 0x00:  // MFC0 — move from coprocessor 0
      gpr_[rt] = static_cast<int64_t>(static_cast<int32_t>(cp0_[rd]));
      break;
    case 0x04:  // MTC0 — move to coprocessor 0
      cp0_[rd] = static_cast<uint32_t>(gpr_[rt] & 0xFFFFFFFF);
      break;
    default:
      break;
  }
}

}  // namespace N64Emulator
