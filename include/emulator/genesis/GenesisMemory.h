#pragma once
#include "emulator/common/ISaveStateable.h"
#include "emulator/genesis/GenesisConstants.h"
#include <array>
#include <cstdint>

namespace AIO::Emulator::Genesis {

class GenesisCartridge;
class GenesisVDP;
class YM2612;
class SN76489;

/// @brief Genesis M68000 and Z80 bus multiplexer.
///
/// Routes M68K and Z80 bus transactions to the correct peripheral:
///
///   M68K address map:
///     0x000000–0x3FFFFF → cartridge ROM (via GenesisCartridge)
///     0xA00000–0xA0FFFF → Z80 bus window (proxied via Z80 RAM)
///     0xA11100           → Z80 BUSREQ register
///     0xA11200           → Z80 RESET register
///     0xA13000–0xA130FF  → SSF2 / other cart registers
///     0xC00000–0xC00003  → VDP DATA port
///     0xC00004–0xC00007  → VDP CTRL port / status
///     0xC00008–0xC0000B  → VDP H/V counter
///     0xC00011           → PSG write
///     0xFF0000–0xFFFFFF  → 64 KB main RAM (mirror of 0xE00000–0xFFFFFF)
///
///   Z80 address map:
///     0x0000–0x1FFF → 8 KB Z80 RAM
///     0x4000–0x4003 → YM2612 registers
///     0x7F11         → SN76489
///     0x8000–0xFFFF → 32 KB M68K bank window
///
/// @code
///   GenesisMemory mem;
///   mem.Init(&cart, &vdp, &ym, &psg);
///   uint16_t w = mem.M68KRead16(0xFF0000);
/// @endcode
class GenesisMemory : public AIO::Emulator::Common::ISaveStateable {
public:
    GenesisMemory()  = default;
    ~GenesisMemory() override = default;

    GenesisMemory(const GenesisMemory&)            = delete;
    GenesisMemory& operator=(const GenesisMemory&) = delete;

    /// Attach peripherals. Must be called before any bus access.
    void Init(GenesisCartridge* cart,
              GenesisVDP*       vdp,
              YM2612*           ym,
              SN76489*          psg) noexcept;

    // ── M68K bus interface ─────────────────────────────────────────────────
    [[nodiscard]] uint8_t  M68KRead8 (uint32_t addr);
    [[nodiscard]] uint16_t M68KRead16(uint32_t addr);
    void                   M68KWrite8 (uint32_t addr, uint8_t  value);
    void                   M68KWrite16(uint32_t addr, uint16_t value);

    // ── Z80 bus interface (called by Z80 core) ─────────────────────────────
    [[nodiscard]] uint8_t Z80Read8 (uint16_t addr);
    void                  Z80Write8(uint16_t addr, uint8_t value);
    [[nodiscard]] uint8_t Z80In8   (uint8_t port);
    void                  Z80Out8  (uint8_t port, uint8_t value);

    // ── Z80 bank window control ────────────────────────────────────────────
    /// Called by Z80 core when it writes the bank window register.
    void SetZ80BankWindow(uint32_t addr23to15) noexcept { z80BankBase_ = addr23to15 & 0xFF8000u; }
    [[nodiscard]] uint32_t GetZ80BankWindow() const noexcept { return z80BankBase_; }

    // ── Direct RAM access (for DMA / test) ────────────────────────────────
    [[nodiscard]] uint8_t* RawRamPtr() noexcept { return ram_.data(); }

    // ISaveStateable
    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    std::array<uint8_t, kRamSize>    ram_    {};  ///< 64 KB main RAM
    std::array<uint8_t, kZ80RamSize> z80Ram_ {};  ///< 8 KB Z80 RAM

    uint32_t z80BankBase_ {0}; ///< High bits of M68K address mapped to Z80 0x8000 window

    GenesisCartridge* cart_ {nullptr};
    GenesisVDP*       vdp_  {nullptr};
    YM2612*           ym_   {nullptr};
    SN76489*          psg_  {nullptr};
};

} // namespace AIO::Emulator::Genesis
