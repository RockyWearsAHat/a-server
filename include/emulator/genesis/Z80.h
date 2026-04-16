#pragma once
#include "emulator/common/ISaveStateable.h"
#include <cstdint>

namespace AIO::Emulator::Genesis {

/// @brief Zilog Z80 co-processor for the Sega Genesis.
///
/// Models the Z80-A running at ~3.58 MHz used as the Genesis sound CPU.
/// The Z80 has exclusive access to:
///   - 8 KB internal RAM (0x0000–0x1FFF)
///   - YM2612 at 0x4000–0x4003
///   - SN76489 at 0x7F11
///   - 32 KB M68K bank window at 0x8000–0xFFFF
///
/// The M68K controls the Z80 via two bus-control registers:
///   - BUS REQUEST  (0xA11100): M68K can assert to take the Z80 bus
///   - RESET        (0xA11200): active-low reset line
///
/// @code
///   GenesisMemory mem;
///   Z80 z80(mem);
///   z80.Reset();
///   z80.SetBusRequest(false); // release Z80 to run
///   int cy = z80.Step();
/// @endcode
class Z80 : public AIO::Emulator::Common::ISaveStateable {
public:
    explicit Z80(class GenesisMemory& mem) noexcept;
    ~Z80() override = default;

    Z80(const Z80&)            = delete;
    Z80& operator=(const Z80&) = delete;

    /// Drive the RESET# line. When asserted (true) the Z80 is held in reset.
    void SetReset(bool reset) noexcept;

    /// Drive the BUSREQ line from the M68K.
    /// When asserted (true) the M68K holds the Z80 bus; Z80 does not execute.
    void SetBusRequest(bool busReq) noexcept { busReq_ = busReq; }

    /// Execute one instruction or consume bus-wait cycles.
    /// Returns master-clock cycles consumed (0 when bus is requested or in reset).
    [[nodiscard]] int Step();

    // ── Debugging / test accessors ─────────────────────────────────────────
    [[nodiscard]] uint16_t GetPC() const noexcept { return pc_; }
    [[nodiscard]] uint16_t GetSP() const noexcept { return sp_; }
    [[nodiscard]] uint8_t  GetA()  const noexcept { return a_; }
    [[nodiscard]] uint8_t  GetF()  const noexcept { return f_; }
    [[nodiscard]] uint8_t  GetB()  const noexcept { return b_; }
    [[nodiscard]] uint8_t  GetC()  const noexcept { return c_; }
    [[nodiscard]] uint8_t  GetD()  const noexcept { return d_; }
    [[nodiscard]] uint8_t  GetE()  const noexcept { return e_; }
    [[nodiscard]] uint8_t  GetH()  const noexcept { return h_; }
    [[nodiscard]] uint8_t  GetL()  const noexcept { return l_; }

    /// Set the M68K bank window register (high 9 bits of the 23-bit bank address).
    void SetBankWindow(uint8_t highBit) noexcept;

    // ISaveStateable
    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    GenesisMemory& mem_;

    // ── Main register file ────────────────────────────────────────────────
    uint8_t  a_  {0xFF}, f_  {0xFF};  ///< Accumulator + flags
    uint8_t  b_  {0}, c_  {0};
    uint8_t  d_  {0}, e_  {0};
    uint8_t  h_  {0}, l_  {0};
    uint8_t  ixh_{0}, ixl_{0};        ///< Index register IX
    uint8_t  iyh_{0}, iyl_{0};        ///< Index register IY
    uint8_t  i_  {0};                 ///< Interrupt vector register
    uint8_t  r_  {0};                 ///< Memory refresh register
    uint16_t sp_ {0xFFFF};
    uint16_t pc_ {0x0000};

    // ── Alternate register shadow ────────────────────────────────────────
    uint8_t  a2_ {0}, f2_ {0};
    uint8_t  b2_ {0}, c2_ {0};
    uint8_t  d2_ {0}, e2_ {0};
    uint8_t  h2_ {0}, l2_ {0};

    // ── Control / interrupt ───────────────────────────────────────────────
    bool   busReq_ {true};   ///< M68K holds Z80 bus (default: Z80 halted until released)
    bool   reset_  {true};   ///< Z80 in reset state
    bool   halted_ {false};  ///< HALT instruction executed
    bool   iff1_   {false};  ///< Interrupt flip-flop 1
    bool   iff2_   {false};  ///< Interrupt flip-flop 2
    uint8_t im_    {0};      ///< Interrupt mode (0, 1, or 2)
    bool   nmiPending_ {false};
    bool   irqPending_ {false};

