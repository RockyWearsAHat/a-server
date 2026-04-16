// M68000.cpp — minimal MC68000 execution scaffold for Genesis bring-up.
// NOTE: This is a functional skeleton focused on reset/vector correctness and
//       deterministic stepping. Instruction coverage will be expanded in phases.

#include "emulator/genesis/M68000.h"
#include "emulator/common/SaveState.h"
#include "emulator/genesis/GenesisMemory.h"
#include <stdexcept>

namespace AIO::Emulator::Genesis {

M68000::M68000(GenesisMemory& mem) noexcept
    : mem_(mem) {
    Reset();
}

void M68000::EnterSuper() noexcept {
    if (!IsSuper()) {
        usp_ = a_[7];
        a_[7] = ssp_;
        sr_ |= 0x2000;
    }
}

void M68000::LeaveSuper() noexcept {
    if (IsSuper()) {
        ssp_ = a_[7];
        a_[7] = usp_;
        sr_ &= static_cast<uint16_t>(~0x2000);
    }
}

uint8_t M68000::Read8(uint32_t addr) {
    return mem_.M68KRead8(addr);
}

uint16_t M68000::Read16(uint32_t addr) {
    if ((addr & 1u) != 0) {
        throw std::runtime_error("M68000 address error on odd Read16");
    }
    return mem_.M68KRead16(addr);
}

uint32_t M68000::Read32(uint32_t addr) {
    const uint16_t hi = Read16(addr);
    const uint16_t lo = Read16(addr + 2);
    return (static_cast<uint32_t>(hi) << 16) | lo;
}

void M68000::Write8(uint32_t addr, uint8_t v) {
    mem_.M68KWrite8(addr, v);
}

void M68000::Write16(uint32_t addr, uint16_t v) {
    if ((addr & 1u) != 0) {
        throw std::runtime_error("M68000 address error on odd Write16");
    }
    mem_.M68KWrite16(addr, v);
}

void M68000::Write32(uint32_t addr, uint32_t v) {
    Write16(addr,     static_cast<uint16_t>(v >> 16));
    Write16(addr + 2, static_cast<uint16_t>(v));
}

void M68000::Push16(uint16_t v) {
    a_[7] -= 2;
    Write16(a_[7], v);
}

void M68000::Push32(uint32_t v) {
    a_[7] -= 4;
    Write32(a_[7], v);
}

uint16_t M68000::Pop16() {
    const uint16_t v = Read16(a_[7]);
    a_[7] += 2;
    return v;
}

uint32_t M68000::Pop32() {
    const uint32_t v = Read32(a_[7]);
    a_[7] += 4;
    return v;
}

uint32_t M68000::CalcEA(int /*mode*/, int /*reg*/, int /*size*/) {
    return 0;
}

void M68000::ServiceInterrupt(int level) {
    if (level <= GetIPL()) {
        return;
    }

    EnterSuper();
    Push16(sr_);
    Push32(pc_);
    SetIPL(level);

    // 68000 vector base is fixed at 0x000000; autovector starts at 0x18.
    const uint32_t vector = static_cast<uint32_t>(0x18 + level) * 4u;
    pc_ = Read32(vector);
}

void M68000::Reset() {
    // Supervisor mode, IPL=7, trace off.
    sr_ = 0x2700;
    halted_ = false;
    stopped_ = false;
    busError_ = false;
    pendingIpl_ = 0;

    // RESET vector table: [0]=SSP, [1]=PC
    ssp_ = Read32(0x000000);
    usp_ = 0;
    a_[7] = ssp_;
    pc_ = Read32(0x000004);
}

int M68000::Dispatch(uint16_t opcode) {
    // Minimal bring-up set:
    // 0x4E71 = NOP
    // 0x4E72 = STOP #imm
    // 0x4E75 = RTS
    // 0x4E73 = RTE
    // 0x4E70 = RESET
    if (opcode == 0x4E71) { return ExecNOP(); }
    if (opcode == 0x4E72) { return ExecSTOP(opcode); }
    if (opcode == 0x4E75) { return ExecRTS(); }
    if (opcode == 0x4E73) { return ExecRTE(); }
    if (opcode == 0x4E70) { return ExecRESET(); }

    // Branch family
    if ((opcode & 0xF000) == 0x6000) {
        if ((opcode & 0x0F00) == 0x0000) { return ExecBRA(opcode); }
        if ((opcode & 0x0F00) == 0x0100) { return ExecBSR(opcode); }
        return ExecBcc(opcode);
    }

    // Unknown opcode trap for now (deterministic behavior)
    return ExecILLEGAL();
}

int M68000::Step() {
    if (busError_) {
        // Bus error vector = 2
        EnterSuper();
        Push16(sr_);
        Push32(pc_);
        pc_ = Read32(2u * 4u);
        busError_ = false;
        return 50;
    }

    if (pendingIpl_ > 0 && pendingIpl_ > GetIPL()) {
        ServiceInterrupt(pendingIpl_);
        return 44;
    }

    if (halted_ || stopped_) {
        return 4;
    }

    const uint16_t opcode = Read16(pc_);
    pc_ += 2;
    return Dispatch(opcode);
}

void M68000::SaveState(AIO::Emulator::Common::SaveStateWriter& w) const {
    for (uint32_t v : d_) { w.WriteU32(v); }
    for (uint32_t v : a_) { w.WriteU32(v); }
    w.WriteU32(ssp_);
    w.WriteU32(usp_);
    w.WriteU32(pc_);
    w.WriteU16(sr_);
    w.WriteU8(static_cast<uint8_t>(pendingIpl_));
    w.WriteBool(halted_);
    w.WriteBool(busError_);
    w.WriteBool(stopped_);
}

void M68000::LoadState(AIO::Emulator::Common::SaveStateReader& r) {
    for (uint32_t& v : d_) { v = r.ReadU32(); }
    for (uint32_t& v : a_) { v = r.ReadU32(); }
    ssp_ = r.ReadU32();
    usp_ = r.ReadU32();
    pc_  = r.ReadU32();
    sr_  = r.ReadU16();
    pendingIpl_ = static_cast<int>(r.ReadU8());
    halted_ = r.ReadBool();
    busError_ = r.ReadBool();
    stopped_ = r.ReadBool();
}

// ── Implemented minimal opcodes ─────────────────────────────────────────────
int M68000::ExecNOP() { return 4; }

int M68000::ExecSTOP(uint16_t /*op*/) {
    const uint16_t newSr = Read16(pc_);
    pc_ += 2;
    sr_ = newSr;
    stopped_ = true;
    return 4;
}

int M68000::ExecRESET() {
    // External RESET signal to devices; CPU itself keeps running.
    return 132;
}

int M68000::ExecRTS() {
    pc_ = Pop32();
    return 16;
}

int M68000::ExecRTE() {
    sr_ = Pop16();
    pc_ = Pop32();
    return 20;
}

int M68000::ExecRTR() {
    const uint16_t ccr = Pop16();
    sr_ = static_cast<uint16_t>((sr_ & 0xFF00) | (ccr & 0x00FF));
    pc_ = Pop32();
    return 20;
}

int M68000::ExecBRA(uint16_t op) {
    int16_t disp = static_cast<int8_t>(op & 0x00FF);
    if ((op & 0x00FF) == 0) {
        disp = static_cast<int16_t>(Read16(pc_));
        pc_ += 2;
    }
    pc_ = static_cast<uint32_t>(static_cast<int32_t>(pc_) + disp);
    return 10;
}

int M68000::ExecBSR(uint16_t op) {
    int16_t disp = static_cast<int8_t>(op & 0x00FF);
    if ((op & 0x00FF) == 0) {
        disp = static_cast<int16_t>(Read16(pc_));
        pc_ += 2;
    }
    Push32(pc_);
    pc_ = static_cast<uint32_t>(static_cast<int32_t>(pc_) + disp);
    return 18;
}

int M68000::ExecBcc(uint16_t op) {
    const uint8_t cond = static_cast<uint8_t>((op >> 8) & 0x0F);
    bool take = false;
    switch (cond) {
        case 0x2: take = !FlagC() && !FlagZ(); break; // HI
        case 0x3: take =  FlagC() ||  FlagZ(); break; // LS
        case 0x4: take = !FlagC();             break; // CC
        case 0x5: take =  FlagC();             break; // CS
        case 0x6: take = !FlagZ();             break; // NE
        case 0x7: take =  FlagZ();             break; // EQ
        case 0x8: take = !FlagV();             break; // VC
        case 0x9: take =  FlagV();             break; // VS
        case 0xA: take = !FlagN();             break; // PL
        case 0xB: take =  FlagN();             break; // MI
        case 0xC: take = (FlagN() == FlagV()); break; // GE
        case 0xD: take = (FlagN() != FlagV()); break; // LT
        case 0xE: take = !FlagZ() && (FlagN() == FlagV()); break; // GT
        case 0xF: take =  FlagZ() || (FlagN() != FlagV()); break; // LE
        default:  take = false; break;
    }

    int16_t disp = static_cast<int8_t>(op & 0x00FF);
    if ((op & 0x00FF) == 0) {
        disp = static_cast<int16_t>(Read16(pc_));
        pc_ += 2;
    }
    if (take) {
        pc_ = static_cast<uint32_t>(static_cast<int32_t>(pc_) + disp);
        return 10;
    }
    return 8;
}

int M68000::ExecILLEGAL() {
    // Vector #4 = illegal instruction.
    EnterSuper();
    Push16(sr_);
    Push32(pc_ - 2);
    pc_ = Read32(4u * 4u);
    return 34;
}

// ── Unimplemented opcode groups: deterministic stubs ────────────────────────
#define M68K_STUB(name, cycles) int M68000::name(uint16_t) { return cycles; }
#define M68K_STUB0(name, cycles) int M68000::name() { return cycles; }

M68K_STUB(ExecMOVE, 4)
M68K_STUB(ExecMOVEA, 4)
M68K_STUB(ExecMOVESR, 4)
M68K_STUB(ExecMOVEUSP, 4)
M68K_STUB(ExecMOVEM, 8)
M68K_STUB(ExecMOVEP, 8)
M68K_STUB(ExecLEA, 4)
M68K_STUB(ExecPEA, 8)
M68K_STUB(ExecEXG, 6)
M68K_STUB(ExecEXT, 4)
M68K_STUB(ExecSWAP, 4)
M68K_STUB(ExecADD, 4)
M68K_STUB(ExecADDA, 4)
M68K_STUB(ExecADDI, 8)
M68K_STUB(ExecADDQ, 4)
M68K_STUB(ExecADDX, 4)
M68K_STUB(ExecSUB, 4)
M68K_STUB(ExecSUBA, 4)
M68K_STUB(ExecSUBI, 8)
M68K_STUB(ExecSUBQ, 4)
M68K_STUB(ExecSUBX, 4)
M68K_STUB(ExecMULS, 40)
M68K_STUB(ExecMULU, 40)
M68K_STUB(ExecDIVS, 140)
M68K_STUB(ExecDIVU, 140)
M68K_STUB(ExecNEG, 4)
M68K_STUB(ExecNEGX, 4)
M68K_STUB(ExecCLR, 4)
M68K_STUB(ExecCMP, 4)
M68K_STUB(ExecCMPA, 4)
M68K_STUB(ExecCMPI, 8)
M68K_STUB(ExecCMPM, 4)
M68K_STUB(ExecAND, 4)
M68K_STUB(ExecANDI, 8)
M68K_STUB(ExecANDISR, 20)
M68K_STUB(ExecOR, 4)
M68K_STUB(ExecORI, 8)
M68K_STUB(ExecORISR, 20)
M68K_STUB(ExecEOR, 4)
M68K_STUB(ExecEORI, 8)
M68K_STUB(ExecNOT, 4)
M68K_STUB(ExecASL, 6)
M68K_STUB(ExecASR, 6)
M68K_STUB(ExecLSL, 6)
M68K_STUB(ExecLSR, 6)
M68K_STUB(ExecROL, 6)
M68K_STUB(ExecROR, 6)
M68K_STUB(ExecROXL, 6)
M68K_STUB(ExecROXR, 6)
M68K_STUB(ExecBTST, 4)
M68K_STUB(ExecBSET, 8)
M68K_STUB(ExecBCLR, 8)
M68K_STUB(ExecBCHG, 8)
M68K_STUB(ExecDBcc, 10)
M68K_STUB(ExecScc, 8)
M68K_STUB(ExecJMP, 8)
M68K_STUB(ExecJSR, 16)
M68K_STUB(ExecTST, 4)
M68K_STUB(ExecTAS, 4)
M68K_STUB(ExecTRAP, 34)
M68K_STUB0(ExecTRAPV, 34)
M68K_STUB(ExecCHK, 10)
M68K_STUB0(ExecLINEA, 34)
M68K_STUB0(ExecLINEF, 34)
M68K_STUB(ExecLINK, 16)
M68K_STUB(ExecUNLK, 12)
M68K_STUB(ExecABCD, 6)
M68K_STUB(ExecSBCD, 6)
M68K_STUB(ExecNBCD, 6)
M68K_STUB(ExecPACK, 8)
M68K_STUB(ExecUNPK, 8)

#undef M68K_STUB
#undef M68K_STUB0

} // namespace AIO::Emulator::Genesis
