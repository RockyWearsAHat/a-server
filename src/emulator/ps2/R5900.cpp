#include "emulator/ps2/R5900.h"
#include "emulator/ps2/PS2Memory.h"

namespace PS2Emulator {

R5900::R5900(PS2Memory* memory)
    : gpr_{}, pc_(0), hi_(0), lo_(0), cp0_{},
      delay_slot_(false), delay_target_(0),
      memory_(memory) {}

void R5900::Reset() {
    gpr_.fill(0);
    cp0_.fill(0);
    // EE boot vector: BIOS ROM mapped at 0xBFC00000 (KSEG1, physical 0x1FC00000)
    pc_          = 0xBFC00000U;
    hi_ = lo_    = 0;
    delay_slot_  = false;
    delay_target_= 0;
}

uint8_t R5900::Read8(uint32_t addr) {
    return memory_->Read8(addr & 0x1FFFFFFFU);
}

uint16_t R5900::Read16(uint32_t addr) {
    return memory_->Read16(addr & 0x1FFFFFFFU);
}

uint32_t R5900::Read32(uint32_t addr) {
    return memory_->Read32(addr & 0x1FFFFFFFU);
}

void R5900::Write32(uint32_t addr, uint32_t val) {
    memory_->Write32(addr & 0x1FFFFFFFU, val);
}

uint32_t R5900::ExecuteSpecial(uint32_t op) {
    const uint8_t rs   = (op >> 21) & 0x1F;
    const uint8_t rt   = (op >> 16) & 0x1F;
    const uint8_t rd   = (op >> 11) & 0x1F;
    const uint8_t shamt= (op >>  6) & 0x1F;
    const uint8_t fn   =  op        & 0x3F;

    switch (fn) {
        case 0x00:  // SLL
            if (rd) gpr_[rd] = static_cast<uint32_t>(gpr_[rt]) << shamt;
            break;
        case 0x02:  // SRL
            if (rd) gpr_[rd] = static_cast<uint32_t>(gpr_[rt]) >> shamt;
            break;
        case 0x03:  // SRA
            if (rd) gpr_[rd] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<uint32_t>(gpr_[rt])) >> shamt);
            break;
        case 0x08:  // JR
            delay_target_ = static_cast<uint32_t>(gpr_[rs]);
            delay_slot_   = true;
            break;
        case 0x09:  // JALR
            if (rd) gpr_[rd] = pc_ + 8;
            delay_target_   = static_cast<uint32_t>(gpr_[rs]);
            delay_slot_     = true;
            break;
        case 0x10:  // MFHI
            if (rd) gpr_[rd] = hi_;
            break;
        case 0x12:  // MFLO
            if (rd) gpr_[rd] = lo_;
            break;
        case 0x18: {// MULT
            int64_t res = static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(gpr_[rs]))) *
                          static_cast<int64_t>(static_cast<int32_t>(static_cast<uint32_t>(gpr_[rt])));
            lo_ = static_cast<uint32_t>(res);
            hi_ = static_cast<uint32_t>(res >> 32);
            break;
        }
        case 0x1A: {// DIV
            int32_t a = static_cast<int32_t>(static_cast<uint32_t>(gpr_[rs]));
            int32_t b = static_cast<int32_t>(static_cast<uint32_t>(gpr_[rt]));
            if (b != 0) { lo_ = static_cast<uint32_t>(a / b); hi_ = static_cast<uint32_t>(a % b); }
            break;
        }
        case 0x20:  // ADD (treat as ADDU for scaffold)
        case 0x21:  // ADDU
            if (rd) gpr_[rd] = static_cast<uint32_t>(gpr_[rs]) + static_cast<uint32_t>(gpr_[rt]);
            break;
        case 0x22:  // SUB (treat as SUBU)
        case 0x23:  // SUBU
            if (rd) gpr_[rd] = static_cast<uint32_t>(gpr_[rs]) - static_cast<uint32_t>(gpr_[rt]);
            break;
        case 0x24:  // AND
            if (rd) gpr_[rd] = gpr_[rs] & gpr_[rt];
            break;
        case 0x25:  // OR
            if (rd) gpr_[rd] = gpr_[rs] | gpr_[rt];
            break;
        case 0x26:  // XOR
            if (rd) gpr_[rd] = gpr_[rs] ^ gpr_[rt];
            break;
        case 0x2A:  // SLT
            if (rd) gpr_[rd] = (static_cast<int64_t>(gpr_[rs]) < static_cast<int64_t>(gpr_[rt])) ? 1 : 0;
            break;
        case 0x2B:  // SLTU
            if (rd) gpr_[rd] = (gpr_[rs] < gpr_[rt]) ? 1 : 0;
            break;
        default:
            break;
    }
    pc_ += 4;
    return 1;
}

uint32_t R5900::ExecuteRegImm(uint32_t op) {
    const uint8_t  rs    = (op >> 21) & 0x1F;
    const uint8_t  sub   = (op >> 16) & 0x1F;
    const int16_t  simm  = static_cast<int16_t>(op & 0xFFFF);

    if (sub == 0x00) {  // BLTZ
        if (static_cast<int64_t>(gpr_[rs]) < 0) {
            delay_target_ = pc_ + 4 + (static_cast<int32_t>(simm) << 2);
            delay_slot_   = true;
        }
    } else if (sub == 0x01) {  // BGEZ
        if (static_cast<int64_t>(gpr_[rs]) >= 0) {
            delay_target_ = pc_ + 4 + (static_cast<int32_t>(simm) << 2);
            delay_slot_   = true;
        }
    }
    pc_ += 4;
    return 1;
}

