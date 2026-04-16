#include "emulator/gamecube/Gecko.h"
#include "emulator/gamecube/GameCubeMemory.h"
#include "emulator/gamecube/GameCubeConstants.h"

namespace GameCubeEmulator {

namespace {
// Sign-extend a 26-bit branch target to 32 bits
inline int32_t SignExt26(uint32_t x) {
    return static_cast<int32_t>(x << 6) >> 6;
}
// Sign-extend 16-bit immediate
inline int32_t SignExt16(uint32_t x) {
    return static_cast<int32_t>(static_cast<int16_t>(x));
}
}  // namespace

Gecko::Gecko(GameCubeMemory* memory) : memory_(memory) {
    Reset();
}

void Gecko::Reset() {
    gpr_.fill(0);
    pc_  = kBiosBase;   // 0xFFF00100 (PPC reset vector)
    lr_  = 0;
    ctr_ = 0;
    cr_  = 0;
    xer_ = 0;
    msr_ = 0x40;
    branch_delay_  = false;
    branch_target_ = 0;
}

// ─────────────────── Memory helpers ────────────────────

uint8_t Gecko::Read8(uint32_t addr) {
    return memory_->Read8(addr);
}
uint16_t Gecko::Read16(uint32_t addr) {
    return memory_->Read16(addr);
}
uint32_t Gecko::Read32(uint32_t addr) {
    return memory_->Read32(addr);
}
void Gecko::Write8(uint32_t addr, uint8_t val) {
    memory_->Write8(addr, val);
}
void Gecko::Write16(uint32_t addr, uint16_t val) {
    memory_->Write16(addr, val);
}
void Gecko::Write32(uint32_t addr, uint32_t val) {
    memory_->Write32(addr, val);
}

// ─────────────────── Instruction decode ────────────────

uint32_t Gecko::Step() {
    if (branch_delay_) {
        pc_ = branch_target_;
        branch_delay_ = false;
    }

    uint32_t op = Read32(pc_);
    pc_ += 4;
    return Execute(op);
}

uint32_t Gecko::Execute(uint32_t op) {
    uint8_t  opcode = (op >> 26) & 0x3F;
    uint8_t  rD = (op >> 21) & 0x1F;
    uint8_t  rA = (op >> 16) & 0x1F;
    uint8_t  rB = (op >> 11) & 0x1F;
    int32_t  imm = SignExt16(op & 0xFFFF);

    switch (opcode) {
    // ── 0x0C addic ───────────────────────────────────────────────────
    case 0x0C:
        gpr_[rD] = gpr_[rA] + static_cast<uint32_t>(imm);
        return 1;

    // ── 0x0E addi / li ──────────────────────────────────────────────
    case 0x0E:
        gpr_[rD] = (rA == 0) ? static_cast<uint32_t>(imm)
                              : gpr_[rA] + static_cast<uint32_t>(imm);
        return 1;

    // ── 0x0F addis / lis ────────────────────────────────────────────
    case 0x0F:
        gpr_[rD] = (rA == 0) ? (static_cast<uint32_t>(imm) << 16)
                              : gpr_[rA] + (static_cast<uint32_t>(imm) << 16);
        return 1;

    // ── 0x10 bc (branch conditional) ────────────────────────────────
    case 0x10: {
        bool lk = (op & 1) != 0;
        bool aa = (op & 2) != 0;
        uint8_t bo = (op >> 21) & 0x1F;
        uint8_t bi = (op >> 16) & 0x1F;
        int32_t bd = static_cast<int32_t>(static_cast<int16_t>(op & 0xFFFC));

        bool dec_ctr = !(bo & 0x04);
        bool ctr_ok  = !dec_ctr || (--ctr_ != 0) == (bool)(bo & 0x02);
        bool cr_bit  = (cr_ >> (31 - bi)) & 1;
        bool cond_ok = (bo & 0x10) || (cr_bit == (bool)(bo & 0x08));

        if (lk) lr_ = pc_;
        if (ctr_ok && cond_ok) {
            pc_ = aa ? static_cast<uint32_t>(bd) : (pc_ - 4) + static_cast<uint32_t>(bd);
        }
        return 1;
    }

    // ── 0x12 b / bl / ba / bla ──────────────────────────────────────
    case 0x12: {
        bool lk = (op & 1) != 0;
        bool aa = (op & 2) != 0;
        int32_t li = SignExt26(op & 0x03FFFFFC);
        if (lk) lr_ = pc_;
        pc_ = aa ? static_cast<uint32_t>(li) : (pc_ - 4) + static_cast<uint32_t>(li);
        return 1;
    }

    // ── 0x13 bclr / bcctr ───────────────────────────────────────────
    case 0x13: {
        uint16_t xo = (op >> 1) & 0x3FF;
        bool lk = (op & 1) != 0;
        if (xo == 16) {  // bclr
            uint32_t target = lr_ & ~0x3U;
            if (lk) lr_ = pc_;
            pc_ = target;
        } else if (xo == 528) {  // bcctr
            uint32_t target = ctr_ & ~0x3U;
            if (lk) lr_ = pc_;
            pc_ = target;
        }
        return 1;
    }

    // ── 0x14 rlwimi ─────────────────────────────────────────────────
    case 0x14: {
        uint8_t sh = (op >> 11) & 0x1F;
        uint8_t mb = (op >> 6)  & 0x1F;
        uint8_t me = (op >> 1)  & 0x1F;
        uint32_t rot = (gpr_[rA] << sh) | (gpr_[rA] >> (32 - sh));
        uint32_t mask = 0;
        for (uint8_t i = mb; ; i = (i + 1) & 0x1F) {
            mask |= (0x80000000U >> i);
            if (i == me) break;
        }
        gpr_[rD] = (rot & mask) | (gpr_[rD] & ~mask);
        return 1;
    }

    // ── 0x15 rlwinm ─────────────────────────────────────────────────
    case 0x15: {
        uint8_t sh = (op >> 11) & 0x1F;
        uint8_t mb = (op >> 6)  & 0x1F;
        uint8_t me = (op >> 1)  & 0x1F;
        uint32_t rot = (gpr_[rA] << sh) | (gpr_[rA] >> (32 - sh));
        uint32_t mask = 0;
        for (uint8_t i = mb; ; i = (i + 1) & 0x1F) {
            mask |= (0x80000000U >> i);
            if (i == me) break;
        }
        gpr_[rD] = rot & mask;
        return 1;
    }

    // ── 0x18 ori ────────────────────────────────────────────────────
    case 0x18:
        gpr_[rA] = gpr_[rD] | (op & 0xFFFF);
        return 1;

    // ── 0x19 oris ───────────────────────────────────────────────────
    case 0x19:
        gpr_[rA] = gpr_[rD] | ((op & 0xFFFF) << 16);
        return 1;

    // ── 0x1A xori ───────────────────────────────────────────────────
    case 0x1A:
        gpr_[rA] = gpr_[rD] ^ (op & 0xFFFF);
        return 1;

    // ── 0x1C andi. ──────────────────────────────────────────────────
    case 0x1C:
        gpr_[rA] = gpr_[rD] & (op & 0xFFFF);
        return 1;

    // ── 0x1F extended integer / move SPR ────────────────────────────
    case 0x1F:
        return ExecuteInteger(op);

    // ── 0x20 lwz ────────────────────────────────────────────────────
    case 0x20:
        gpr_[rD] = Read32((rA ? gpr_[rA] : 0) + static_cast<uint32_t>(imm));
        return 1;

    // ── 0x21 lwzu ───────────────────────────────────────────────────
    case 0x21:
        gpr_[rA] += static_cast<uint32_t>(imm);
        gpr_[rD] = Read32(gpr_[rA]);
        return 1;

    // ── 0x22 lbz ────────────────────────────────────────────────────
    case 0x22:
        gpr_[rD] = Read8((rA ? gpr_[rA] : 0) + static_cast<uint32_t>(imm));
        return 1;

    // ── 0x24 stw ────────────────────────────────────────────────────
    case 0x24:
        Write32((rA ? gpr_[rA] : 0) + static_cast<uint32_t>(imm), gpr_[rD]);
        return 1;

    // ── 0x25 stwu ───────────────────────────────────────────────────
    case 0x25:
        gpr_[rA] += static_cast<uint32_t>(imm);
        Write32(gpr_[rA], gpr_[rD]);
        return 1;

    // ── 0x26 stb ────────────────────────────────────────────────────
    case 0x26:
        Write8((rA ? gpr_[rA] : 0) + static_cast<uint32_t>(imm),
               static_cast<uint8_t>(gpr_[rD]));
        return 1;

    // ── 0x2A lha ────────────────────────────────────────────────────
    case 0x2A:
        gpr_[rD] = static_cast<uint32_t>(
            static_cast<int32_t>(static_cast<int16_t>(
                Read16((rA ? gpr_[rA] : 0) + static_cast<uint32_t>(imm)))));
        return 1;

    // ── 0x2C cmpwi ──────────────────────────────────────────────────
    case 0x2C: {
        int32_t a = static_cast<int32_t>(gpr_[rA]);
        uint8_t bf = (op >> 23) & 0x7;
        uint8_t shift = 28 - (bf * 4);
        cr_ &= ~(0xFU << shift);
        if      (a < imm)  cr_ |= (0x8U << shift);
        else if (a > imm)  cr_ |= (0x4U << shift);
        else               cr_ |= (0x2U << shift);
        return 1;
    }

    // ── 0x2E lmw ─────────────────────────────────────────────────────
    case 0x2E: {
        uint32_t addr = (rA ? gpr_[rA] : 0) + static_cast<uint32_t>(imm);
        for (uint8_t r = rD; r < 32; r++, addr += 4) {
            gpr_[r] = Read32(addr);
        }
        return 1;
    }

    default:
        // Unimplemented / NOP
        return 1;
    }
}

uint32_t Gecko::ExecuteInteger(uint32_t op) {
    uint8_t  rD = (op >> 21) & 0x1F;
    uint8_t  rA = (op >> 16) & 0x1F;
    uint8_t  rB = (op >> 11) & 0x1F;
    uint16_t xo = (op >> 1) & 0x3FF;

    switch (xo) {
    // add
    case 266:
        gpr_[rD] = gpr_[rA] + gpr_[rB];
        return 1;
    // subf
    case 40:
        gpr_[rD] = gpr_[rB] - gpr_[rA];
        return 1;
    // mullw
    case 235:
        gpr_[rD] = static_cast<uint32_t>(
            static_cast<int32_t>(gpr_[rA]) * static_cast<int32_t>(gpr_[rB]));
        return 1;
    // divw
    case 491: {
        int32_t a = static_cast<int32_t>(gpr_[rA]);
        int32_t b = static_cast<int32_t>(gpr_[rB]);
        if (b != 0) gpr_[rD] = static_cast<uint32_t>(a / b);
        return 1;
    }
    // and
    case 28:
        gpr_[rA] = gpr_[rD] & gpr_[rB];
        return 1;
    // or / mr
    case 444:
        gpr_[rA] = gpr_[rD] | gpr_[rB];
        return 1;
    // xor
    case 316:
        gpr_[rA] = gpr_[rD] ^ gpr_[rB];
        return 1;
    // nor
    case 124:
        gpr_[rA] = ~(gpr_[rD] | gpr_[rB]);
        return 1;
    // neg
    case 104:
        gpr_[rD] = static_cast<uint32_t>(-static_cast<int32_t>(gpr_[rA]));
        return 1;
    // slw
    case 24: {
        uint8_t sh = gpr_[rB] & 0x3F;
        gpr_[rA] = (sh < 32) ? (gpr_[rD] << sh) : 0;
        return 1;
    }
    // srw
    case 536: {
        uint8_t sh = gpr_[rB] & 0x3F;
        gpr_[rA] = (sh < 32) ? (gpr_[rD] >> sh) : 0;
        return 1;
    }
    // sraw
    case 792: {
        uint8_t sh = gpr_[rB] & 0x3F;
        gpr_[rA] = (sh < 32)
            ? static_cast<uint32_t>(static_cast<int32_t>(gpr_[rD]) >> sh)
            : 0;
        return 1;
    }
    // mflr
    case 339: {
        uint16_t spr = ((op >> 11) & 0x1F) | (((op >> 16) & 0x1F) << 5);
        if (spr == 8)       gpr_[rD] = lr_;
        else if (spr == 9)  gpr_[rD] = ctr_;
        return 1;
    }
    // mtlr / mtctr
    case 467: {
        uint16_t spr = ((op >> 11) & 0x1F) | (((op >> 16) & 0x1F) << 5);
        if (spr == 8)       lr_  = gpr_[rD];
        else if (spr == 9)  ctr_ = gpr_[rD];
        return 1;
    }
    // cmp
    case 0: {
        uint8_t bf = (op >> 23) & 0x7;
        uint8_t shift = 28 - (bf * 4);
        int32_t a = static_cast<int32_t>(gpr_[rA]);
        int32_t b = static_cast<int32_t>(gpr_[rB]);
        cr_ &= ~(0xFU << shift);
        if      (a < b) cr_ |= (0x8U << shift);
        else if (a > b) cr_ |= (0x4U << shift);
        else            cr_ |= (0x2U << shift);
        return 1;
    }
    // lwzx
    case 23:
        gpr_[rD] = Read32((rA ? gpr_[rA] : 0) + gpr_[rB]);
        return 1;
    // stwx
    case 151:
        Write32((rA ? gpr_[rA] : 0) + gpr_[rB], gpr_[rD]);
        return 1;
    default:
        return 1;
    }
}

// ─────────────────── Save / Load state ────────────────

Gecko::State Gecko::SaveState() const {
    return { gpr_, pc_, lr_, ctr_, cr_, xer_, msr_ };
}

void Gecko::LoadState(const State& s) {
    gpr_  = s.gpr;
    pc_   = s.pc;
    lr_   = s.lr;
    ctr_  = s.ctr;
    cr_   = s.cr;
    xer_  = s.xer;
    msr_  = s.msr;
    branch_delay_  = false;
    branch_target_ = 0;
}

}  // namespace GameCubeEmulator
