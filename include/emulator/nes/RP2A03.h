#pragma once

#include "emulator/common/ISaveStateable.h"
#include <cstdint>
#include <utility>

namespace AIO::Emulator::NES {

class NESMemory;

/// Addressing modes supported by the 6502 / RP2A03.
enum class AddrMode : uint8_t {
    Implied,      // OPC         — no operand
    Accumulator,  // OPC A       — accumulator
    Immediate,    // OPC #nn     — literal byte follows
    ZeroPage,     // OPC nn      — address in zero page
    ZeroPageX,    // OPC nn,X
    ZeroPageY,    // OPC nn,Y
    Absolute,     // OPC nnnn
    AbsoluteX,    // OPC nnnn,X
    AbsoluteY,    // OPC nnnn,Y
    Indirect,     // OPC (nnnn)  — JMP only
    IndirectX,    // OPC (nn,X)  — pre-indexed
    IndirectY,    // OPC (nn),Y  — post-indexed
    Relative,     // OPC ±disp   — branch instructions
};

/// RP2A03 — the NES CPU.
///
/// Implements the full MOS 6502 instruction set with the following
/// hardware-accurate deviations:
///   - Decimal mode (BCD) is present in the status register bit but
///     has NO EFFECT on ADC/SBC output — the 2A03 strips the BCD circuit.
///   - The open-bus high byte of the program counter is observable on
///     the data bus during indirect fetch (undocumented behaviour).
///   - All page-crossing penalties and branch penalties are cycle-accurate
///     per the "WDC Programming the 65816" timing tables and NESDev wiki.
///
/// Ownership: NESMemory is held by NES and passed by reference. RP2A03
/// does not own the memory.
class RP2A03 : public AIO::Emulator::Common::ISaveStateable {
public:
    explicit RP2A03(NESMemory& memory);
    ~RP2A03() override = default;

    // ── Execution ─────────────────────────────────────────────────────────

    void Reset();

    /// Execute one instruction.
    /// @return CPU cycles consumed (1–7; branch/page-cross penalties included).
    [[nodiscard]] int Step();

    // ── Interrupt lines ───────────────────────────────────────────────────

    /// Raise a non-maskable interrupt. NMI is edge-triggered;
    /// calling this twice before it is serviced counts as one NMI.
    void SetNMI(bool active);

    /// Assert or de-assert the IRQ line (level-triggered, maskable by I flag).
    void SetIRQ(bool active);

    // ── Register access (for tests and debugger) ──────────────────────────

    [[nodiscard]] uint16_t GetPC() const noexcept { return pc_; }
    [[nodiscard]] uint8_t  GetA()  const noexcept { return a_; }
    [[nodiscard]] uint8_t  GetX()  const noexcept { return x_; }
    [[nodiscard]] uint8_t  GetY()  const noexcept { return y_; }
    [[nodiscard]] uint8_t  GetS()  const noexcept { return s_; }
    [[nodiscard]] uint8_t  GetP()  const noexcept { return p_; }

    void SetPC(uint16_t v) noexcept { pc_ = v; }
    void SetA (uint8_t  v) noexcept { a_  = v; }
    void SetX (uint8_t  v) noexcept { x_  = v; }
    void SetY (uint8_t  v) noexcept { y_  = v; }
    void SetS (uint8_t  v) noexcept { s_  = v; }
    void SetP (uint8_t  v) noexcept { p_  = v; }

    // ── ISaveStateable ────────────────────────────────────────────────────

    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    NESMemory& mem_;

    // ──────────────── Registers ───────────────────────────────────────────
    uint16_t pc_ = 0;
    uint8_t  a_  = 0; // Accumulator
    uint8_t  x_  = 0; // Index X
    uint8_t  y_  = 0; // Index Y
    uint8_t  s_  = 0; // Stack pointer (offset from $0100)
    uint8_t  p_  = 0; // Processor status flags

    // ──────────────── Interrupt state ────────────────────────────────────
    bool nmiPending_  = false;
    bool irqAsserted_ = false;

    // ──────────────── Memory helpers ─────────────────────────────────────
    [[nodiscard]] uint8_t  Read8 (uint16_t addr) const;
    [[nodiscard]] uint16_t Read16(uint16_t addr) const;

    void Push8(uint8_t v);
    void Push16(uint16_t v);
    [[nodiscard]] uint8_t  Pop8();
    [[nodiscard]] uint16_t Pop16();

    // ──────────────── Flag helpers ────────────────────────────────────────
    void SetFlag(uint8_t mask, bool value) noexcept;
    [[nodiscard]] bool GetFlag(uint8_t mask) const noexcept { return (p_ & mask) != 0; }

    // Sets N and Z flags based on value.
    void UpdateNZ(uint8_t value) noexcept;

    // ──────────────── Address-mode resolvers ─────────────────────────────
    // Each resolver returns the effective address and a page-cross bool.
    [[nodiscard]] uint16_t AddrImmediate  ()                     noexcept;
    [[nodiscard]] uint16_t AddrZeroPage   ()                     noexcept;
    [[nodiscard]] uint16_t AddrZeroPageX  ()                     noexcept;
    [[nodiscard]] uint16_t AddrZeroPageY  ()                     noexcept;
    [[nodiscard]] uint16_t AddrAbsolute   ()                     noexcept;
    [[nodiscard]] std::pair<uint16_t,bool> AddrAbsoluteX() noexcept;
    [[nodiscard]] std::pair<uint16_t,bool> AddrAbsoluteY() noexcept;
    [[nodiscard]] uint16_t AddrIndirect   ()                     noexcept;
    [[nodiscard]] uint16_t AddrIndirectX  ()                     noexcept;
    [[nodiscard]] std::pair<uint16_t,bool> AddrIndirectY() noexcept;
    [[nodiscard]] int8_t   AddrRelative   ()                     noexcept;

