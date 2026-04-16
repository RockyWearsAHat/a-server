#include "emulator/dreamcast/SH4.h"
#include "emulator/dreamcast/DreamcastMemory.h"

namespace DreamcastEmulator {

SH4::SH4(DreamcastMemory* memory)
    : r_{}, r_bank_{}, pc_(0), pr_(0), sr_(0), gbr_(0), vbr_(0),
      mach_(0), macl_(0), delay_slot_(false), delay_target_(0),
      halted_(false), memory_(memory) {}

void SH4::Reset() {
    r_.fill(0);
    r_bank_.fill(0);
    // SH-4 boot vector in P2 uncached mirror of Area 0 (boot ROM)
    pc_   = 0xA0000000U;
    pr_   = 0;
    // SR: MD=1 (privileged), RB=1 (bank 1), BL=1 (block irq), I3-I0=0xF
    sr_   = 0x700000F0U;
    gbr_  = 0;
    vbr_  = 0;
    mach_ = 0;
    macl_ = 0;
    delay_slot_   = false;
    delay_target_ = 0;
    halted_       = false;
}

uint8_t SH4::Read8(uint32_t addr) {
    return memory_->Read8(addr & 0x1FFFFFFFU);
}

uint16_t SH4::Read16(uint32_t addr) {
    return memory_->Read16(addr & 0x1FFFFFFFU);
}

uint32_t SH4::Read32(uint32_t addr) {
    return memory_->Read32(addr & 0x1FFFFFFFU);
}

void SH4::Write8(uint32_t addr, uint8_t val) {
    memory_->Write8(addr & 0x1FFFFFFFU, val);
}

void SH4::Write16(uint32_t addr, uint16_t val) {
    memory_->Write16(addr & 0x1FFFFFFFU, val);
}

void SH4::Write32(uint32_t addr, uint32_t val) {
    memory_->Write32(addr & 0x1FFFFFFFU, val);
}

uint32_t SH4::Execute(uint16_t op) {
    const uint8_t  ni   = (op >> 12) & 0xF;  // primary nibble
    const uint8_t  n    = (op >> 8)  & 0xF;  // destination register nibble
    const uint8_t  m    = (op >> 4)  & 0xF;  // source register nibble
    const uint8_t  lo4  =  op        & 0xF;
    const uint8_t  imm8 =  op        & 0xFF;
    const int8_t   simm8 = static_cast<int8_t>(imm8);
    const uint16_t imm12 = op        & 0x0FFFU;

    switch (ni) {
        case 0x0: {
            if (op == 0x0009) {  // NOP
                pc_ += 2;
                return 1;
            }
            if ((op & 0xFF0F) == 0x0002) {  // STC SR/GBR/VBR -> Rn (scaffold: SR)
                if (m == 0) r_[n] = sr_;
                else if (m == 1) r_[n] = gbr_;
                else if (m == 2) r_[n] = vbr_;
                pc_ += 2;
                return 1;
            }
            if (op == 0x000B) {  // RTS
                delay_target_ = pr_;
                delay_slot_   = true;
                pc_ += 2;
                return 2;
            }
            if (op == 0x0028) {  // CLRMAC
                mach_ = macl_ = 0;
                pc_ += 2;
                return 1;
            }
            pc_ += 2;
            return 1;
        }
        case 0x1: {
            // MOV.L Rm, @(disp, Rn)
            uint8_t disp = op & 0xF;
            Write32(r_[n] + (disp << 2), r_[m]);
            pc_ += 2;
            return 1;
        }
        case 0x2: {
            switch (lo4) {
                case 0x0: Write8 (r_[n], static_cast<uint8_t>(r_[m])); break;  // MOV.B
                case 0x1: Write16(r_[n], static_cast<uint16_t>(r_[m])); break; // MOV.W
                case 0x2: Write32(r_[n], r_[m]); break;                        // MOV.L
                case 0x5: {
                    // MOV.W Rm, @-Rn (pre-dec)
                    r_[n] -= 2;
                    Write16(r_[n], static_cast<uint16_t>(r_[m]));
                    break;
                }
                case 0x6: {
                    // MOV.L Rm, @-Rn (pre-dec)
                    r_[n] -= 4;
                    Write32(r_[n], r_[m]);
                    break;
                }
                case 0x8: {
                    // TST Rm, Rn
                    uint32_t t = (r_[n] & r_[m]) == 0 ? 1U : 0U;
                    sr_ = (sr_ & ~0x1U) | t;
                    break;
                }
                case 0x9: {
                    // AND Rm, Rn
                    r_[n] &= r_[m];
                    break;
                }
                case 0xA: {
                    // XOR Rm, Rn
                    r_[n] ^= r_[m];
                    break;
                }
                case 0xB: {
                    // OR Rm, Rn
                    r_[n] |= r_[m];
                    break;
                }
                default: break;
            }
            pc_ += 2;
            return 1;
        }
        case 0x3: {
            switch (lo4) {
                case 0x0: {
                    // CMP/EQ Rm, Rn
                    uint32_t t = (r_[n] == r_[m]) ? 1U : 0U;
                    sr_ = (sr_ & ~0x1U) | t;
                    break;
                }
                case 0x2: {
                    // CMP/HS Rm, Rn  (unsigned >=)
                    uint32_t t = (r_[n] >= r_[m]) ? 1U : 0U;
                    sr_ = (sr_ & ~0x1U) | t;
                    break;
                }
                case 0x3: {
                    // CMP/GE Rm, Rn  (signed >=)
                    uint32_t t = (static_cast<int32_t>(r_[n]) >= static_cast<int32_t>(r_[m])) ? 1U : 0U;
                    sr_ = (sr_ & ~0x1U) | t;
                    break;
                }
                case 0x4: {
                    // DIV1 placeholder – SH-4 has 1-step divider; stub NOP
                    break;
                }
                case 0x8: {
                    // SUB Rm, Rn
                    r_[n] -= r_[m];
                    break;
                }
                case 0xC: {
                    // ADD Rm, Rn
                    r_[n] += r_[m];
                    break;
                }
                case 0xE: {
                    // ADDC Rm, Rn
                    uint64_t res = static_cast<uint64_t>(r_[n]) + r_[m] + (sr_ & 0x1U);
                    r_[n] = static_cast<uint32_t>(res);
                    sr_ = (sr_ & ~0x1U) | (res >> 32 ? 1U : 0U);
                    break;
                }
                default: break;
            }
            pc_ += 2;
            return 1;
        }
        case 0x4: {
            // Assorted shift/rotate/jump ops
            switch (op & 0x00FF) {
                case 0x00: {
                    // SHLL Rn
                    uint32_t t = (r_[n] >> 31) & 1U;
                    r_[n] <<= 1;
                    sr_ = (sr_ & ~0x1U) | t;
                    pc_ += 2;
                    return 1;
                }
                case 0x01: {
                    // SHLR Rn
                    uint32_t t = r_[n] & 1U;
                    r_[n] >>= 1;
                    sr_ = (sr_ & ~0x1U) | t;
                    pc_ += 2;
                    return 1;
                }
                case 0x08: {
                    // SHLL2 Rn
                    r_[n] <<= 2;
                    pc_ += 2;
                    return 1;
                }
                case 0x18: {
                    // SHLL8 Rn
                    r_[n] <<= 8;
                    pc_ += 2;
                    return 1;
                }
                case 0x28: {
                    // SHLL16 Rn
                    r_[n] <<= 16;
                    pc_ += 2;
                    return 1;
                }
                case 0x0B: {
                    // JSR @Rn  (delay slot)
                    pr_           = pc_ + 4;
                    delay_target_ = r_[n];
                    delay_slot_   = true;
                    pc_ += 2;
                    return 2;
                }
                case 0x2B: {
                    // JMP @Rn  (delay slot)
                    delay_target_ = r_[n];
                    delay_slot_   = true;
                    pc_ += 2;
                    return 2;
                }
                case 0x10: {
                    // DT Rn
                    r_[n]--;
                    uint32_t t = (r_[n] == 0) ? 1U : 0U;
                    sr_ = (sr_ & ~0x1U) | t;
                    pc_ += 2;
                    return 1;
                }
                default:
                    pc_ += 2;
                    return 1;
            }
        }
        case 0x5: {
            // MOV.L @(disp, Rm), Rn
            uint8_t disp = op & 0xF;
            r_[n] = Read32(r_[m] + (disp << 2));
            pc_ += 2;
            return 1;
        }
        case 0x6: {
            switch (lo4) {
                case 0x0: r_[n] = static_cast<int8_t>(Read8(r_[m]));   break; // MOV.B @Rm, Rn (signed ext)
                case 0x1: r_[n] = static_cast<int16_t>(Read16(r_[m])); break; // MOV.W @Rm, Rn
                case 0x2: r_[n] = Read32(r_[m]);                        break; // MOV.L @Rm, Rn
                case 0x3: r_[n] = r_[m];                                break; // MOV Rm, Rn
                case 0x4: {
                    // MOV.B @Rm+, Rn (post-inc)
                    r_[n] = static_cast<int8_t>(Read8(r_[m]));
                    if (n != m) r_[m]++;
                    break;
                }
                case 0x5: {
                    // MOV.W @Rm+, Rn
                    r_[n] = static_cast<int16_t>(Read16(r_[m]));
                    if (n != m) r_[m] += 2;
                    break;
                }
                case 0x6: {
                    // MOV.L @Rm+, Rn
                    r_[n] = Read32(r_[m]);
                    if (n != m) r_[m] += 4;
                    break;
                }
                case 0x7: {
                    // NOT Rm, Rn
                    r_[n] = ~r_[m];
                    break;
                }
                default: break;
            }
            pc_ += 2;
            return 1;
        }
        case 0x7: {
            // ADD #imm8, Rn
            r_[n] += static_cast<uint32_t>(simm8);
            pc_ += 2;
            return 1;
        }
        case 0x8: {
            // Branches & misc
            uint8_t sub = (op >> 8) & 0xF;
            if (sub == 0x8) {
                // CMP/EQ #imm8, R0
                uint32_t t = (r_[0] == static_cast<uint32_t>(static_cast<int8_t>(imm8))) ? 1U : 0U;
                sr_ = (sr_ & ~0x1U) | t;
                pc_ += 2;
                return 1;
            }
            if (sub == 0x9) {
                // BT label (branch if T==1)
                if (sr_ & 0x1U) {
                    pc_ += 4 + (static_cast<int8_t>(imm8) << 1);
                } else {
                    pc_ += 2;
                }
                return 1;
            }
            if (sub == 0xB) {
                // BF label (branch if T==0)
                if (!(sr_ & 0x1U)) {
                    pc_ += 4 + (static_cast<int8_t>(imm8) << 1);
                } else {
                    pc_ += 2;
                }
                return 1;
            }
            if (sub == 0xD) {
                // BT/S  (delay slot)
                if (sr_ & 0x1U) {
                    delay_target_ = pc_ + 4 + (static_cast<int8_t>(imm8) << 1);
                    delay_slot_   = true;
                }
                pc_ += 2;
                return 1;
            }
            if (sub == 0xF) {
                // BF/S  (delay slot)
                if (!(sr_ & 0x1U)) {
                    delay_target_ = pc_ + 4 + (static_cast<int8_t>(imm8) << 1);
                    delay_slot_   = true;
                }
                pc_ += 2;
                return 1;
            }
            pc_ += 2;
            return 1;
        }
        case 0x9: {
            // MOV.W @(disp, PC), Rn  (PC-relative word load)
            uint32_t eff = (pc_ & ~0x1U) + 4 + (imm8 << 1);
            r_[n] = static_cast<int16_t>(Read16(eff));
            pc_ += 2;
            return 1;
        }
        case 0xA: {
            // BRA label (delay slot)
            int32_t disp = static_cast<int32_t>((imm12 << 20)) >> 20;  // sign-extend 12→32
            delay_target_ = pc_ + 4 + (disp << 1);
            delay_slot_   = true;
            pc_ += 2;
            return 2;
        }
        case 0xB: {
            // BSR label (delay slot)
            int32_t disp = static_cast<int32_t>((imm12 << 20)) >> 20;
            pr_           = pc_ + 4;
            delay_target_ = pc_ + 4 + (disp << 1);
            delay_slot_   = true;
            pc_ += 2;
            return 2;
        }
        case 0xC: {
            uint8_t sub = (op >> 8) & 0xF;
            if (sub == 0x8) {
                // TST #imm8, R0
                uint32_t t = (r_[0] & imm8) == 0 ? 1U : 0U;
                sr_ = (sr_ & ~0x1U) | t;
                pc_ += 2;
                return 1;
            }
            if (sub == 0x9) {
                // AND #imm8, R0
                r_[0] &= imm8;
                pc_ += 2;
                return 1;
            }
            if (sub == 0xA) {
                // XOR #imm8, R0
                r_[0] ^= imm8;
                pc_ += 2;
                return 1;
            }
            if (sub == 0xB) {
                // OR #imm8, R0
                r_[0] |= imm8;
                pc_ += 2;
                return 1;
            }
            pc_ += 2;
            return 1;
        }
        case 0xD: {
            // MOV.L @(disp, PC), Rn  (PC-relative longword load)
            uint32_t eff = (pc_ & ~0x3U) + 4 + (imm8 << 2);
            r_[n] = Read32(eff);
            pc_ += 2;
            return 1;
        }
        case 0xE: {
            // MOV #imm8, Rn (sign extend)
            r_[n] = static_cast<uint32_t>(simm8);
            pc_ += 2;
            return 1;
        }
        default:
            pc_ += 2;
            return 1;
    }
}

uint32_t SH4::Step() {
    if (halted_) return 1;

    if (delay_slot_) {
        delay_slot_ = false;
        uint16_t op = Read16(pc_);
        Execute(op);  // execute delay slot instruction
        pc_ = delay_target_;
        return 1;
    }

    uint16_t op = Read16(pc_);
    return Execute(op);
}

SH4::State SH4::SaveState() const {
    State s;
    s.r      = r_;
    s.r_bank = r_bank_;
    s.pc     = pc_;
    s.pr     = pr_;
    s.sr     = sr_;
    s.gbr    = gbr_;
    s.vbr    = vbr_;
    s.mach   = mach_;
    s.macl   = macl_;
    s.halted = halted_;
    return s;
}

void SH4::LoadState(const State& state) {
    r_      = state.r;
    r_bank_ = state.r_bank;
    pc_     = state.pc;
    pr_     = state.pr;
    sr_     = state.sr;
    gbr_    = state.gbr;
    vbr_    = state.vbr;
    mach_   = state.mach;
    macl_   = state.macl;
    halted_ = state.halted;
}

}  // namespace DreamcastEmulator