    uint32_t bankAddr_ {0};  ///< 32 KB M68K bank address (bits 23..15)

    // ── Register pair helpers ─────────────────────────────────────────────
    [[nodiscard]] uint16_t GetBC() const noexcept { return (static_cast<uint16_t>(b_) << 8) | c_; }
    [[nodiscard]] uint16_t GetDE() const noexcept { return (static_cast<uint16_t>(d_) << 8) | e_; }
    [[nodiscard]] uint16_t GetHL() const noexcept { return (static_cast<uint16_t>(h_) << 8) | l_; }
    [[nodiscard]] uint16_t GetIX() const noexcept { return (static_cast<uint16_t>(ixh_) << 8) | ixl_; }
    [[nodiscard]] uint16_t GetIY() const noexcept { return (static_cast<uint16_t>(iyh_) << 8) | iyl_; }
    [[nodiscard]] uint16_t GetAF() const noexcept { return (static_cast<uint16_t>(a_) << 8) | f_; }

    void SetBC(uint16_t v) noexcept { b_ = static_cast<uint8_t>(v >> 8); c_ = static_cast<uint8_t>(v); }
    void SetDE(uint16_t v) noexcept { d_ = static_cast<uint8_t>(v >> 8); e_ = static_cast<uint8_t>(v); }
    void SetHL(uint16_t v) noexcept { h_ = static_cast<uint8_t>(v >> 8); l_ = static_cast<uint8_t>(v); }
    void SetIX(uint16_t v) noexcept { ixh_ = static_cast<uint8_t>(v >> 8); ixl_ = static_cast<uint8_t>(v); }
    void SetIY(uint16_t v) noexcept { iyh_ = static_cast<uint8_t>(v >> 8); iyl_ = static_cast<uint8_t>(v); }
    void SetAF(uint16_t v) noexcept { a_ = static_cast<uint8_t>(v >> 8); f_ = static_cast<uint8_t>(v); }

    // ── Flag helpers ──────────────────────────────────────────────────────
    static constexpr uint8_t kFlagC = 0x01;
    static constexpr uint8_t kFlagN = 0x02;
    static constexpr uint8_t kFlagP = 0x04; ///< Parity / overflow
    static constexpr uint8_t kFlagH = 0x10;
    static constexpr uint8_t kFlagZ = 0x40;
    static constexpr uint8_t kFlagS = 0x80;

    [[nodiscard]] bool FlagC() const noexcept { return (f_ & kFlagC) != 0; }
    [[nodiscard]] bool FlagZ() const noexcept { return (f_ & kFlagZ) != 0; }
    [[nodiscard]] bool FlagS() const noexcept { return (f_ & kFlagS) != 0; }
    [[nodiscard]] bool FlagP() const noexcept { return (f_ & kFlagP) != 0; }
    [[nodiscard]] bool FlagH() const noexcept { return (f_ & kFlagH) != 0; }
    [[nodiscard]] bool FlagN() const noexcept { return (f_ & kFlagN) != 0; }

    // ── Bus helpers ───────────────────────────────────────────────────────
    [[nodiscard]] uint8_t  Read8 (uint16_t addr);
    void                   Write8(uint16_t addr, uint8_t v);
    [[nodiscard]] uint8_t  In8   (uint8_t port);
    void                   Out8  (uint8_t port, uint8_t v);

    // ── Stack helpers ─────────────────────────────────────────────────────
    void    Push(uint16_t v);
    [[nodiscard]] uint16_t Pop();

    // ── Instruction dispatch ──────────────────────────────────────────────
    [[nodiscard]] int Dispatch();
    [[nodiscard]] int DispatchCB();
    [[nodiscard]] int DispatchDD();
    [[nodiscard]] int DispatchED();
    [[nodiscard]] int DispatchFD();
    [[nodiscard]] int DispatchDDCB(int8_t dis);
    [[nodiscard]] int DispatchFDCB(int8_t dis);

    // ── Interrupt service ─────────────────────────────────────────────────
    [[nodiscard]] int ServiceNMI();
    [[nodiscard]] int ServiceIRQ();
};

} // namespace AIO::Emulator::Genesis
