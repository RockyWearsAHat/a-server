#include "emulator/atari2600/MOS6507.h"
#include "emulator/atari2600/Atari2600Memory.h"

namespace Atari2600 {

MOS6507::MOS6507(Atari2600Memory& memory) noexcept : mem_(memory) {}

// ─── State ───────────────────────────────────────────────────────────────────

MOS6507::State MOS6507::SaveState() const noexcept {
    return { pc_, a_, x_, y_, s_, p_ };
}

void MOS6507::LoadState(const State& st) noexcept {
    pc_ = st.pc; a_ = st.a; x_ = st.x; y_ = st.y; s_ = st.s; p_ = st.p;
}

// ─── Reset ───────────────────────────────────────────────────────────────────

void MOS6507::Reset() {
    a_ = 0; x_ = 0; y_ = 0;
    s_  = 0xFD;
    p_  = kFlagU | kFlagI;
    pc_ = Read16(kResetVector);
}

// ─── Memory ──────────────────────────────────────────────────────────────────

uint8_t MOS6507::Read8(uint16_t addr) {
    return mem_.Read8(addr & kAddrMask);
}

void MOS6507::Write8(uint16_t addr, uint8_t val) {
    mem_.Write8(addr & kAddrMask, val);
}

uint16_t MOS6507::Read16(uint16_t addr) {
    const uint8_t lo = Read8(addr);
    const uint8_t hi = Read8(static_cast<uint16_t>(addr + 1));
    return static_cast<uint16_t>((hi << 8) | lo);
}

// JMP indirect has a 6502 page-wrap bug: ($12FF) wraps to ($1200) for high byte
uint16_t MOS6507::Read16Wrapped(uint16_t addr) {
    const uint8_t lo = Read8(addr);
    const uint8_t hi = Read8(static_cast<uint16_t>((addr & 0xFF00u) | ((addr + 1) & 0x00FFu)));
    return static_cast<uint16_t>((hi << 8) | lo);
}

// ─── Stack ───────────────────────────────────────────────────────────────────

void MOS6507::Push8(uint8_t val) {
    Write8(kStackBase | s_, val);
    s_--;
}

uint8_t MOS6507::Pop8() {
    s_++;
    return Read8(kStackBase | s_);
}

void MOS6507::Push16(uint16_t val) {
    Push8(static_cast<uint8_t>(val >> 8));
    Push8(static_cast<uint8_t>(val & 0xFF));
}

uint16_t MOS6507::Pop16() {
    const uint8_t lo = Pop8();
    const uint8_t hi = Pop8();
    return static_cast<uint16_t>((hi << 8) | lo);
}

// ─── Addressing modes ────────────────────────────────────────────────────────

uint16_t MOS6507::AddrImmediate() {
    return pc_++;
}

uint16_t MOS6507::AddrZeroPage() {
    return Read8(pc_++);
}

uint16_t MOS6507::AddrZeroPageX() {
    return static_cast<uint16_t>((Read8(pc_++) + x_) & 0xFF);
}

uint16_t MOS6507::AddrZeroPageY() {
    return static_cast<uint16_t>((Read8(pc_++) + y_) & 0xFF);
}

uint16_t MOS6507::AddrAbsolute() {
    const uint16_t addr = Read16(pc_);
    pc_ += 2;
    return addr;
}

uint16_t MOS6507::AddrAbsoluteX(int& extra) {
    const uint16_t base = Read16(pc_);
    pc_ += 2;
    const uint16_t ea = static_cast<uint16_t>(base + x_);
    extra = ((base & 0xFF00) != (ea & 0xFF00)) ? 1 : 0;
    return ea;
}

uint16_t MOS6507::AddrAbsoluteY(int& extra) {
    const uint16_t base = Read16(pc_);
    pc_ += 2;
    const uint16_t ea = static_cast<uint16_t>(base + y_);
    extra = ((base & 0xFF00) != (ea & 0xFF00)) ? 1 : 0;
    return ea;
}

uint16_t MOS6507::AddrIndirectX() {
    const uint8_t ptr = static_cast<uint8_t>(Read8(pc_++) + x_);
    const uint8_t lo  = Read8(ptr);
    const uint8_t hi  = Read8(static_cast<uint8_t>(ptr + 1));
    return static_cast<uint16_t>((hi << 8) | lo);
}

uint16_t MOS6507::AddrIndirectY(int& extra) {
    const uint8_t  ptr  = Read8(pc_++);
    const uint8_t  lo   = Read8(ptr);
    const uint8_t  hi   = Read8(static_cast<uint8_t>(ptr + 1));
    const uint16_t base = static_cast<uint16_t>((hi << 8) | lo);
    const uint16_t ea   = static_cast<uint16_t>(base + y_);
    extra = ((base & 0xFF00) != (ea & 0xFF00)) ? 1 : 0;
    return ea;
}

int8_t MOS6507::AddrRelative() {
    return static_cast<int8_t>(Read8(pc_++));
}

// ─── Branch helper ───────────────────────────────────────────────────────────

int MOS6507::Branch(bool condition, int8_t offset) {
    if (!condition) return 0;
    const uint16_t cur = pc_;
    pc_ = static_cast<uint16_t>(pc_ + offset);
    return ((cur & 0xFF00) != (pc_ & 0xFF00)) ? 2 : 1;
}

// ─── Instruction implementations ─────────────────────────────────────────────

void MOS6507::ExecADC(uint16_t ea) {
    const uint8_t  val = Read8(ea);
    const uint16_t sum = static_cast<uint16_t>(a_) + val + (GetC() ? 1 : 0);
    SetV(((a_ ^ sum) & (val ^ sum) & 0x80) != 0);
    SetC(sum > 0xFF);
    a_ = static_cast<uint8_t>(sum);
    SetNZ(a_);
}

void MOS6507::ExecAND(uint16_t ea) { a_ &= Read8(ea); SetNZ(a_); }

void MOS6507::ExecASL_A() {
    SetC(a_ & 0x80);
    a_ = static_cast<uint8_t>(a_ << 1);
    SetNZ(a_);
}

void MOS6507::ExecASL_M(uint16_t ea) {
    uint8_t v = Read8(ea);
    SetC(v & 0x80);
    v = static_cast<uint8_t>(v << 1);
    Write8(ea, v);
    SetNZ(v);
}

void MOS6507::ExecBIT(uint16_t ea) {
    const uint8_t v = Read8(ea);
    SetZ((a_ & v) == 0);
    SetN(v & 0x80);
    SetV(v & 0x40);
}

void MOS6507::ExecCMP(uint16_t ea) {
    const uint8_t v = Read8(ea);
    SetC(a_ >= v);
    SetNZ(static_cast<uint8_t>(a_ - v));
}

void MOS6507::ExecCPX(uint16_t ea) {
    const uint8_t v = Read8(ea);
    SetC(x_ >= v);
    SetNZ(static_cast<uint8_t>(x_ - v));
}

void MOS6507::ExecCPY(uint16_t ea) {
    const uint8_t v = Read8(ea);
    SetC(y_ >= v);
    SetNZ(static_cast<uint8_t>(y_ - v));
}

void MOS6507::ExecDEC(uint16_t ea) {
    const uint8_t v = static_cast<uint8_t>(Read8(ea) - 1);
    Write8(ea, v);
    SetNZ(v);
}

void MOS6507::ExecEOR(uint16_t ea) { a_ ^= Read8(ea); SetNZ(a_); }

void MOS6507::ExecINC(uint16_t ea) {
    const uint8_t v = static_cast<uint8_t>(Read8(ea) + 1);
    Write8(ea, v);
    SetNZ(v);
}

void MOS6507::ExecJMP(uint16_t addr) { pc_ = addr; }
void MOS6507::ExecJSR(uint16_t addr) { Push16(static_cast<uint16_t>(pc_ - 1)); pc_ = addr; }

void MOS6507::ExecLDA(uint16_t ea) { a_ = Read8(ea); SetNZ(a_); }
void MOS6507::ExecLDX(uint16_t ea) { x_ = Read8(ea); SetNZ(x_); }
void MOS6507::ExecLDY(uint16_t ea) { y_ = Read8(ea); SetNZ(y_); }

void MOS6507::ExecLSR_A() {
    SetC(a_ & 0x01);
    a_ >>= 1;
    SetNZ(a_);
}

void MOS6507::ExecLSR_M(uint16_t ea) {
    uint8_t v = Read8(ea);
    SetC(v & 0x01);
    v >>= 1;
    Write8(ea, v);
    SetNZ(v);
}

void MOS6507::ExecORA(uint16_t ea) { a_ |= Read8(ea); SetNZ(a_); }

void MOS6507::ExecROL_A() {
    const uint8_t newC = a_ >> 7;
    a_ = static_cast<uint8_t>((a_ << 1) | (GetC() ? 1 : 0));
    SetC(newC);
    SetNZ(a_);
}

void MOS6507::ExecROL_M(uint16_t ea) {
    uint8_t v = Read8(ea);
    const uint8_t newC = v >> 7;
    v = static_cast<uint8_t>((v << 1) | (GetC() ? 1 : 0));
    Write8(ea, v);
    SetC(newC);
    SetNZ(v);
}

void MOS6507::ExecROR_A() {
    const uint8_t newC = a_ & 0x01;
    a_ = static_cast<uint8_t>((a_ >> 1) | (GetC() ? 0x80 : 0));
    SetC(newC);
    SetNZ(a_);
}

void MOS6507::ExecROR_M(uint16_t ea) {
    uint8_t v = Read8(ea);
    const uint8_t newC = v & 0x01;
    v = static_cast<uint8_t>((v >> 1) | (GetC() ? 0x80 : 0));
    Write8(ea, v);
    SetC(newC);
    SetNZ(v);
}

void MOS6507::ExecSBC(uint16_t ea) {
    const uint8_t  val = Read8(ea);
    const uint16_t diff = static_cast<uint16_t>(a_) - val - (GetC() ? 0 : 1);
    SetV(((a_ ^ val) & (a_ ^ diff) & 0x80) != 0);
    SetC(diff < 0x100);
    a_ = static_cast<uint8_t>(diff);
    SetNZ(a_);
}

void MOS6507::ExecSTA(uint16_t ea) { Write8(ea, a_); }
void MOS6507::ExecSTX(uint16_t ea) { Write8(ea, x_); }
void MOS6507::ExecSTY(uint16_t ea) { Write8(ea, y_); }

// ─── Main Opcode Dispatch ─────────────────────────────────────────────────────

int MOS6507::Step() {
    const uint8_t op = Read8(pc_++);
    int cycles  = 2; // Minimum for any instruction
    int extra   = 0;

    switch (op) {
        // ── BRK ──────────────────────────────────────────────────────────
        case 0x00:
            pc_++;  // skip padding byte
            Push16(pc_);
            Push8(p_ | kFlagB | kFlagU);
            SetI(true);
            pc_ = Read16(kIRQVector);
            cycles = 7;
            break;

        // ── ORA ──────────────────────────────────────────────────────────
        case 0x01: ExecORA(AddrIndirectX());           cycles = 6; break;
        case 0x05: ExecORA(AddrZeroPage());            cycles = 3; break;
        case 0x09: ExecORA(AddrImmediate());           cycles = 2; break;
        case 0x0D: ExecORA(AddrAbsolute());            cycles = 4; break;
        case 0x11: ExecORA(AddrIndirectY(extra));      cycles = 5 + extra; break;
        case 0x15: ExecORA(AddrZeroPageX());           cycles = 4; break;
        case 0x19: ExecORA(AddrAbsoluteY(extra));      cycles = 4 + extra; break;
        case 0x1D: ExecORA(AddrAbsoluteX(extra));      cycles = 4 + extra; break;

        // ── ASL ──────────────────────────────────────────────────────────
        case 0x06: ExecASL_M(AddrZeroPage());          cycles = 5; break;
        case 0x0A: ExecASL_A();                        cycles = 2; break;
        case 0x0E: ExecASL_M(AddrAbsolute());          cycles = 6; break;
        case 0x16: ExecASL_M(AddrZeroPageX());         cycles = 6; break;
        case 0x1E: ExecASL_M(AddrAbsoluteX(extra));   cycles = 7; break;

        // ── PHP / PLP ─────────────────────────────────────────────────────
        case 0x08: Push8(p_ | kFlagB | kFlagU);       cycles = 3; break;
        case 0x28: p_ = (Pop8() & ~kFlagB) | kFlagU;  cycles = 4; break;

        // ── Branches ─────────────────────────────────────────────────────
        case 0x10: cycles = 2 + Branch(!GetN(), AddrRelative()); break; // BPL
        case 0x30: cycles = 2 + Branch( GetN(), AddrRelative()); break; // BMI
        case 0x50: cycles = 2 + Branch(!GetV(), AddrRelative()); break; // BVC
        case 0x70: cycles = 2 + Branch( GetV(), AddrRelative()); break; // BVS
        case 0x90: cycles = 2 + Branch(!GetC(), AddrRelative()); break; // BCC
        case 0xB0: cycles = 2 + Branch( GetC(), AddrRelative()); break; // BCS
        case 0xD0: cycles = 2 + Branch(!GetZ(), AddrRelative()); break; // BNE
        case 0xF0: cycles = 2 + Branch( GetZ(), AddrRelative()); break; // BEQ

        // ── CLC / SEC / CLI / SEI / CLV / CLD / SED ──────────────────────
        case 0x18: SetC(false); cycles = 2; break;
        case 0x38: SetC(true);  cycles = 2; break;
        case 0x58: SetI(false); cycles = 2; break;
        case 0x78: SetI(true);  cycles = 2; break;
        case 0xB8: SetV(false); cycles = 2; break;
        case 0xD8: SetD(false); cycles = 2; break;
        case 0xF8: SetD(true);  cycles = 2; break;

        // ── JSR / RTS / RTI ───────────────────────────────────────────────
        case 0x20: ExecJSR(AddrAbsolute()); cycles = 6; break;
        case 0x60: pc_ = static_cast<uint16_t>(Pop16() + 1); cycles = 6; break; // RTS
        case 0x40: // RTI
            p_  = (Pop8() & ~kFlagB) | kFlagU;
            pc_ = Pop16();
            cycles = 6;
            break;

        // ── JMP ───────────────────────────────────────────────────────────
        case 0x4C: ExecJMP(AddrAbsolute());                          cycles = 3; break;
        case 0x6C: ExecJMP(Read16Wrapped(AddrAbsolute()));           cycles = 5; break;

        // ── BIT ───────────────────────────────────────────────────────────
        case 0x24: ExecBIT(AddrZeroPage());  cycles = 3; break;
        case 0x2C: ExecBIT(AddrAbsolute());  cycles = 4; break;

        // ── AND ───────────────────────────────────────────────────────────
        case 0x21: ExecAND(AddrIndirectX());          cycles = 6; break;
        case 0x25: ExecAND(AddrZeroPage());            cycles = 3; break;
        case 0x29: ExecAND(AddrImmediate());           cycles = 2; break;
        case 0x2D: ExecAND(AddrAbsolute());            cycles = 4; break;
        case 0x31: ExecAND(AddrIndirectY(extra));      cycles = 5 + extra; break;
        case 0x35: ExecAND(AddrZeroPageX());           cycles = 4; break;
        case 0x39: ExecAND(AddrAbsoluteY(extra));      cycles = 4 + extra; break;
        case 0x3D: ExecAND(AddrAbsoluteX(extra));      cycles = 4 + extra; break;

        // ── ROL ───────────────────────────────────────────────────────────
        case 0x26: ExecROL_M(AddrZeroPage());          cycles = 5; break;
        case 0x2A: ExecROL_A();                        cycles = 2; break;
        case 0x2E: ExecROL_M(AddrAbsolute());          cycles = 6; break;
        case 0x36: ExecROL_M(AddrZeroPageX());         cycles = 6; break;
        case 0x3E: ExecROL_M(AddrAbsoluteX(extra));   cycles = 7; break;

        // ── EOR ───────────────────────────────────────────────────────────
        case 0x41: ExecEOR(AddrIndirectX());           cycles = 6; break;
        case 0x45: ExecEOR(AddrZeroPage());            cycles = 3; break;
        case 0x49: ExecEOR(AddrImmediate());           cycles = 2; break;
        case 0x4D: ExecEOR(AddrAbsolute());            cycles = 4; break;
        case 0x51: ExecEOR(AddrIndirectY(extra));      cycles = 5 + extra; break;
        case 0x55: ExecEOR(AddrZeroPageX());           cycles = 4; break;
        case 0x59: ExecEOR(AddrAbsoluteY(extra));      cycles = 4 + extra; break;
        case 0x5D: ExecEOR(AddrAbsoluteX(extra));      cycles = 4 + extra; break;

        // ── LSR ───────────────────────────────────────────────────────────
        case 0x46: ExecLSR_M(AddrZeroPage());          cycles = 5; break;
        case 0x4A: ExecLSR_A();                        cycles = 2; break;
        case 0x4E: ExecLSR_M(AddrAbsolute());          cycles = 6; break;
        case 0x56: ExecLSR_M(AddrZeroPageX());         cycles = 6; break;
        case 0x5E: ExecLSR_M(AddrAbsoluteX(extra));   cycles = 7; break;

        // ── PHA / PLA ─────────────────────────────────────────────────────
        case 0x48: Push8(a_); cycles = 3; break;
        case 0x68: a_ = Pop8(); SetNZ(a_); cycles = 4; break;

        // ── ADC ───────────────────────────────────────────────────────────
        case 0x61: ExecADC(AddrIndirectX());           cycles = 6; break;
        case 0x65: ExecADC(AddrZeroPage());            cycles = 3; break;
        case 0x69: ExecADC(AddrImmediate());           cycles = 2; break;
        case 0x6D: ExecADC(AddrAbsolute());            cycles = 4; break;
        case 0x71: ExecADC(AddrIndirectY(extra));      cycles = 5 + extra; break;
        case 0x75: ExecADC(AddrZeroPageX());           cycles = 4; break;
        case 0x79: ExecADC(AddrAbsoluteY(extra));      cycles = 4 + extra; break;
        case 0x7D: ExecADC(AddrAbsoluteX(extra));      cycles = 4 + extra; break;

        // ── ROR ───────────────────────────────────────────────────────────
        case 0x66: ExecROR_M(AddrZeroPage());          cycles = 5; break;
        case 0x6A: ExecROR_A();                        cycles = 2; break;
        case 0x6E: ExecROR_M(AddrAbsolute());          cycles = 6; break;
        case 0x76: ExecROR_M(AddrZeroPageX());         cycles = 6; break;
        case 0x7E: ExecROR_M(AddrAbsoluteX(extra));   cycles = 7; break;

        // ── STA ───────────────────────────────────────────────────────────
        case 0x81: ExecSTA(AddrIndirectX());           cycles = 6; break;
        case 0x85: ExecSTA(AddrZeroPage());            cycles = 3; break;
        case 0x8D: ExecSTA(AddrAbsolute());            cycles = 4; break;
        case 0x91: ExecSTA(AddrIndirectY(extra));      cycles = 6; break;
        case 0x95: ExecSTA(AddrZeroPageX());           cycles = 4; break;
        case 0x99: ExecSTA(AddrAbsoluteY(extra));      cycles = 5; break;
        case 0x9D: ExecSTA(AddrAbsoluteX(extra));      cycles = 5; break;

        // ── STX ───────────────────────────────────────────────────────────
        case 0x86: ExecSTX(AddrZeroPage());  cycles = 3; break;
        case 0x8E: ExecSTX(AddrAbsolute());  cycles = 4; break;
        case 0x96: ExecSTX(AddrZeroPageY()); cycles = 4; break;

        // ── STY ───────────────────────────────────────────────────────────
        case 0x84: ExecSTY(AddrZeroPage());  cycles = 3; break;
        case 0x8C: ExecSTY(AddrAbsolute());  cycles = 4; break;
        case 0x94: ExecSTY(AddrZeroPageX()); cycles = 4; break;

        // ── DEY / INY / DEX / INX / TAX / TXA / TAY / TYA ───────────────
        case 0x88: y_--; SetNZ(y_); cycles = 2; break; // DEY
        case 0xC8: y_++; SetNZ(y_); cycles = 2; break; // INY
        case 0xCA: x_--; SetNZ(x_); cycles = 2; break; // DEX
        case 0xE8: x_++; SetNZ(x_); cycles = 2; break; // INX
        case 0xAA: x_ = a_; SetNZ(x_); cycles = 2; break; // TAX
        case 0x8A: a_ = x_; SetNZ(a_); cycles = 2; break; // TXA
        case 0xA8: y_ = a_; SetNZ(y_); cycles = 2; break; // TAY
        case 0x98: a_ = y_; SetNZ(a_); cycles = 2; break; // TYA

        // ── TXS / TSX ─────────────────────────────────────────────────────
        case 0x9A: s_ = x_;           cycles = 2; break; // TXS
        case 0xBA: x_ = s_; SetNZ(x_); cycles = 2; break; // TSX

        // ── LDA ───────────────────────────────────────────────────────────
        case 0xA1: ExecLDA(AddrIndirectX());           cycles = 6; break;
        case 0xA5: ExecLDA(AddrZeroPage());            cycles = 3; break;
        case 0xA9: ExecLDA(AddrImmediate());           cycles = 2; break;
        case 0xAD: ExecLDA(AddrAbsolute());            cycles = 4; break;
        case 0xB1: ExecLDA(AddrIndirectY(extra));      cycles = 5 + extra; break;
        case 0xB5: ExecLDA(AddrZeroPageX());           cycles = 4; break;
        case 0xB9: ExecLDA(AddrAbsoluteY(extra));      cycles = 4 + extra; break;
        case 0xBD: ExecLDA(AddrAbsoluteX(extra));      cycles = 4 + extra; break;

        // ── LDX ───────────────────────────────────────────────────────────
        case 0xA2: ExecLDX(AddrImmediate());           cycles = 2; break;
        case 0xA6: ExecLDX(AddrZeroPage());            cycles = 3; break;
        case 0xAE: ExecLDX(AddrAbsolute());            cycles = 4; break;
        case 0xB6: ExecLDX(AddrZeroPageY());           cycles = 4; break;
        case 0xBE: ExecLDX(AddrAbsoluteY(extra));      cycles = 4 + extra; break;

        // ── LDY ───────────────────────────────────────────────────────────
        case 0xA0: ExecLDY(AddrImmediate());           cycles = 2; break;
        case 0xA4: ExecLDY(AddrZeroPage());            cycles = 3; break;
        case 0xAC: ExecLDY(AddrAbsolute());            cycles = 4; break;
        case 0xB4: ExecLDY(AddrZeroPageX());           cycles = 4; break;
        case 0xBC: ExecLDY(AddrAbsoluteX(extra));      cycles = 4 + extra; break;

        // ── CMP ───────────────────────────────────────────────────────────
        case 0xC1: ExecCMP(AddrIndirectX());           cycles = 6; break;
        case 0xC5: ExecCMP(AddrZeroPage());            cycles = 3; break;
        case 0xC9: ExecCMP(AddrImmediate());           cycles = 2; break;
        case 0xCD: ExecCMP(AddrAbsolute());            cycles = 4; break;
        case 0xD1: ExecCMP(AddrIndirectY(extra));      cycles = 5 + extra; break;
        case 0xD5: ExecCMP(AddrZeroPageX());           cycles = 4; break;
        case 0xD9: ExecCMP(AddrAbsoluteY(extra));      cycles = 4 + extra; break;
        case 0xDD: ExecCMP(AddrAbsoluteX(extra));      cycles = 4 + extra; break;

        // ── CPX ───────────────────────────────────────────────────────────
        case 0xE0: ExecCPX(AddrImmediate()); cycles = 2; break;
        case 0xE4: ExecCPX(AddrZeroPage());  cycles = 3; break;
        case 0xEC: ExecCPX(AddrAbsolute());  cycles = 4; break;

        // ── CPY ───────────────────────────────────────────────────────────
        case 0xC0: ExecCPY(AddrImmediate()); cycles = 2; break;
        case 0xC4: ExecCPY(AddrZeroPage());  cycles = 3; break;
        case 0xCC: ExecCPY(AddrAbsolute());  cycles = 4; break;

        // ── DEC ───────────────────────────────────────────────────────────
        case 0xC6: ExecDEC(AddrZeroPage());          cycles = 5; break;
        case 0xCE: ExecDEC(AddrAbsolute());          cycles = 6; break;
        case 0xD6: ExecDEC(AddrZeroPageX());         cycles = 6; break;
        case 0xDE: ExecDEC(AddrAbsoluteX(extra));    cycles = 7; break;

        // ── INC ───────────────────────────────────────────────────────────
        case 0xE6: ExecINC(AddrZeroPage());          cycles = 5; break;
        case 0xEE: ExecINC(AddrAbsolute());          cycles = 6; break;
        case 0xF6: ExecINC(AddrZeroPageX());         cycles = 6; break;
        case 0xFE: ExecINC(AddrAbsoluteX(extra));    cycles = 7; break;

        // ── SBC ───────────────────────────────────────────────────────────
        case 0xE1: ExecSBC(AddrIndirectX());           cycles = 6; break;
        case 0xE5: ExecSBC(AddrZeroPage());            cycles = 3; break;
        case 0xE9: ExecSBC(AddrImmediate());           cycles = 2; break;
        case 0xED: ExecSBC(AddrAbsolute());            cycles = 4; break;
        case 0xF1: ExecSBC(AddrIndirectY(extra));      cycles = 5 + extra; break;
        case 0xF5: ExecSBC(AddrZeroPageX());           cycles = 4; break;
        case 0xF9: ExecSBC(AddrAbsoluteY(extra));      cycles = 4 + extra; break;
        case 0xFD: ExecSBC(AddrAbsoluteX(extra));      cycles = 4 + extra; break;

        // ── NOP ───────────────────────────────────────────────────────────
        case 0xEA: cycles = 2; break;

        // ── Unofficial NOPs (common ones used by some 2600 games) ─────────
        case 0x1A: case 0x3A: case 0x5A: case 0x7A:
        case 0xDA: case 0xFA:
            cycles = 2; break;
        case 0x04: case 0x44: case 0x64:
            pc_++; cycles = 3; break;  // SKB zero-page
        case 0x0C:
            pc_ += 2; cycles = 4; break; // SKW absolute

        default:
            cycles = 2; break; // Unknown opcode — treat as NOP
    }

    return cycles;
}

} // namespace Atari2600
