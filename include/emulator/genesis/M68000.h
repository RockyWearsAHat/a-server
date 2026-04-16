#pragma once
#include "emulator/common/ISaveStateable.h"
#include <cstdint>

namespace AIO::Emulator::Genesis {

class GenesisVDP;
class GenesisAPU;

/// @brief Motorola MC68000 CPU core for the Sega Genesis.
///
/// Implements the full MC68000 instruction set including all addressing modes,
/// the 7-level interrupt controller, the STOP instruction, and the bus error
/// and address error exception flow.
///
/// Cycle counts match Charles MacDonald's US-patent timing tables (Tier-1).
///
/// @code
///   GenesisMemory mem;
///   M68000 cpu(mem);
///   cpu.Reset();
///   int cycles = cpu.Step(); // execute one instruction, return cycles consumed
/// @endcode
class M68000 : public AIO::Emulator::Common::ISaveStateable {
public:
    explicit M68000(class GenesisMemory& mem) noexcept;
    ~M68000() override = default;

    M68000(const M68000&)            = delete;
    M68000& operator=(const M68000&) = delete;

    /// Pull RESET# line: load supervisor stack pointer and PC from reset vectors.
    void Reset();

    /// Execute one full instruction. Returns master-clock cycles consumed.
    [[nodiscard]] int Step();

    /// Assert or release the interrupt pending lines (levels 1–7).
    /// Level 0 = no interrupt; level 7 = NMI (always taken).
    void SetInterruptLevel(int level) noexcept { pendingIpl_ = level; }

    /// Assert or release the HALT line.
    void SetHalt(bool halt) noexcept { halted_ = halt; }

    /// Assert or release the external bus error signal.
    void SetBusError(bool be) noexcept { busError_ = be; }

    // ── Debugging / test accessors ─────────────────────────────────────────
    [[nodiscard]] uint32_t GetPC()  const noexcept { return pc_; }
    [[nodiscard]] uint32_t GetSP()  const noexcept { return a_[7]; }
    [[nodiscard]] uint16_t GetSR()  const noexcept { return sr_; }
    [[nodiscard]] uint32_t GetD(int n) const noexcept { return d_[n & 7]; }
    [[nodiscard]] uint32_t GetA(int n) const noexcept { return a_[n & 7]; }

    // ISaveStateable
    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    GenesisMemory& mem_;

    // ── Registers ────────────────────────────────────────────────────────
    uint32_t d_[8]  {};   ///< Data registers D0–D7
    uint32_t a_[8]  {};   ///< Address registers A0–A7 (A7 = USP or SSP)
    uint32_t ssp_   {0};  ///< Supervisor stack pointer (saved when in user mode)
    uint32_t usp_   {0};  ///< User stack pointer (saved when in supervisor mode)
    uint32_t pc_    {0};
    uint16_t sr_    {0x2700}; ///< Status register (supervisor, IPL mask = 7)

    int  pendingIpl_ {0};
    bool halted_     {false};
    bool busError_   {false};
    bool stopped_    {false};

    // ── SR helpers ────────────────────────────────────────────────────────
    [[nodiscard]] bool    FlagC()  const noexcept { return (sr_ & 0x0001) != 0; }
    [[nodiscard]] bool    FlagV()  const noexcept { return (sr_ & 0x0002) != 0; }
    [[nodiscard]] bool    FlagZ()  const noexcept { return (sr_ & 0x0004) != 0; }
    [[nodiscard]] bool    FlagN()  const noexcept { return (sr_ & 0x0008) != 0; }
    [[nodiscard]] bool    FlagX()  const noexcept { return (sr_ & 0x0010) != 0; }
    [[nodiscard]] int     GetIPL() const noexcept { return (sr_ >> 8) & 0x07; }
    [[nodiscard]] bool    IsSuper()const noexcept { return (sr_ & 0x2000) != 0; }

    void SetFlagC(bool v) noexcept { sr_ = v ? (sr_ | 0x0001) : (sr_ & ~0x0001); }
    void SetFlagV(bool v) noexcept { sr_ = v ? (sr_ | 0x0002) : (sr_ & ~0x0002); }
    void SetFlagZ(bool v) noexcept { sr_ = v ? (sr_ | 0x0004) : (sr_ & ~0x0004); }
    void SetFlagN(bool v) noexcept { sr_ = v ? (sr_ | 0x0008) : (sr_ & ~0x0008); }
    void SetFlagX(bool v) noexcept { sr_ = v ? (sr_ | 0x0010) : (sr_ & ~0x0010); }
    void SetIPL(int level) noexcept { sr_ = (sr_ & ~0x0700) | static_cast<uint16_t>((level & 7) << 8); }
    void EnterSuper() noexcept;
    void LeaveSuper() noexcept;

    // ── Bus helpers ───────────────────────────────────────────────────────
    [[nodiscard]] uint8_t  Read8 (uint32_t addr);
    [[nodiscard]] uint16_t Read16(uint32_t addr);
    [[nodiscard]] uint32_t Read32(uint32_t addr);
    void Write8 (uint32_t addr, uint8_t  v);
    void Write16(uint32_t addr, uint16_t v);
    void Write32(uint32_t addr, uint32_t v);

    // ── Stack helpers ─────────────────────────────────────────────────────
    void Push16(uint16_t v);
    void Push32(uint32_t v);
    [[nodiscard]] uint16_t Pop16();
    [[nodiscard]] uint32_t Pop32();

    // ── Effective address resolver ────────────────────────────────────────
    [[nodiscard]] uint32_t CalcEA(int mode, int reg, int size);

    // ── Interrupt service ─────────────────────────────────────────────────
    void ServiceInterrupt(int level);