uint32_t R5900::ExecuteCop0(uint32_t op) {
    const uint8_t sub = (op >> 21) & 0x1F;
    const uint8_t rt  = (op >> 16) & 0x1F;
    const uint8_t rd  = (op >> 11) & 0x1F;

    if (sub == 0x00) {       // MFC0
        if (rt) gpr_[rt] = cp0_[rd];
    } else if (sub == 0x04) { // MTC0
        cp0_[rd] = static_cast<uint32_t>(gpr_[rt]);
    }
    pc_ += 4;
    return 1;
}

uint32_t R5900::Execute(uint32_t op) {
    const uint8_t  primary = (op >> 26) & 0x3F;
    const uint8_t  rs      = (op >> 21) & 0x1F;
    const uint8_t  rt      = (op >> 16) & 0x1F;
    const int16_t  simm    = static_cast<int16_t>(op & 0xFFFF);
    const uint16_t uimm    = op & 0xFFFF;
    const uint32_t target  = op & 0x3FFFFFFU;

    switch (primary) {
        case 0x00: return ExecuteSpecial(op);
        case 0x01: return ExecuteRegImm(op);
        case 0x02:  // J
            delay_target_ = ((pc_ + 4) & 0xF0000000U) | (target << 2);
            delay_slot_   = true;
            pc_ += 4;
            return 1;
        case 0x03:  // JAL
            gpr_[31]      = pc_ + 8;
            delay_target_ = ((pc_ + 4) & 0xF0000000U) | (target << 2);
            delay_slot_   = true;
            pc_ += 4;
            return 1;
        case 0x04:  // BEQ
            if (gpr_[rs] == gpr_[rt]) {
                delay_target_ = pc_ + 4 + (static_cast<int32_t>(simm) << 2);
                delay_slot_   = true;
            }
            pc_ += 4;
            return 1;
        case 0x05:  // BNE
            if (gpr_[rs] != gpr_[rt]) {
                delay_target_ = pc_ + 4 + (static_cast<int32_t>(simm) << 2);
                delay_slot_   = true;
            }
            pc_ += 4;
            return 1;
        case 0x06:  // BLEZ
            if (static_cast<int64_t>(gpr_[rs]) <= 0) {
                delay_target_ = pc_ + 4 + (static_cast<int32_t>(simm) << 2);
                delay_slot_   = true;
            }
            pc_ += 4;
            return 1;
        case 0x07:  // BGTZ
            if (static_cast<int64_t>(gpr_[rs]) > 0) {
                delay_target_ = pc_ + 4 + (static_cast<int32_t>(simm) << 2);
                delay_slot_   = true;
            }
            pc_ += 4;
            return 1;
        case 0x08:  // ADDI (treat as ADDIU)
        case 0x09:  // ADDIU
            if (rt) gpr_[rt] = static_cast<uint32_t>(static_cast<uint32_t>(gpr_[rs]) + static_cast<uint32_t>(static_cast<int32_t>(simm)));
            pc_ += 4;
            return 1;
        case 0x0A:  // SLTI
            if (rt) gpr_[rt] = (static_cast<int64_t>(gpr_[rs]) < static_cast<int32_t>(simm)) ? 1 : 0;
            pc_ += 4;
            return 1;
        case 0x0B:  // SLTIU
            if (rt) gpr_[rt] = (gpr_[rs] < static_cast<uint32_t>(static_cast<int32_t>(simm))) ? 1 : 0;
            pc_ += 4;
            return 1;
        case 0x0C:  // ANDI
            if (rt) gpr_[rt] = gpr_[rs] & uimm;
            pc_ += 4;
            return 1;
        case 0x0D:  // ORI
            if (rt) gpr_[rt] = gpr_[rs] | uimm;
            pc_ += 4;
            return 1;
        case 0x0E:  // XORI
            if (rt) gpr_[rt] = gpr_[rs] ^ uimm;
            pc_ += 4;
            return 1;
        case 0x0F:  // LUI
            if (rt) gpr_[rt] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<uint32_t>(uimm) << 16));
            pc_ += 4;
            return 1;
        case 0x10: return ExecuteCop0(op);
        case 0x23:  // LW
            if (rt) gpr_[rt] = static_cast<int32_t>(Read32(static_cast<uint32_t>(gpr_[rs]) + static_cast<uint32_t>(static_cast<int32_t>(simm))));
            pc_ += 4;
            return 1;
        case 0x2B:  // SW
            Write32(static_cast<uint32_t>(gpr_[rs]) + static_cast<uint32_t>(static_cast<int32_t>(simm)),
                    static_cast<uint32_t>(gpr_[rt]));
            pc_ += 4;
            return 1;
        default:
            pc_ += 4;
            return 1;
    }
}

uint32_t R5900::Step() {
    if (delay_slot_) {
        delay_slot_ = false;
        uint32_t slot_op = Read32(pc_);
        Execute(slot_op);
        pc_ = delay_target_;
        return 1;
    }
    uint32_t op = Read32(pc_);
    return Execute(op);
}

R5900::State R5900::SaveState() const {
    return {gpr_, pc_, hi_, lo_, cp0_};
}

void R5900::LoadState(const State& state) {
    gpr_ = state.gpr;
    pc_  = state.pc;
    hi_  = state.hi;
    lo_  = state.lo;
    cp0_ = state.cp0;
}

}  // namespace PS2Emulator
