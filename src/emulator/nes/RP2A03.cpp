#include "emulator/nes/RP2A03.h"
#include "emulator/nes/NESMemory.h"
#include "emulator/common/SaveState.h"
#include "emulator/nes/NESConstants.h"

namespace AIO::Emulator::NES {

// ── CPU status flag masks ─────────────────────────────────────────────────
static constexpr uint8_t kFlagC = 0x01; // Carry
static constexpr uint8_t kFlagZ = 0x02; // Zero
static constexpr uint8_t kFlagI = 0x04; // Interrupt disable
static constexpr uint8_t kFlagD = 0x08; // Decimal (ignored by 2A03 hardware)
static constexpr uint8_t kFlagB = 0x10; // Break
static constexpr uint8_t kFlagU = 0x20; // Unused (always 1 on push)
static constexpr uint8_t kFlagV = 0x40; // Overflow
static constexpr uint8_t kFlagN = 0x80; // Negative

RP2A03::RP2A03(NESMemory& memory) : mem_(memory) {}

void RP2A03::Reset() {
    a_  = 0;
    x_  = 0;
    y_  = 0;
    s_  = 0xFD;
    p_  = kFlagU | kFlagI;
    // Read the reset vector from $FFFC/$FFFD
    pc_ = Read16(kResetVector);
    nmiPending_  = false;
    irqAsserted_ = false;
}

// ── Interrupt lines ───────────────────────────────────────────────────────

void RP2A03::SetNMI(bool active) {
    if (active && !nmiPending_) {
        nmiPending_ = true;
    }
}

void RP2A03::SetIRQ(bool active) {
    irqAsserted_ = active;
}

// ── Execution ─────────────────────────────────────────────────────────────

int RP2A03::Step() {
    // Service NMI (edge-triggered, highest priority).
    if (nmiPending_) {
        nmiPending_ = false;
        ServiceNMI();
        return 7;
    }
    // Service IRQ (level-triggered, masked by I flag).
    if (irqAsserted_ && !GetFlag(kFlagI)) {
        ServiceIRQ();
        return 7;
    }

    const uint8_t opcode = Read8(pc_++);
    int cycles = 0;

    switch (opcode) {
        // ── LDA ──────────────────────────────────────────────────────────
        case 0xA9: { auto e = AddrImmediate();        cycles = 2; ExecLDA(e); break; }
        case 0xA5: { auto e = AddrZeroPage();         cycles = 3; ExecLDA(e); break; }
        case 0xB5: { auto e = AddrZeroPageX();        cycles = 4; ExecLDA(e); break; }
        case 0xAD: { auto e = AddrAbsolute();         cycles = 4; ExecLDA(e); break; }
        case 0xBD: { auto [e,x] = AddrAbsoluteX();   cycles = 4 + x; ExecLDA(e); break; }
        case 0xB9: { auto [e,x] = AddrAbsoluteY();   cycles = 4 + x; ExecLDA(e); break; }
        case 0xA1: { auto e = AddrIndirectX();        cycles = 6; ExecLDA(e); break; }
        case 0xB1: { auto [e,x] = AddrIndirectY();   cycles = 5 + x; ExecLDA(e); break; }
        // ── LDX ──────────────────────────────────────────────────────────
        case 0xA2: { auto e = AddrImmediate();        cycles = 2; ExecLDX(e); break; }
        case 0xA6: { auto e = AddrZeroPage();         cycles = 3; ExecLDX(e); break; }
        case 0xB6: { auto e = AddrZeroPageY();        cycles = 4; ExecLDX(e); break; }
        case 0xAE: { auto e = AddrAbsolute();         cycles = 4; ExecLDX(e); break; }
        case 0xBE: { auto [e,x] = AddrAbsoluteY();   cycles = 4 + x; ExecLDX(e); break; }
        // ── LDY ──────────────────────────────────────────────────────────
        case 0xA0: { auto e = AddrImmediate();        cycles = 2; ExecLDY(e); break; }
        case 0xA4: { auto e = AddrZeroPage();         cycles = 3; ExecLDY(e); break; }
        case 0xB4: { auto e = AddrZeroPageX();        cycles = 4; ExecLDY(e); break; }
        case 0xAC: { auto e = AddrAbsolute();         cycles = 4; ExecLDY(e); break; }
        case 0xBC: { auto [e,x] = AddrAbsoluteX();   cycles = 4 + x; ExecLDY(e); break; }
        // ── STA ──────────────────────────────────────────────────────────
        case 0x85: { auto e = AddrZeroPage();         cycles = 3; ExecSTA(e); break; }
        case 0x95: { auto e = AddrZeroPageX();        cycles = 4; ExecSTA(e); break; }
        case 0x8D: { auto e = AddrAbsolute();         cycles = 4; ExecSTA(e); break; }
        case 0x9D: { auto [e,x] = AddrAbsoluteX();   cycles = 5; ExecSTA(e); (void)x; break; }
        case 0x99: { auto [e,x] = AddrAbsoluteY();   cycles = 5; ExecSTA(e); (void)x; break; }
        case 0x81: { auto e = AddrIndirectX();        cycles = 6; ExecSTA(e); break; }
        case 0x91: { auto [e,x] = AddrIndirectY();   cycles = 6; ExecSTA(e); (void)x; break; }
        // ── STX / STY ────────────────────────────────────────────────────
        case 0x86: { auto e = AddrZeroPage();         cycles = 3; ExecSTX(e); break; }
        case 0x96: { auto e = AddrZeroPageY();        cycles = 4; ExecSTX(e); break; }
        case 0x8E: { auto e = AddrAbsolute();         cycles = 4; ExecSTX(e); break; }
        case 0x84: { auto e = AddrZeroPage();         cycles = 3; ExecSTY(e); break; }
        case 0x94: { auto e = AddrZeroPageX();        cycles = 4; ExecSTY(e); break; }
        case 0x8C: { auto e = AddrAbsolute();         cycles = 4; ExecSTY(e); break; }
        // ── Transfers ────────────────────────────────────────────────────
        case 0xAA: cycles = 2; ExecTAX(); break;
        case 0xA8: cycles = 2; ExecTAY(); break;
        case 0xBA: cycles = 2; ExecTSX(); break;
        case 0x8A: cycles = 2; ExecTXA(); break;
        case 0x9A: cycles = 2; ExecTXS(); break;
        case 0x98: cycles = 2; ExecTYA(); break;
        // ── Stack ────────────────────────────────────────────────────────
        case 0x48: cycles = 3; ExecPHA(); break;
        case 0x08: cycles = 3; ExecPHP(); break;
        case 0x68: cycles = 4; ExecPLA(); break;
        case 0x28: cycles = 4; ExecPLP(); break;
        // ── ADC ──────────────────────────────────────────────────────────
        case 0x69: { auto e = AddrImmediate();        cycles = 2; ExecADC(e); break; }
        case 0x65: { auto e = AddrZeroPage();         cycles = 3; ExecADC(e); break; }
        case 0x75: { auto e = AddrZeroPageX();        cycles = 4; ExecADC(e); break; }
        case 0x6D: { auto e = AddrAbsolute();         cycles = 4; ExecADC(e); break; }
        case 0x7D: { auto [e,x] = AddrAbsoluteX();   cycles = 4 + x; ExecADC(e); break; }
        case 0x79: { auto [e,x] = AddrAbsoluteY();   cycles = 4 + x; ExecADC(e); break; }
        case 0x61: { auto e = AddrIndirectX();        cycles = 6; ExecADC(e); break; }
        case 0x71: { auto [e,x] = AddrIndirectY();   cycles = 5 + x; ExecADC(e); break; }
        // ── SBC ──────────────────────────────────────────────────────────
        case 0xE9: { auto e = AddrImmediate();        cycles = 2; ExecSBC(e); break; }
        case 0xE5: { auto e = AddrZeroPage();         cycles = 3; ExecSBC(e); break; }
        case 0xF5: { auto e = AddrZeroPageX();        cycles = 4; ExecSBC(e); break; }
        case 0xED: { auto e = AddrAbsolute();         cycles = 4; ExecSBC(e); break; }
        case 0xFD: { auto [e,x] = AddrAbsoluteX();   cycles = 4 + x; ExecSBC(e); break; }
        case 0xF9: { auto [e,x] = AddrAbsoluteY();   cycles = 4 + x; ExecSBC(e); break; }
        case 0xE1: { auto e = AddrIndirectX();        cycles = 6; ExecSBC(e); break; }
        case 0xF1: { auto [e,x] = AddrIndirectY();   cycles = 5 + x; ExecSBC(e); break; }
        // ── AND ──────────────────────────────────────────────────────────
        case 0x29: { auto e = AddrImmediate();        cycles = 2; ExecAND(e); break; }
        case 0x25: { auto e = AddrZeroPage();         cycles = 3; ExecAND(e); break; }
        case 0x35: { auto e = AddrZeroPageX();        cycles = 4; ExecAND(e); break; }
        case 0x2D: { auto e = AddrAbsolute();         cycles = 4; ExecAND(e); break; }
        case 0x3D: { auto [e,x] = AddrAbsoluteX();   cycles = 4 + x; ExecAND(e); break; }
        case 0x39: { auto [e,x] = AddrAbsoluteY();   cycles = 4 + x; ExecAND(e); break; }
        case 0x21: { auto e = AddrIndirectX();        cycles = 6; ExecAND(e); break; }
        case 0x31: { auto [e,x] = AddrIndirectY();   cycles = 5 + x; ExecAND(e); break; }
        // ── EOR ──────────────────────────────────────────────────────────
        case 0x49: { auto e = AddrImmediate();        cycles = 2; ExecEOR(e); break; }
        case 0x45: { auto e = AddrZeroPage();         cycles = 3; ExecEOR(e); break; }
        case 0x55: { auto e = AddrZeroPageX();        cycles = 4; ExecEOR(e); break; }
        case 0x4D: { auto e = AddrAbsolute();         cycles = 4; ExecEOR(e); break; }
        case 0x5D: { auto [e,x] = AddrAbsoluteX();   cycles = 4 + x; ExecEOR(e); break; }
        case 0x59: { auto [e,x] = AddrAbsoluteY();   cycles = 4 + x; ExecEOR(e); break; }
        case 0x41: { auto e = AddrIndirectX();        cycles = 6; ExecEOR(e); break; }
        case 0x51: { auto [e,x] = AddrIndirectY();   cycles = 5 + x; ExecEOR(e); break; }
        // ── ORA ──────────────────────────────────────────────────────────
        case 0x09: { auto e = AddrImmediate();        cycles = 2; ExecORA(e); break; }
        case 0x05: { auto e = AddrZeroPage();         cycles = 3; ExecORA(e); break; }
        case 0x15: { auto e = AddrZeroPageX();        cycles = 4; ExecORA(e); break; }
        case 0x0D: { auto e = AddrAbsolute();         cycles = 4; ExecORA(e); break; }
        case 0x1D: { auto [e,x] = AddrAbsoluteX();   cycles = 4 + x; ExecORA(e); break; }
        case 0x19: { auto [e,x] = AddrAbsoluteY();   cycles = 4 + x; ExecORA(e); break; }
        case 0x01: { auto e = AddrIndirectX();        cycles = 6; ExecORA(e); break; }
        case 0x11: { auto [e,x] = AddrIndirectY();   cycles = 5 + x; ExecORA(e); break; }
        // ── BIT ──────────────────────────────────────────────────────────
        case 0x24: { auto e = AddrZeroPage();         cycles = 3; ExecBIT(e); break; }
        case 0x2C: { auto e = AddrAbsolute();         cycles = 4; ExecBIT(e); break; }
        // ── Shifts/Rotates — Accumulator ─────────────────────────────────
        case 0x0A: cycles = 2; ExecASL_Acc(); break;
        case 0x4A: cycles = 2; ExecLSR_Acc(); break;
        case 0x2A: cycles = 2; ExecROL_Acc(); break;
        case 0x6A: cycles = 2; ExecROR_Acc(); break;
        // ── ASL memory ───────────────────────────────────────────────────
        case 0x06: { auto e = AddrZeroPage();         cycles = 5; ExecASL_Mem(e); break; }
        case 0x16: { auto e = AddrZeroPageX();        cycles = 6; ExecASL_Mem(e); break; }
        case 0x0E: { auto e = AddrAbsolute();         cycles = 6; ExecASL_Mem(e); break; }
        case 0x1E: { auto [e,x] = AddrAbsoluteX();   cycles = 7; ExecASL_Mem(e); (void)x; break; }
        // ── LSR memory ───────────────────────────────────────────────────
        case 0x46: { auto e = AddrZeroPage();         cycles = 5; ExecLSR_Mem(e); break; }
        case 0x56: { auto e = AddrZeroPageX();        cycles = 6; ExecLSR_Mem(e); break; }
        case 0x4E: { auto e = AddrAbsolute();         cycles = 6; ExecLSR_Mem(e); break; }
        case 0x5E: { auto [e,x] = AddrAbsoluteX();   cycles = 7; ExecLSR_Mem(e); (void)x; break; }
        // ── ROL memory ───────────────────────────────────────────────────
        case 0x26: { auto e = AddrZeroPage();         cycles = 5; ExecROL_Mem(e); break; }
        case 0x36: { auto e = AddrZeroPageX();        cycles = 6; ExecROL_Mem(e); break; }
        case 0x2E: { auto e = AddrAbsolute();         cycles = 6; ExecROL_Mem(e); break; }
        case 0x3E: { auto [e,x] = AddrAbsoluteX();   cycles = 7; ExecROL_Mem(e); (void)x; break; }
        // ── ROR memory ───────────────────────────────────────────────────
        case 0x66: { auto e = AddrZeroPage();         cycles = 5; ExecROR_Mem(e); break; }
        case 0x76: { auto e = AddrZeroPageX();        cycles = 6; ExecROR_Mem(e); break; }
        case 0x6E: { auto e = AddrAbsolute();         cycles = 6; ExecROR_Mem(e); break; }
        case 0x7E: { auto [e,x] = AddrAbsoluteX();   cycles = 7; ExecROR_Mem(e); (void)x; break; }
        // ── INC / DEC ────────────────────────────────────────────────────
        case 0xE6: { auto e = AddrZeroPage();         cycles = 5; ExecINC(e); break; }
        case 0xF6: { auto e = AddrZeroPageX();        cycles = 6; ExecINC(e); break; }
        case 0xEE: { auto e = AddrAbsolute();         cycles = 6; ExecINC(e); break; }
        case 0xFE: { auto [e,x] = AddrAbsoluteX();   cycles = 7; ExecINC(e); (void)x; break; }
        case 0xC6: { auto e = AddrZeroPage();         cycles = 5; ExecDEC(e); break; }
        case 0xD6: { auto e = AddrZeroPageX();        cycles = 6; ExecDEC(e); break; }
        case 0xCE: { auto e = AddrAbsolute();         cycles = 6; ExecDEC(e); break; }
        case 0xDE: { auto [e,x] = AddrAbsoluteX();   cycles = 7; ExecDEC(e); (void)x; break; }
        case 0xE8: cycles = 2; ExecINX(); break;
        case 0xC8: cycles = 2; ExecINY(); break;
        case 0xCA: cycles = 2; ExecDEX(); break;
        case 0x88: cycles = 2; ExecDEY(); break;
        // ── CMP / CPX / CPY ──────────────────────────────────────────────
        case 0xC9: { auto e = AddrImmediate();        cycles = 2; ExecCMP(e); break; }
        case 0xC5: { auto e = AddrZeroPage();         cycles = 3; ExecCMP(e); break; }
        case 0xD5: { auto e = AddrZeroPageX();        cycles = 4; ExecCMP(e); break; }
        case 0xCD: { auto e = AddrAbsolute();         cycles = 4; ExecCMP(e); break; }
        case 0xDD: { auto [e,x] = AddrAbsoluteX();   cycles = 4 + x; ExecCMP(e); break; }
        case 0xD9: { auto [e,x] = AddrAbsoluteY();   cycles = 4 + x; ExecCMP(e); break; }
        case 0xC1: { auto e = AddrIndirectX();        cycles = 6; ExecCMP(e); break; }
        case 0xD1: { auto [e,x] = AddrIndirectY();   cycles = 5 + x; ExecCMP(e); break; }
        case 0xE0: { auto e = AddrImmediate();        cycles = 2; ExecCPX(e); break; }
        case 0xE4: { auto e = AddrZeroPage();         cycles = 3; ExecCPX(e); break; }
        case 0xEC: { auto e = AddrAbsolute();         cycles = 4; ExecCPX(e); break; }
        case 0xC0: { auto e = AddrImmediate();        cycles = 2; ExecCPY(e); break; }
        case 0xC4: { auto e = AddrZeroPage();         cycles = 3; ExecCPY(e); break; }
        case 0xCC: { auto e = AddrAbsolute();         cycles = 4; ExecCPY(e); break; }
        // ── Branches ─────────────────────────────────────────────────────
        case 0x90: cycles = 2 + ExecBCC(); break;
        case 0xB0: cycles = 2 + ExecBCS(); break;
        case 0xF0: cycles = 2 + ExecBEQ(); break;
        case 0x30: cycles = 2 + ExecBMI(); break;
        case 0xD0: cycles = 2 + ExecBNE(); break;
        case 0x10: cycles = 2 + ExecBPL(); break;
        case 0x50: cycles = 2 + ExecBVC(); break;
        case 0x70: cycles = 2 + ExecBVS(); break;
        // ── Jumps ────────────────────────────────────────────────────────
        case 0x4C: { auto e = AddrAbsolute();         cycles = 3; ExecJMP(e); break; }
        case 0x6C: { auto e = AddrIndirect();         cycles = 5; ExecJMP(e); break; }
        case 0x20: cycles = 6; ExecJSR(); break;
        case 0x40: cycles = 6; ExecRTI(); break;
        case 0x60: cycles = 6; ExecRTS(); break;
        // ── Flag instructions ─────────────────────────────────────────────
        case 0x18: cycles = 2; ExecCLC(); break;
        case 0xD8: cycles = 2; ExecCLD(); break;
        case 0x58: cycles = 2; ExecCLI(); break;
        case 0xB8: cycles = 2; ExecCLV(); break;
        case 0x38: cycles = 2; ExecSEC(); break;
        case 0xF8: cycles = 2; ExecSED(); break;
        case 0x78: cycles = 2; ExecSEI(); break;
        // ── BRK ──────────────────────────────────────────────────────────
        case 0x00: cycles = 7; ExecBRK(); break;
        // ── NOP (official + common unofficials) ──────────────────────────
        case 0xEA:
        case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA:
            cycles = 2; ExecNOP(); break;
        // Unofficial 2-byte NOPs (skip 1 byte, 2 cycles, dummy read)
        case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2:
            pc_++;                          // consume the immediate byte
            cycles = 2; break;
        // Unofficial 3-byte NOPs — absolute (4 cycles)
        case 0x0C:
            pc_ += 2; cycles = 4; break;
        // Unofficial 3-byte NOPs — absolute,X (4+1 page-cross cycles)
        case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC: {
            auto [e, x] = AddrAbsoluteX();
            (void)e;
            cycles = 4 + x;
            break;
        }
        // KIL / JAM — halt the CPU (real hardware locks up; stub as 2-cycle NOP)
        case 0x02: case 0x12: case 0x22: case 0x32:
        case 0x42: case 0x52: case 0x62: case 0x72:
        case 0x92: case 0xB2: case 0xD2: case 0xF2:
            cycles = 2; break;
        // ── Undocumented: SLO (ASL + ORA) ───────────────────────────────
        case 0x07: { auto e = AddrZeroPage();                   cycles = 5; ExecSLO(e); break; }
        case 0x17: { auto e = AddrZeroPageX();                  cycles = 6; ExecSLO(e); break; }
        case 0x0F: { auto e = AddrAbsolute();                   cycles = 6; ExecSLO(e); break; }
        case 0x1F: { auto [e,x] = AddrAbsoluteX(); (void)x;    cycles = 7; ExecSLO(e); break; }
        case 0x1B: { auto [e,x] = AddrAbsoluteY(); (void)x;    cycles = 7; ExecSLO(e); break; }
        case 0x03: { auto e = AddrIndirectX();                  cycles = 8; ExecSLO(e); break; }
        case 0x13: { auto [e,x] = AddrIndirectY(); (void)x;    cycles = 8; ExecSLO(e); break; }
        // ── Undocumented: RLA (ROL + AND) ───────────────────────────────
        case 0x27: { auto e = AddrZeroPage();                   cycles = 5; ExecRLA(e); break; }
        case 0x37: { auto e = AddrZeroPageX();                  cycles = 6; ExecRLA(e); break; }
        case 0x2F: { auto e = AddrAbsolute();                   cycles = 6; ExecRLA(e); break; }
        case 0x3F: { auto [e,x] = AddrAbsoluteX(); (void)x;    cycles = 7; ExecRLA(e); break; }
        case 0x3B: { auto [e,x] = AddrAbsoluteY(); (void)x;    cycles = 7; ExecRLA(e); break; }
        case 0x23: { auto e = AddrIndirectX();                  cycles = 8; ExecRLA(e); break; }
        case 0x33: { auto [e,x] = AddrIndirectY(); (void)x;    cycles = 8; ExecRLA(e); break; }
        // ── Undocumented: SRE (LSR + EOR) ───────────────────────────────
        case 0x47: { auto e = AddrZeroPage();                   cycles = 5; ExecSRE(e); break; }
        case 0x57: { auto e = AddrZeroPageX();                  cycles = 6; ExecSRE(e); break; }
        case 0x4F: { auto e = AddrAbsolute();                   cycles = 6; ExecSRE(e); break; }
        case 0x5F: { auto [e,x] = AddrAbsoluteX(); (void)x;    cycles = 7; ExecSRE(e); break; }
        case 0x5B: { auto [e,x] = AddrAbsoluteY(); (void)x;    cycles = 7; ExecSRE(e); break; }
        case 0x43: { auto e = AddrIndirectX();                  cycles = 8; ExecSRE(e); break; }
        case 0x53: { auto [e,x] = AddrIndirectY(); (void)x;    cycles = 8; ExecSRE(e); break; }
        // ── Undocumented: RRA (ROR + ADC) ───────────────────────────────
        case 0x67: { auto e = AddrZeroPage();                   cycles = 5; ExecRRA(e); break; }
        case 0x77: { auto e = AddrZeroPageX();                  cycles = 6; ExecRRA(e); break; }
        case 0x6F: { auto e = AddrAbsolute();                   cycles = 6; ExecRRA(e); break; }
        case 0x7F: { auto [e,x] = AddrAbsoluteX(); (void)x;    cycles = 7; ExecRRA(e); break; }
        case 0x7B: { auto [e,x] = AddrAbsoluteY(); (void)x;    cycles = 7; ExecRRA(e); break; }
        case 0x63: { auto e = AddrIndirectX();                  cycles = 8; ExecRRA(e); break; }
        case 0x73: { auto [e,x] = AddrIndirectY(); (void)x;    cycles = 8; ExecRRA(e); break; }
        // ── Undocumented: SAX (store A & X) ─────────────────────────────
        case 0x87: { auto e = AddrZeroPage();                   cycles = 3; ExecSAX(e); break; }
        case 0x97: { auto e = AddrZeroPageY();                  cycles = 4; ExecSAX(e); break; }
        case 0x8F: { auto e = AddrAbsolute();                   cycles = 4; ExecSAX(e); break; }
        case 0x83: { auto e = AddrIndirectX();                  cycles = 6; ExecSAX(e); break; }
        // ── Undocumented: LAX (load A and X) ────────────────────────────
        case 0xA7: { auto e = AddrZeroPage();                   cycles = 3; ExecLAX(e); break; }
        case 0xB7: { auto e = AddrZeroPageY();                  cycles = 4; ExecLAX(e); break; }
        case 0xAF: { auto e = AddrAbsolute();                   cycles = 4; ExecLAX(e); break; }
        case 0xBF: { auto [e,x] = AddrAbsoluteY();             cycles = 4 + x; ExecLAX(e); break; }
        case 0xA3: { auto e = AddrIndirectX();                  cycles = 6; ExecLAX(e); break; }
        case 0xB3: { auto [e,x] = AddrIndirectY();             cycles = 5 + x; ExecLAX(e); break; }
        // ── Undocumented: DCP (DEC + CMP) ───────────────────────────────
        case 0xC7: { auto e = AddrZeroPage();                   cycles = 5; ExecDCP(e); break; }
        case 0xD7: { auto e = AddrZeroPageX();                  cycles = 6; ExecDCP(e); break; }
        case 0xCF: { auto e = AddrAbsolute();                   cycles = 6; ExecDCP(e); break; }
        case 0xDF: { auto [e,x] = AddrAbsoluteX(); (void)x;    cycles = 7; ExecDCP(e); break; }
        case 0xDB: { auto [e,x] = AddrAbsoluteY(); (void)x;    cycles = 7; ExecDCP(e); break; }
        case 0xC3: { auto e = AddrIndirectX();                  cycles = 8; ExecDCP(e); break; }
        case 0xD3: { auto [e,x] = AddrIndirectY(); (void)x;    cycles = 8; ExecDCP(e); break; }
        // ── Undocumented: ISC/ISB (INC + SBC) ───────────────────────────
        case 0xE7: { auto e = AddrZeroPage();                   cycles = 5; ExecISC(e); break; }
        case 0xF7: { auto e = AddrZeroPageX();                  cycles = 6; ExecISC(e); break; }
        case 0xEF: { auto e = AddrAbsolute();                   cycles = 6; ExecISC(e); break; }
        case 0xFF: { auto [e,x] = AddrAbsoluteX(); (void)x;    cycles = 7; ExecISC(e); break; }
        case 0xFB: { auto [e,x] = AddrAbsoluteY(); (void)x;    cycles = 7; ExecISC(e); break; }
        case 0xE3: { auto e = AddrIndirectX();                  cycles = 8; ExecISC(e); break; }
        case 0xF3: { auto [e,x] = AddrIndirectY(); (void)x;    cycles = 8; ExecISC(e); break; }
        // ── Undocumented: one-byte immediate combiners ──────────────────
        case 0x0B: case 0x2B: cycles = 2; ExecANC(); break;  // ANC
        case 0x4B:             cycles = 2; ExecALR(); break;  // ALR
        case 0x6B:             cycles = 2; ExecARR(); break;  // ARR
        case 0xCB:             cycles = 2; ExecAXS(); break;  // AXS / SBX
        case 0xEB:             { auto e = AddrImmediate(); cycles = 2; ExecSBC(e); break; } // SBC dup
        // ── Unknown opcode: treat as NOP(2) ──────────────────────────────
        default:
            cycles = 2;
            break;
    }

    return cycles;
}

// ── Memory helpers ────────────────────────────────────────────────────────

uint8_t RP2A03::Read8(uint16_t addr) const {
    return mem_.Read8(static_cast<uint32_t>(addr));
}

uint16_t RP2A03::Read16(uint16_t addr) const {
    const uint8_t lo = Read8(addr);
    const uint8_t hi = Read8(addr + 1);
    return static_cast<uint16_t>((hi << 8) | lo);
}

void RP2A03::Push8(uint8_t v) {
    mem_.Write8(0x0100u | s_--, v);
}

void RP2A03::Push16(uint16_t v) {
    Push8(static_cast<uint8_t>(v >> 8));
    Push8(static_cast<uint8_t>(v & 0xFF));
}

uint8_t RP2A03::Pop8() {
    return Read8(0x0100u | ++s_);
}

uint16_t RP2A03::Pop16() {
    const uint8_t lo = Pop8();
    const uint8_t hi = Pop8();
    return static_cast<uint16_t>((hi << 8) | lo);
}

// ── Flag helpers ──────────────────────────────────────────────────────────

void RP2A03::SetFlag(uint8_t mask, bool value) noexcept {
    if (value) p_ |= mask; else p_ &= ~mask;
}

void RP2A03::UpdateNZ(uint8_t value) noexcept {
    SetFlag(kFlagZ, value == 0);
    SetFlag(kFlagN, (value & 0x80) != 0);
}

// ── Address-mode resolvers ────────────────────────────────────────────────

uint16_t RP2A03::AddrImmediate() noexcept  { return pc_++; }
uint16_t RP2A03::AddrZeroPage()  noexcept  { return Read8(pc_++); }
uint16_t RP2A03::AddrZeroPageX() noexcept  { return (Read8(pc_++) + x_) & 0xFF; }
uint16_t RP2A03::AddrZeroPageY() noexcept  { return (Read8(pc_++) + y_) & 0xFF; }
uint16_t RP2A03::AddrAbsolute()  noexcept  {
    const uint16_t addr = Read16(pc_);
    pc_ += 2;
    return addr;
}

std::pair<uint16_t,bool> RP2A03::AddrAbsoluteX() noexcept {
    const uint16_t base = Read16(pc_); pc_ += 2;
    const uint16_t eff  = base + x_;
    return { eff, (base & 0xFF00) != (eff & 0xFF00) };
}

std::pair<uint16_t,bool> RP2A03::AddrAbsoluteY() noexcept {
    const uint16_t base = Read16(pc_); pc_ += 2;
    const uint16_t eff  = base + y_;
    return { eff, (base & 0xFF00) != (eff & 0xFF00) };
}

uint16_t RP2A03::AddrIndirect() noexcept {
    const uint16_t ptr = Read16(pc_); pc_ += 2;
    // Hardware bug: page boundary wraps within the same page.
    const uint8_t lo = Read8(ptr);
    const uint8_t hi = Read8((ptr & 0xFF00) | ((ptr + 1) & 0x00FF));
    return static_cast<uint16_t>((hi << 8) | lo);
}

uint16_t RP2A03::AddrIndirectX() noexcept {
    const uint8_t zp = (Read8(pc_++) + x_) & 0xFF;
    const uint8_t lo = Read8(zp);
    const uint8_t hi = Read8((zp + 1) & 0xFF);
    return static_cast<uint16_t>((hi << 8) | lo);
}

std::pair<uint16_t,bool> RP2A03::AddrIndirectY() noexcept {
    const uint8_t zp = Read8(pc_++);
    const uint8_t lo = Read8(zp);
    const uint8_t hi = Read8((zp + 1) & 0xFF);
    const uint16_t base = static_cast<uint16_t>((hi << 8) | lo);
    const uint16_t eff  = base + y_;
    return { eff, (base & 0xFF00) != (eff & 0xFF00) };
}

int8_t RP2A03::AddrRelative() noexcept {
    return static_cast<int8_t>(Read8(pc_++));
}

// ── Interrupt service ─────────────────────────────────────────────────────

void RP2A03::ServiceNMI() {
    Push16(pc_);
    Push8((p_ | kFlagU) & ~kFlagB);
    SetFlag(kFlagI, true);
    pc_ = Read16(kNmiVector);
}

void RP2A03::ServiceIRQ() {
    Push16(pc_);
    Push8((p_ | kFlagU) & ~kFlagB);
    SetFlag(kFlagI, true);
    pc_ = Read16(kIrqBrkVector);
}

// ── Instruction implementations ───────────────────────────────────────────

int RP2A03::ExecLDA(uint16_t addr) { a_ = Read8(addr); UpdateNZ(a_); return 0; }
int RP2A03::ExecLDX(uint16_t addr) { x_ = Read8(addr); UpdateNZ(x_); return 0; }
int RP2A03::ExecLDY(uint16_t addr) { y_ = Read8(addr); UpdateNZ(y_); return 0; }
int RP2A03::ExecSTA(uint16_t addr) { mem_.Write8(addr, a_); return 0; }
int RP2A03::ExecSTX(uint16_t addr) { mem_.Write8(addr, x_); return 0; }
int RP2A03::ExecSTY(uint16_t addr) { mem_.Write8(addr, y_); return 0; }

int RP2A03::ExecTAX() { x_ = a_; UpdateNZ(x_); return 0; }
int RP2A03::ExecTAY() { y_ = a_; UpdateNZ(y_); return 0; }
int RP2A03::ExecTXA() { a_ = x_; UpdateNZ(a_); return 0; }
int RP2A03::ExecTYA() { a_ = y_; UpdateNZ(a_); return 0; }
int RP2A03::ExecTSX() { x_ = s_; UpdateNZ(x_); return 0; }
int RP2A03::ExecTXS() { s_ = x_; return 0; }

int RP2A03::ExecPHA() { Push8(a_); return 0; }
int RP2A03::ExecPHP() { Push8(p_ | kFlagU | kFlagB); return 0; }
int RP2A03::ExecPLA() { a_ = Pop8(); UpdateNZ(a_); return 0; }
int RP2A03::ExecPLP() { p_ = (Pop8() & ~kFlagB) | kFlagU; return 0; }

int RP2A03::ExecADC(uint16_t addr) {
    const uint8_t  m   = Read8(addr);
    const uint16_t res = a_ + m + (GetFlag(kFlagC) ? 1u : 0u);
    SetFlag(kFlagV, (~(a_ ^ m) & (a_ ^ res) & 0x80) != 0);
    SetFlag(kFlagC, res > 0xFF);
    a_ = static_cast<uint8_t>(res);
    UpdateNZ(a_);
    return 0;
}

int RP2A03::ExecSBC(uint16_t addr) {
    const uint8_t  m   = Read8(addr) ^ 0xFF;
    const uint16_t res = a_ + m + (GetFlag(kFlagC) ? 1u : 0u);
    SetFlag(kFlagV, (~(a_ ^ m) & (a_ ^ res) & 0x80) != 0);
    SetFlag(kFlagC, res > 0xFF);
    a_ = static_cast<uint8_t>(res);
    UpdateNZ(a_);
    return 0;
}

int RP2A03::ExecAND(uint16_t addr) { a_ &= Read8(addr); UpdateNZ(a_); return 0; }
int RP2A03::ExecEOR(uint16_t addr) { a_ ^= Read8(addr); UpdateNZ(a_); return 0; }
int RP2A03::ExecORA(uint16_t addr) { a_ |= Read8(addr); UpdateNZ(a_); return 0; }

int RP2A03::ExecBIT(uint16_t addr) {
    const uint8_t m = Read8(addr);
    SetFlag(kFlagZ, (a_ & m) == 0);
    SetFlag(kFlagV, (m & 0x40) != 0);
    SetFlag(kFlagN, (m & 0x80) != 0);
    return 0;
}

int RP2A03::ExecASL_Acc() {
    SetFlag(kFlagC, (a_ & 0x80) != 0);
    a_ <<= 1;
    UpdateNZ(a_);
    return 0;
}

int RP2A03::ExecASL_Mem(uint16_t addr) {
    uint8_t m = Read8(addr);
    SetFlag(kFlagC, (m & 0x80) != 0);
    m <<= 1;
    mem_.Write8(addr, m);
    UpdateNZ(m);
    return 0;
}

int RP2A03::ExecLSR_Acc() {
    SetFlag(kFlagC, (a_ & 0x01) != 0);
    a_ >>= 1;
    UpdateNZ(a_);
    return 0;
}

int RP2A03::ExecLSR_Mem(uint16_t addr) {
    uint8_t m = Read8(addr);
    SetFlag(kFlagC, (m & 0x01) != 0);
    m >>= 1;
    mem_.Write8(addr, m);
    UpdateNZ(m);
    return 0;
}

int RP2A03::ExecROL_Acc() {
    const uint8_t old = a_;
    a_ = static_cast<uint8_t>((old << 1) | (GetFlag(kFlagC) ? 1 : 0));
    SetFlag(kFlagC, (old & 0x80) != 0);
    UpdateNZ(a_);
    return 0;
}

int RP2A03::ExecROL_Mem(uint16_t addr) {
    const uint8_t old = Read8(addr);
    const uint8_t res = static_cast<uint8_t>((old << 1) | (GetFlag(kFlagC) ? 1 : 0));
    SetFlag(kFlagC, (old & 0x80) != 0);
    mem_.Write8(addr, res);
    UpdateNZ(res);
    return 0;
}

int RP2A03::ExecROR_Acc() {
    const uint8_t old = a_;
    a_ = static_cast<uint8_t>((old >> 1) | (GetFlag(kFlagC) ? 0x80 : 0));
    SetFlag(kFlagC, (old & 0x01) != 0);
    UpdateNZ(a_);
    return 0;
}

int RP2A03::ExecROR_Mem(uint16_t addr) {
    const uint8_t old = Read8(addr);
    const uint8_t res = static_cast<uint8_t>((old >> 1) | (GetFlag(kFlagC) ? 0x80 : 0));
    SetFlag(kFlagC, (old & 0x01) != 0);
    mem_.Write8(addr, res);
    UpdateNZ(res);
    return 0;
}

int RP2A03::ExecINC(uint16_t addr) {
    const uint8_t res = Read8(addr) + 1u;
    mem_.Write8(addr, res);
    UpdateNZ(res);
    return 0;
}

int RP2A03::ExecDEC(uint16_t addr) {
    const uint8_t res = Read8(addr) - 1u;
    mem_.Write8(addr, res);
    UpdateNZ(res);
    return 0;
}

int RP2A03::ExecINX() { ++x_; UpdateNZ(x_); return 0; }
int RP2A03::ExecINY() { ++y_; UpdateNZ(y_); return 0; }
int RP2A03::ExecDEX() { --x_; UpdateNZ(x_); return 0; }
int RP2A03::ExecDEY() { --y_; UpdateNZ(y_); return 0; }

int RP2A03::ExecCMP(uint16_t addr) {
    const uint8_t m = Read8(addr);
    SetFlag(kFlagC, a_ >= m);
    UpdateNZ(static_cast<uint8_t>(a_ - m));
    return 0;
}

int RP2A03::ExecCPX(uint16_t addr) {
    const uint8_t m = Read8(addr);
    SetFlag(kFlagC, x_ >= m);
    UpdateNZ(static_cast<uint8_t>(x_ - m));
    return 0;
}

int RP2A03::ExecCPY(uint16_t addr) {
    const uint8_t m = Read8(addr);
    SetFlag(kFlagC, y_ >= m);
    UpdateNZ(static_cast<uint8_t>(y_ - m));
    return 0;
}

int RP2A03::ExecJMP(uint16_t addr) { pc_ = addr; return 0; }

int RP2A03::ExecJSR() {
    const uint16_t target = Read16(pc_); pc_ += 2;
    Push16(pc_ - 1);
    pc_ = target;
    return 0;
}

int RP2A03::ExecRTS() {
    pc_ = Pop16() + 1;
    return 0;
}

int RP2A03::ExecRTI() {
    p_ = (Pop8() & ~kFlagB) | kFlagU;
    pc_ = Pop16();
    return 0;
}

int RP2A03::ExecBRK() {
    pc_++;
    Push16(pc_);
    Push8(p_ | kFlagU | kFlagB);
    SetFlag(kFlagI, true);
    pc_ = Read16(kIrqBrkVector);
    return 0;
}

int RP2A03::ExecNOP() { return 0; }

int RP2A03::ExecCLC() { SetFlag(kFlagC, false); return 0; }
int RP2A03::ExecCLD() { SetFlag(kFlagD, false); return 0; }
int RP2A03::ExecCLI() { SetFlag(kFlagI, false); return 0; }
int RP2A03::ExecCLV() { SetFlag(kFlagV, false); return 0; }
int RP2A03::ExecSEC() { SetFlag(kFlagC, true);  return 0; }
int RP2A03::ExecSED() { SetFlag(kFlagD, true);  return 0; }
int RP2A03::ExecSEI() { SetFlag(kFlagI, true);  return 0; }

// ── Branch helper ─────────────────────────────────────────────────────────

int RP2A03::Branch(bool condition) {
    const int8_t disp = AddrRelative();
    if (!condition) {
        return 0;
    }
    const uint16_t newPc = static_cast<uint16_t>(pc_ + disp);
    const int pageCross  = ((pc_ & 0xFF00) != (newPc & 0xFF00)) ? 1 : 0;
    pc_ = newPc;
    return 1 + pageCross;
}

int RP2A03::ExecBCC() { return Branch(!GetFlag(kFlagC)); }
int RP2A03::ExecBCS() { return Branch( GetFlag(kFlagC)); }
int RP2A03::ExecBEQ() { return Branch( GetFlag(kFlagZ)); }
int RP2A03::ExecBMI() { return Branch( GetFlag(kFlagN)); }
int RP2A03::ExecBNE() { return Branch(!GetFlag(kFlagZ)); }
int RP2A03::ExecBPL() { return Branch(!GetFlag(kFlagN)); }
int RP2A03::ExecBVC() { return Branch(!GetFlag(kFlagV)); }
int RP2A03::ExecBVS() { return Branch( GetFlag(kFlagV)); }

// ── Undocumented instruction implementations ──────────────────────────────
// Cycle counts are set in the switch above; these helpers only perform
// the register / memory side-effects and flags.

int RP2A03::ExecSLO(uint16_t addr) {
    // ASL on memory, then ORA A.
    uint8_t m = Read8(addr);
    SetFlag(kFlagC, (m & 0x80) != 0);
    m <<= 1;
    mem_.Write8(addr, m);
    a_ |= m;
    UpdateNZ(a_);
    return 0;
}

int RP2A03::ExecRLA(uint16_t addr) {
    // ROL on memory, then AND A.
    const uint8_t old = Read8(addr);
    const uint8_t res = static_cast<uint8_t>((old << 1) | (GetFlag(kFlagC) ? 1 : 0));
    SetFlag(kFlagC, (old & 0x80) != 0);
    mem_.Write8(addr, res);
    a_ &= res;
    UpdateNZ(a_);
    return 0;
}

int RP2A03::ExecSRE(uint16_t addr) {
    // LSR on memory, then EOR A.
    uint8_t m = Read8(addr);
    SetFlag(kFlagC, (m & 0x01) != 0);
    m >>= 1;
    mem_.Write8(addr, m);
    a_ ^= m;
    UpdateNZ(a_);
    return 0;
}

int RP2A03::ExecRRA(uint16_t addr) {
    // ROR on memory, then ADC A with result.
    const uint8_t old = Read8(addr);
    const uint8_t m   = static_cast<uint8_t>((old >> 1) | (GetFlag(kFlagC) ? 0x80 : 0));
    SetFlag(kFlagC, (old & 0x01) != 0);
    mem_.Write8(addr, m);
    // Re-use ExecADC logic inline to keep flags accurate.
    const uint16_t res = a_ + m + (GetFlag(kFlagC) ? 1u : 0u);
    SetFlag(kFlagV, (~(a_ ^ m) & (a_ ^ res) & 0x80) != 0);
    SetFlag(kFlagC, res > 0xFF);
    a_ = static_cast<uint8_t>(res);
    UpdateNZ(a_);
    return 0;
}

int RP2A03::ExecSAX(uint16_t addr) {
    // Store A & X; no flags affected.
    mem_.Write8(addr, a_ & x_);
    return 0;
}

int RP2A03::ExecLAX(uint16_t addr) {
    // Load A and X from same address.
    a_ = x_ = Read8(addr);
    UpdateNZ(a_);
    return 0;
}

int RP2A03::ExecDCP(uint16_t addr) {
    // DEC then CMP (unsigned compare, no V flag).
    const uint8_t m = Read8(addr) - 1u;
    mem_.Write8(addr, m);
    SetFlag(kFlagC, a_ >= m);
    UpdateNZ(static_cast<uint8_t>(a_ - m));
    return 0;
}

int RP2A03::ExecISC(uint16_t addr) {
    // INC then SBC.
    const uint8_t m = Read8(addr) + 1u;
    mem_.Write8(addr, m);
    // SBC is ADC with operand inverted.
    const uint8_t  inv = m ^ 0xFF;
    const uint16_t res = a_ + inv + (GetFlag(kFlagC) ? 1u : 0u);
    SetFlag(kFlagV, (~(a_ ^ inv) & (a_ ^ res) & 0x80) != 0);
    SetFlag(kFlagC, res > 0xFF);
    a_ = static_cast<uint8_t>(res);
    UpdateNZ(a_);
    return 0;
}

int RP2A03::ExecANC() {
    // AND A with #imm; carry = bit 7 of result (same as N).
    const uint8_t imm = Read8(AddrImmediate());
    a_ &= imm;
    UpdateNZ(a_);
    SetFlag(kFlagC, (a_ & 0x80) != 0);
    return 0;
}

int RP2A03::ExecALR() {
    // AND A with #imm, then LSR A.
    const uint8_t imm = Read8(AddrImmediate());
    a_ &= imm;
    SetFlag(kFlagC, (a_ & 0x01) != 0);
    a_ >>= 1;
    UpdateNZ(a_);
    return 0;
}

int RP2A03::ExecARR() {
    // AND A with #imm, then ROR A with special C and V behaviour.
    // Bit 6 after the ROR becomes C; V = (C ^ bit 5 after ROR).
    const uint8_t imm = Read8(AddrImmediate());
    a_ &= imm;
    a_ = static_cast<uint8_t>((a_ >> 1) | (GetFlag(kFlagC) ? 0x80 : 0));
    UpdateNZ(a_);
    SetFlag(kFlagC, (a_ & 0x40) != 0);
    SetFlag(kFlagV, (((a_ >> 6) ^ (a_ >> 5)) & 0x01) != 0);
    return 0;
}

int RP2A03::ExecAXS() {
    // X = (A & X) - #imm; flags N, Z, C set (no borrow added from A).
    const uint8_t imm = Read8(AddrImmediate());
    const uint8_t ax  = a_ & x_;
    SetFlag(kFlagC, ax >= imm);
    x_ = static_cast<uint8_t>(ax - imm);
    UpdateNZ(x_);
    return 0;
}

// ── Save state ────────────────────────────────────────────────────────────

void RP2A03::SaveState(Common::SaveStateWriter& w) const {
    w.WriteU16(pc_);
    w.WriteU8(a_);
    w.WriteU8(x_);
    w.WriteU8(y_);
    w.WriteU8(s_);
    w.WriteU8(p_);
    w.WriteBool(nmiPending_);
    w.WriteBool(irqAsserted_);
}

void RP2A03::LoadState(Common::SaveStateReader& r) {
    pc_          = r.ReadU16();
    a_           = r.ReadU8();
    x_           = r.ReadU8();
    y_           = r.ReadU8();
    s_           = r.ReadU8();
    p_           = r.ReadU8();
    nmiPending_  = r.ReadBool();
    irqAsserted_ = r.ReadBool();
}

} // namespace AIO::Emulator::NES