    // ── Instruction dispatch ──────────────────────────────────────────────
    [[nodiscard]] int Dispatch(uint16_t opcode);

    // ── Instruction groups ────────────────────────────────────────────────
    [[nodiscard]] int ExecMOVE    (uint16_t op);
    [[nodiscard]] int ExecMOVEA   (uint16_t op);
    [[nodiscard]] int ExecMOVESR  (uint16_t op);
    [[nodiscard]] int ExecMOVEUSP (uint16_t op);
    [[nodiscard]] int ExecMOVEM   (uint16_t op);
    [[nodiscard]] int ExecMOVEP   (uint16_t op);
    [[nodiscard]] int ExecLEA     (uint16_t op);
    [[nodiscard]] int ExecPEA     (uint16_t op);
    [[nodiscard]] int ExecEXG     (uint16_t op);
    [[nodiscard]] int ExecEXT     (uint16_t op);
    [[nodiscard]] int ExecSWAP    (uint16_t op);

    [[nodiscard]] int ExecADD     (uint16_t op);
    [[nodiscard]] int ExecADDA    (uint16_t op);
    [[nodiscard]] int ExecADDI    (uint16_t op);
    [[nodiscard]] int ExecADDQ    (uint16_t op);
    [[nodiscard]] int ExecADDX    (uint16_t op);
    [[nodiscard]] int ExecSUB     (uint16_t op);
    [[nodiscard]] int ExecSUBA    (uint16_t op);
    [[nodiscard]] int ExecSUBI    (uint16_t op);
    [[nodiscard]] int ExecSUBQ    (uint16_t op);
    [[nodiscard]] int ExecSUBX    (uint16_t op);
    [[nodiscard]] int ExecMULS    (uint16_t op);
    [[nodiscard]] int ExecMULU    (uint16_t op);
    [[nodiscard]] int ExecDIVS    (uint16_t op);
    [[nodiscard]] int ExecDIVU    (uint16_t op);
    [[nodiscard]] int ExecNEG     (uint16_t op);
    [[nodiscard]] int ExecNEGX    (uint16_t op);
    [[nodiscard]] int ExecCLR     (uint16_t op);
    [[nodiscard]] int ExecCMP     (uint16_t op);
    [[nodiscard]] int ExecCMPA    (uint16_t op);
    [[nodiscard]] int ExecCMPI    (uint16_t op);
    [[nodiscard]] int ExecCMPM    (uint16_t op);

    [[nodiscard]] int ExecAND     (uint16_t op);
    [[nodiscard]] int ExecANDI    (uint16_t op);
    [[nodiscard]] int ExecANDISR  (uint16_t op);
    [[nodiscard]] int ExecOR      (uint16_t op);
    [[nodiscard]] int ExecORI     (uint16_t op);
    [[nodiscard]] int ExecORISR   (uint16_t op);
    [[nodiscard]] int ExecEOR     (uint16_t op);
    [[nodiscard]] int ExecEORI    (uint16_t op);
    [[nodiscard]] int ExecNOT     (uint16_t op);

    [[nodiscard]] int ExecASL     (uint16_t op);
    [[nodiscard]] int ExecASR     (uint16_t op);
    [[nodiscard]] int ExecLSL     (uint16_t op);
    [[nodiscard]] int ExecLSR     (uint16_t op);
    [[nodiscard]] int ExecROL     (uint16_t op);
    [[nodiscard]] int ExecROR     (uint16_t op);
    [[nodiscard]] int ExecROXL    (uint16_t op);
    [[nodiscard]] int ExecROXR    (uint16_t op);

    [[nodiscard]] int ExecBTST    (uint16_t op);
    [[nodiscard]] int ExecBSET    (uint16_t op);
    [[nodiscard]] int ExecBCLR    (uint16_t op);
    [[nodiscard]] int ExecBCHG    (uint16_t op);

    [[nodiscard]] int ExecBRA     (uint16_t op);
    [[nodiscard]] int ExecBSR     (uint16_t op);
    [[nodiscard]] int ExecBcc     (uint16_t op);
    [[nodiscard]] int ExecDBcc    (uint16_t op);
    [[nodiscard]] int ExecScc     (uint16_t op);
    [[nodiscard]] int ExecJMP     (uint16_t op);
    [[nodiscard]] int ExecJSR     (uint16_t op);
    [[nodiscard]] int ExecRTS     ();
    [[nodiscard]] int ExecRTE     ();
    [[nodiscard]] int ExecRTR     ();
    [[nodiscard]] int ExecTST     (uint16_t op);
    [[nodiscard]] int ExecTAS     (uint16_t op);
    [[nodiscard]] int ExecNOP     ();
    [[nodiscard]] int ExecSTOP    (uint16_t op);
    [[nodiscard]] int ExecRESET   ();
    [[nodiscard]] int ExecTRAP    (uint16_t op);
    [[nodiscard]] int ExecTRAPV   ();
    [[nodiscard]] int ExecCHK     (uint16_t op);
    [[nodiscard]] int ExecILLEGAL ();
    [[nodiscard]] int ExecLINEA   ();
    [[nodiscard]] int ExecLINEF   ();
    [[nodiscard]] int ExecLINK    (uint16_t op);
    [[nodiscard]] int ExecUNLK    (uint16_t op);
    [[nodiscard]] int ExecABCD    (uint16_t op);
    [[nodiscard]] int ExecSBCD    (uint16_t op);
    [[nodiscard]] int ExecNBCD    (uint16_t op);
    [[nodiscard]] int ExecPACK    (uint16_t op);
    [[nodiscard]] int ExecUNPK    (uint16_t op);
};

} // namespace AIO::Emulator::Genesis