    // ──────────────── Interrupt service ──────────────────────────────────
    void ServiceNMI();
    void ServiceIRQ();

    // ──────────────── Instruction implementations ─────────────────────────
    // All instructions return the number of extra cycles consumed
    // (page-cross penalty or branch-taken penalty).

    int ExecADC(uint16_t addr);
    int ExecAND(uint16_t addr);
    int ExecASL_Acc();
    int ExecASL_Mem(uint16_t addr);
    int ExecBCC();
    int ExecBCS();
    int ExecBEQ();
    int ExecBIT(uint16_t addr);
    int ExecBMI();
    int ExecBNE();
    int ExecBPL();
    int ExecBRK();
    int ExecBVC();
    int ExecBVS();
    int ExecCLC();
    int ExecCLD();
    int ExecCLI();
    int ExecCLV();
    int ExecCMP(uint16_t addr);
    int ExecCPX(uint16_t addr);
    int ExecCPY(uint16_t addr);
    int ExecDEC(uint16_t addr);
    int ExecDEX();
    int ExecDEY();
    int ExecEOR(uint16_t addr);
    int ExecINC(uint16_t addr);
    int ExecINX();
    int ExecINY();
    int ExecJMP(uint16_t addr);
    int ExecJSR();
    int ExecLDA(uint16_t addr);
    int ExecLDX(uint16_t addr);
    int ExecLDY(uint16_t addr);
    int ExecLSR_Acc();
    int ExecLSR_Mem(uint16_t addr);
    int ExecNOP();
    int ExecORA(uint16_t addr);
    int ExecPHA();
    int ExecPHP();
    int ExecPLA();
    int ExecPLP();
    int ExecROL_Acc();
    int ExecROL_Mem(uint16_t addr);
    int ExecROR_Acc();
    int ExecROR_Mem(uint16_t addr);
    int ExecRTI();
    int ExecRTS();
    int ExecSBC(uint16_t addr);
    int ExecSEC();
    int ExecSED();
    int ExecSEI();
    int ExecSTA(uint16_t addr);
    int ExecSTX(uint16_t addr);
    int ExecSTY(uint16_t addr);
    int ExecTAX();
    int ExecTAY();
    int ExecTSX();
    int ExecTXA();
    int ExecTXS();
    int ExecTYA();

    // ──────────────── Branch helper ───────────────────────────────────────
    // Returns extra cycles: 0 (not taken), 1 (taken, same page), 2 (taken, page cross).
    [[nodiscard]] int Branch(bool condition);

    // ──────────────── Undocumented (illegal) opcodes ─────────────────────
    // Sources: NESDev wiki "CPU unofficial opcodes", Visual6502, Kevtris decap.
    // All implement the documented hardware behaviour; unstable opcodes
    // (SHA, SHX, SHY, TAS, XAA) are intentionally omitted — they depend on
    // board capacitance and are not reliably emulable.

    /// SLO — ASL memory then ORA A (addr). Flags: N,Z,C.
    int ExecSLO(uint16_t addr);
    /// RLA — ROL memory then AND A (addr). Flags: N,Z,C.
    int ExecRLA(uint16_t addr);
    /// SRE — LSR memory then EOR A (addr). Flags: N,Z,C.
    int ExecSRE(uint16_t addr);
    /// RRA — ROR memory then ADC A (addr). Flags: N,Z,V,C.
    int ExecRRA(uint16_t addr);
    /// SAX — store A & X to memory. No flags affected.
    int ExecSAX(uint16_t addr);
    /// LAX — load A and X from same address. Flags: N,Z.
    int ExecLAX(uint16_t addr);
    /// DCP — DEC memory then CMP A with result. Flags: N,Z,C.
    int ExecDCP(uint16_t addr);
    /// ISC/ISB — INC memory then SBC A with result. Flags: N,Z,V,C.
    int ExecISC(uint16_t addr);
    /// ANC — AND A with #imm; C = N (bit 7 of result). Flags: N,Z,C.
    int ExecANC();
    /// ALR — AND A with #imm, then LSR A. Flags: N,Z,C.
    int ExecALR();
    /// ARR — AND A with #imm, then ROR A; C/V set specially. Flags: N,Z,V,C.
    int ExecARR();
    /// AXS/SBX — X = (A & X) - #imm; sets N, Z, C (no borrow from A). Flags: N,Z,C.
    int ExecAXS();

    // ──────────────── Cycle-steal tracking ───────────────────────────────
    // Extra CPU stall cycles injected by OAM DMA and DMC sample fetches.
    // NES::Step() reads and resets this after every instruction.
    int stalledCycles_ = 0;

public:
    /// Consume and clear any accumulated stall cycles (OAM DMA / DMC).
    [[nodiscard]] int DrainStalledCycles() noexcept {
        int v = stalledCycles_;
        stalledCycles_ = 0;
        return v;
    }

    /// Inject stall cycles (called by NES when OAM DMA or DMC fetch fires).
    void InjectStall(int cycles) noexcept { stalledCycles_ += cycles; }

};

} // namespace AIO::Emulator::NES
