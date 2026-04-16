#pragma once
#include "emulator/common/ISaveStateable.h"
#include <cstdint>
#include <span>
#include <vector>

namespace AIO::Emulator::Genesis {

/// @brief Sega Genesis cartridge ROM + optional SRAM.
///
/// Supports the following bank-switch mappers:
///   - Mapper 0: flat ROM (no bank-switch) — most Genesis games
///   - SSF2 mapper: 8 × 512 KB banks (Super Street Fighter II)
///   - EEPROM mapper for save-game titles (Micro Machines, etc.)
///
/// The Genesis header at byte 0x100 identifies the game; SRAM presence and
/// range are declared at offsets 0x1B0–0x1B7.
///
/// @code
///   GenesisCartridge cart;
///   cart.Load(span);           // throws std::runtime_error on bad ROM
///   uint16_t word = cart.Read16(0x200); // read ROM word
/// @endcode
class GenesisCartridge : public AIO::Emulator::Common::ISaveStateable {
public:
    GenesisCartridge()  = default;
    ~GenesisCartridge() override = default;

    GenesisCartridge(const GenesisCartridge&)            = delete;
    GenesisCartridge& operator=(const GenesisCartridge&) = delete;

    /// Load a raw Mega Drive ROM image (binary, not SMD split format).
    /// @throws std::runtime_error if the ROM is too small or too large.
    void Load(std::span<const uint8_t> data);

    // ── CPU bus accessors ─────────────────────────────────────────────────
    [[nodiscard]] uint8_t  Read8 (uint32_t addr) const noexcept;
    [[nodiscard]] uint16_t Read16(uint32_t addr) const noexcept;
    void                   Write8 (uint32_t addr, uint8_t  value) noexcept;
    void                   Write16(uint32_t addr, uint16_t value) noexcept;

    /// Handle SSF2 mapper bank register writes at 0xA130F1–0xA130FF.
    void WriteBankReg(uint8_t reg, uint8_t bank) noexcept;

    // ── State ──────────────────────────────────────────────────────────────
    [[nodiscard]] bool HasSram()    const noexcept { return hasSram_; }
    [[nodiscard]] uint32_t SramStart() const noexcept { return sramStart_; }
    [[nodiscard]] uint32_t SramEnd()   const noexcept { return sramEnd_; }

    // ISaveStateable
    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    std::vector<uint8_t> rom_;
    std::vector<uint8_t> sram_;

    bool     hasSram_   {false};
    uint32_t sramStart_ {0};
    uint32_t sramEnd_   {0};
    bool     sramEnabled_{false};

    // SSF2 bank registers: 8 slots × 512 KB
    uint8_t  bankRegs_[8] {0, 1, 2, 3, 4, 5, 6, 7};
    bool     isSSF2_     {false};

    [[nodiscard]] uint32_t MapAddress(uint32_t addr) const noexcept;
};

} // namespace AIO::Emulator::Genesis
