#include "emulator/saturn/SH2.h"
#include "emulator/saturn/SaturnMemory.h"

namespace SaturnEmulator {

SH2::SH2(SaturnMemory* memory, bool is_slave)
    : pc_(0), pr_(0), sr_(0xF0), gbr_(0), vbr_(0), mach_(0), macl_(0),
      delay_slot_(false), delay_target_(0),
      halted_(false), is_slave_(is_slave),
      memory_(memory) {
  r_.fill(0);
}

void SH2::Reset() {
  r_.fill(0);
  // Boot: fetch PC from VBR + 0 (vector table at 0x00000000)
  // Real Saturn master SH-2 resets to vector[1] at 0x00000004
  // For scaffold, use a safe RAM address
  pc_     = 0x06002000U;   // Work RAM-H entry stub
  pr_     = 0;
  sr_     = 0x000000F0U;   // I-mask = 15 (all interrupts disabled)
  gbr_    = 0;
  vbr_    = 0;
  mach_   = 0;
  macl_   = 0;
  halted_ = false;
  delay_slot_ = false;
}

uint32_t SH2::Step() {
  if (halted_) return 1;

  uint32_t exec_pc = pc_;

  if (delay_slot_) {
    pc_ = delay_target_;
    delay_slot_ = false;
  } else {
    pc_ += 2;
  }

  uint16_t op = Read16(exec_pc);
  return Execute(op);
}

uint8_t SH2::Read8(uint32_t addr) {
  return memory_->Read8(addr);
}

uint16_t SH2::Read16(uint32_t addr) {
  return memory_->Read16(addr);
}

uint32_t SH2::Read32(uint32_t addr) {
  return memory_->Read32(addr);
}

void SH2::Write8(uint32_t addr, uint8_t val) {
  memory_->Write8(addr, val);
}

void SH2::Write16(uint32_t addr, uint16_t val) {
  memory_->Write16(addr, val);
}

void SH2::Write32(uint32_t addr, uint32_t val) {
  memory_->Write32(addr, val);
}

uint32_t SH2::Execute(uint16_t op) {
  if (op == 0x0009) return 1;  // NOP

  uint8_t n = (op >> 8) & 0xF;   // Destination register
  uint8_t m = (op >> 4) & 0xF;   // Source register
  uint8_t hi = (op >> 12) & 0xF; // Top nibble — opcode group

  switch (hi) {
    case 0x0: {
      uint8_t lo = op & 0xFF;
      if (lo == 0x02) { r_[n] = gbr_; return 1; }      // STC GBR, Rn (simplified)
      if (lo == 0x0B) { /* RTS */ delay_slot_ = true; delay_target_ = pr_; return 2; }
      if (lo == 0x2B) { /* RTE */ delay_slot_ = true; delay_target_ = pr_; return 4; }
      return 1;
    }

    case 0x1: {  // MOV.L Rm, @(disp, Rn)
      uint8_t disp = op & 0xF;
      Write32(r_[n] + (disp << 2), r_[m]);
      return 1;
    }

    case 0x2: {
      uint8_t lo = op & 0xF;
      if (lo == 0x0) { Write8(r_[n], r_[m] & 0xFF); return 1; }  // MOV.B Rm, @Rn
      if (lo == 0x1) { Write16(r_[n], r_[m] & 0xFFFF); return 1; }  // MOV.W Rm, @Rn
      if (lo == 0x2) { Write32(r_[n], r_[m]); return 1; }  // MOV.L Rm, @Rn
      if (lo == 0x4) { r_[n] -= 1; Write8(r_[n], r_[m] & 0xFF); return 1; }  // MOV.B Rm, @-Rn
      if (lo == 0x5) { r_[n] -= 2; Write16(r_[n], r_[m] & 0xFFFF); return 1; }  // MOV.W
      if (lo == 0x6) { r_[n] -= 4; Write32(r_[n], r_[m]); return 1; }  // MOV.L
      if (lo == 0xA) {  // XOR Rm, Rn
        r_[n] ^= r_[m];
        return 1;
      }
      if (lo == 0xB) {  // OR Rm, Rn
        r_[n] |= r_[m];
        return 1;
      }
      return 1;
    }

    case 0x3: {
      uint8_t lo = op & 0xF;
      if (lo == 0xC) {  // ADD Rm, Rn
        r_[n] += r_[m];
        return 1;
      }
      if (lo == 0x8) {  // SUB Rm, Rn
        r_[n] -= r_[m];
        return 1;
      }
      return 1;
    }

    case 0x4: {
      uint8_t lo = op & 0xFF;
      if (lo == 0x0B) {  // JSR @Rn
        pr_ = pc_ + 2;
        delay_slot_ = true;
        delay_target_ = r_[n];
        return 2;
      }
      if (lo == 0x2B) {  // JMP @Rn
        delay_slot_ = true;
        delay_target_ = r_[n];
        return 2;
      }
      if ((op & 0xFF) == 0x10) {  // DT Rn
        r_[n]--;
        sr_ = (r_[n] == 0) ? (sr_ | 1) : (sr_ & ~1);
        return 1;
      }
      return 1;
    }

    case 0x5: {  // MOV.L @(disp, Rm), Rn
      uint8_t disp = op & 0xF;
      r_[n] = Read32(r_[m] + (disp << 2));
      return 1;
    }

    case 0x6: {
      uint8_t lo = op & 0xF;
      if (lo == 0x0) { r_[n] = static_cast<int32_t>(static_cast<int8_t>(Read8(r_[m]))); return 1; }   // MOV.B
      if (lo == 0x1) { r_[n] = static_cast<int32_t>(static_cast<int16_t>(Read16(r_[m]))); return 1; } // MOV.W
      if (lo == 0x2) { r_[n] = Read32(r_[m]); return 1; }  // MOV.L @Rm, Rn
      if (lo == 0x3) { r_[n] = r_[m]; return 1; }          // MOV Rm, Rn
      if (lo == 0x4) { r_[n] = static_cast<int32_t>(static_cast<int8_t>(Read8(r_[m]))); r_[m] += 1; return 1; }   // MOV.B @Rm+
      if (lo == 0x5) { r_[n] = static_cast<int32_t>(static_cast<int16_t>(Read16(r_[m]))); r_[m] += 2; return 1; } // MOV.W @Rm+
      if (lo == 0x6) { r_[n] = Read32(r_[m]); r_[m] += 4; return 1; }  // MOV.L @Rm+, Rn
      if (lo == 0x7) { r_[n] = ~r_[m]; return 1; }  // NOT
      return 1;
    }

    case 0x7: {  // ADD #imm, Rn
      int8_t imm = static_cast<int8_t>(op & 0xFF);
      r_[n] += static_cast<int32_t>(imm);
      return 1;
    }

    case 0x8: {
      uint8_t sub = (op >> 8) & 0xF;
      int8_t disp = static_cast<int8_t>(op & 0xFF);
      if (sub == 0x9) {  // BT disp (branch if T=1)
        if (sr_ & 1) {
          delay_slot_  = true;
          delay_target_ = pc_ + (static_cast<int32_t>(disp) << 1);
        }
        return 1;
      }
      if (sub == 0xB) {  // BF disp (branch if T=0)
        if (!(sr_ & 1)) {
          delay_slot_  = true;
          delay_target_ = pc_ + (static_cast<int32_t>(disp) << 1);
        }
        return 1;
      }
      return 1;
    }

    case 0x9: {  // MOV.W @(disp, PC), Rn
      uint8_t disp = op & 0xFF;
      r_[n] = static_cast<int32_t>(static_cast<int16_t>(Read16(pc_ + (disp << 1))));
      return 1;
    }

    case 0xA: {  // BRA disp
      int32_t disp = static_cast<int32_t>((op & 0xFFF) | ((op & 0x800) ? 0xFFFFF000 : 0));
      delay_slot_  = true;
      delay_target_ = pc_ + (disp << 1);
      return 2;
    }

    case 0xB: {  // BSR disp
      int32_t disp = static_cast<int32_t>((op & 0xFFF) | ((op & 0x800) ? 0xFFFFF000 : 0));
      pr_ = pc_ + 2;
      delay_slot_  = true;
      delay_target_ = pc_ + (disp << 1);
      return 2;
    }

    case 0xD: {  // MOV.L @(disp, PC), Rn
      uint8_t disp = op & 0xFF;
      r_[n] = Read32((pc_ & ~3) + (disp << 2));
      return 1;
    }

    case 0xE: {  // MOV #imm, Rn
      int8_t imm = static_cast<int8_t>(op & 0xFF);
      r_[n] = static_cast<int32_t>(imm);
      return 1;
    }

    default:
      return 1;  // Unknown — NOP for scaffold
  }
}

SH2::State SH2::SaveState() const {
  return State{
    .r      = r_,
    .pc     = pc_,
    .pr     = pr_,
    .sr     = sr_,
    .gbr    = gbr_,
    .vbr    = vbr_,
    .mach   = mach_,
    .macl   = macl_,
    .halted = halted_,
  };
}

void SH2::LoadState(const State& state) {
  r_      = state.r;
  pc_     = state.pc;
  pr_     = state.pr;
  sr_     = state.sr;
  gbr_    = state.gbr;
  vbr_    = state.vbr;
  mach_   = state.mach;
  macl_   = state.macl;
  halted_ = state.halted;
  delay_slot_ = false;
}

}  // namespace SaturnEmulator
