#pragma once

#include "emulator/common/IBusDevice.h"
#include "emulator/common/ISaveStateable.h"
#include "NESConstants.h"
#include <array>
#include <cstdint>
#include <functional>

namespace AIO::Emulator::NES {

class NESCartridge;

/// NES system bus memory map.
///
/// Routes CPU address-space accesses (two-byte addresses, $0000–$FFFF) to
/// the correct backing store: WRAM, PPU registers, APU/IO, or cartridge.
/// Mirror logic follows the hardware exactly:
///   WRAM:        $0000–$07FF mirrored four times to $0000–$1FFF
///   PPU regs:    $2000–$2007 mirrored every 8 bytes to $2000–$3FFF
///   APU/IO:      $4000–$4017 (no mirror)
///   Cartridge:   $4020–$FFFF (mapper-specific)
///
/// PPU register side-effects (palette, latch, etc.) are delegated to the
/// PPU object via callbacks registered in Init().
class NESMemory : public AIO::Emulator::Common::IBusDevice,
                  public AIO::Emulator::Common::ISaveStateable {
public:
    NESMemory();
    ~NESMemory() override = default;

    // ── Initialisation ────────────────────────────────────────────────────

    /// Connect the cartridge (must be called before any access).
    /// @throws std::invalid_argument if cartridge is null.
    void Init(NESCartridge* cartridge);

    void Reset();

    // ── IBusDevice ────────────────────────────────────────────────────────

    [[nodiscard]] uint8_t  Read8 (uint32_t address) override;
    void                   Write8(uint32_t address, uint8_t value) override;
    [[nodiscard]] std::string_view DeviceName() const override { return "NESMemory"; }

    // ── Direct WRAM access (for tests and DMA) ────────────────────────────

    [[nodiscard]] uint8_t ReadWRAM(uint16_t addr) const noexcept;
    void                  WriteWRAM(uint16_t addr, uint8_t value) noexcept;

    // ── PPU register proxy callbacks ──────────────────────────────────────
    // The PPU owns its register semantics. NESMemory forwards writes from
    // the $2000–$3FFF range to these callbacks rather than implementing PPU
    // logic here (separation of concerns).

    using PpuReadFn  = std::function<uint8_t(uint8_t reg)>;
    using PpuWriteFn = std::function<void(uint8_t reg, uint8_t value)>;

    void SetPpuCallbacks(PpuReadFn onRead, PpuWriteFn onWrite);

    // ── APU/IO register proxy ─────────────────────────────────────────────

    using ApuReadFn  = std::function<uint8_t(uint16_t addr)>;
    using ApuWriteFn = std::function<void(uint16_t addr, uint8_t value)>;

    void SetApuCallbacks(ApuReadFn onRead, ApuWriteFn onWrite);

    // ── ISaveStateable ────────────────────────────────────────────────────

    void SaveState(AIO::Emulator::Common::SaveStateWriter& writer) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& reader) override;

private:
    std::array<uint8_t, kWramSize> wram_{};
    NESCartridge*                  cartridge_ = nullptr;

    PpuReadFn  ppuRead_;
    PpuWriteFn ppuWrite_;
    ApuReadFn  apuRead_;
    ApuWriteFn apuWrite_;

    [[nodiscard]] uint8_t ReadPpuRegister (uint8_t  reg)  const;
    void                  WritePpuRegister(uint8_t  reg, uint8_t value);
    [[nodiscard]] uint8_t ReadApuIo       (uint16_t addr) const;
    void                  WriteApuIo      (uint16_t addr, uint8_t value);
};

} // namespace AIO::Emulator::NES
