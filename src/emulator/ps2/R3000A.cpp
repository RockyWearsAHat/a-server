#include "emulator/ps2/R3000A.h"
#include "emulator/ps2/PS2Memory.h"

namespace PS2Emulator {

R3000A::R3000A(PS2Memory* memory)
    : gpr_{}, pc_(0), hi_(0), lo_(0),
      delay_slot_(false), delay_target_(0),
      halted_(false), memory_(memory) {}

void R3000A::Reset() {
    gpr_.fill(0);
    // IOP boots at BIOS mirror in KSEG1
    pc_          = 0xBFC00000U;
    hi_ = lo_    = 0;
    delay_slot_  = false;
    delay_target_= 0;
    halted_      = false;
}

uint32_t R3000A::Read32(uint32_t addr) {
    return memory_->IopRead32(addr & 0x1FFFFFFFU);
}

void R3000A::Write32(uint32_t addr, uint32_t val) {
    memory_->IopWrite32(addr & 0x1FFFFFFFU, val);
}

uint32_t R3000A::Execute(uint32_t op) {
    if (halted_) return 1;

    const uint8_t  primary = (op >> 26) & 0x3F;
    const uint8_t  rs      = (op >> 21) & 0x1F;
    const uint8_t  rt      = (op >> 16) & 0x1F;
    const uint8_t  rd      = (op >> 11) & 0x1F;
    const uint8_t  shamt   = (op >>  6) & 0x1F;
    const uint8_t  fn      =  op        & 0x3F;
    const int16_t  simm    = static_cast<int16_t>(op & 0xFFFF);
    const uint32_t target  = op & 0x3FFFFFFU;

    if (primary == 0x00) {
        // SPECIAL
        switch (fn) {
            case 0x00: if (rd) gpr_[rd] = gpr_[rt] << shamt; break;  // SLL
            case 0x02: if (rd) gpr_[rd] = gpr_[rt] >> shamt; break;  // SRL
            case 0x08:  // JR
                delay_target_ = gpr_[rs]; delay_slot_ = true; break;
            case 0x21:  // ADDU
                if (rd) gpr_[rd] = gpr_[rs] + gpr_[rt]; break;
            case 0x24:  // AND
                if (rd) gpr_[rd] = gpr_[rs] & gpr_[rt]; break;
            case 0x25:  // OR
                if (rd) gpr_[rd] = gpr_[rs] | gpr_[rt]; break;
            case 0x2A:  // SLT
                if (rd) gpr_[rd] = (static_cast<int32_t>(gpr_[rs]) < static_cast<int32_t>(gpr_[rt])) ? 1U : 0U; break;
            default: break;
        }
        pc_ += 4;
        return 1;
    }

    switch (primary) {
        case 0x02:  // J
            delay_target_ = ((pc_ + 4) & 0xF0000000U) | (target << 2);
            delay_slot_   = true;
            break;
        case 0x03:  // JAL
            gpr_[31]      = pc_ + 8;
            delay_target_ = ((pc_ + 4) & 0xF0000000U) | (target << 2);
            delay_slot_   = true;
            break;
        case 0x04:  // BEQ
            if (gpr_[rs] == gpr_[rt]) { delay_target_ = pc_ + 4 + (static_cast<int32_t>(simm) << 2); delay_slot_ = true; }
            break;
        case 0x05:  // BNE
            if (gpr_[rs] != gpr_[rt]) { delay_target_ = pc_ + 4 + (static_cast<int32_t>(simm) << 2); delay_slot_ = true; }
            break;
        case 0x09:  // ADDIU
            if (rt) gpr_[rt] = gpr_[rs] + static_cast<uint32_t>(static_cast<int32_t>(simm)); break;
        case 0x0D:  // ORI
            if (rt) gpr_[rt] = gpr_[rs] | static_cast<uint16_t>(op); break;
        case 0x0F:  // LUI
            if (rt) gpr_[rt] = static_cast<uint32_t>(static_cast<uint16_t>(op)) << 16; break;
        case 0x23:  // LW
            if (rt) gpr_[rt] = Read32(gpr_[rs] + static_cast<uint32_t>(static_cast<int32_t>(simm))); break;
        case 0x2B:  // SW
            Write32(gpr_[rs] + static_cast<uint32_t>(static_cast<int32_t>(simm)), gpr_[rt]); break;
        default: break;
    }
    pc_ += 4;
    return 1;
}

uint32_t R3000A::Step() {
    if (halted_) return 1;

    if (delay_slot_) {
        delay_slot_ = false;
        uint32_t slot_op = Read32(pc_);
        Execute(slot_op);
        pc_ = delay_target_;
        return 1;
    }
    return Execute(Read32(pc_));
}

R3000A::State R3000A::SaveState() const {
    return {gpr_, pc_, hi_, lo_, halted_};
}

void R3000A::LoadState(const State& state) {
    gpr_    = state.gpr;
    pc_     = state.pc;
    hi_     = state.hi;
    lo_     = state.lo;
    halted_ = state.halted;
}

}  // namespace PS2Emulator
