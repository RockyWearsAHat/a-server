#pragma once

#include "emulator/common/ISaveStateable.h"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace AIO::Emulator::NES {

/// iNES / NES 2.0 mirroring modes as reported in the header.
enum class MirrorMode : uint8_t {
    Horizontal = 0,
    Vertical   = 1,
    FourScreen = 2,
    SingleLow  = 3,
    SingleHigh = 4,
};

/// Abstract mapper interface.
/// Each supported mapper number implements this to provide PRG/CHR banking.
class IMapper : public AIO::Emulator::Common::ISaveStateable {
public:
    virtual ~IMapper() = default;

    virtual void Reset() = 0;

    /// Read from PRG address space ($8000–$FFFF CPU).
    [[nodiscard]] virtual uint8_t ReadPRG(uint16_t addr) = 0;

    /// Write to PRG address space (triggers mapper register update).
    virtual void WritePRG(uint16_t addr, uint8_t value) = 0;

    /// Read from CHR address space ($0000–$1FFF PPU).
    [[nodiscard]] virtual uint8_t ReadCHR(uint16_t addr) = 0;

    /// Write to CHR address space (CHR RAM mappers only).
    virtual void WriteCHR(uint16_t addr, uint8_t value) = 0;

    /// Current mirroring mode (may be writable for MMC1/MMC3).
    [[nodiscard]] virtual MirrorMode GetMirrorMode() const noexcept = 0;

    /// Per-scanline callback used by IRQ-capable mappers (MMC3 etc.).
    virtual void ScanlineIRQ() {}

    [[nodiscard]] virtual bool IrqPending() const noexcept { return false; }

    // ISaveStateable default no-ops so simple mappers don't have to override.
    void SaveState(AIO::Emulator::Common::SaveStateWriter&) const override {}
    void LoadState(AIO::Emulator::Common::SaveStateReader&)        override {}
};

/// NES Cartridge — iNES/NES2.0 ROM loader + active mapper.
///
/// Responsibilities:
///   - Parse the 16-byte iNES header.
///   - Allocate PRG ROM banks and CHR ROM/RAM buffers.
///   - Instantiate the correct IMapper implementation (0/1/2/3/4).
///   - Expose IBusDevice interfaces for the CPU ($4020–$FFFF) and
///     PPU ($0000–$1FFF) address buses.
class NESCartridge : public AIO::Emulator::Common::ISaveStateable {
public:
    NESCartridge();
    ~NESCartridge() override = default;

    // ── Loading ───────────────────────────────────────────────────────────

    /// Load a ROM image from raw bytes.
    /// @throws std::invalid_argument for malformed headers or unsupported mappers.
    void Load(std::span<const uint8_t> romData);

    [[nodiscard]] bool IsLoaded() const noexcept { return loaded_; }
    [[nodiscard]] std::string_view RomName() const noexcept { return romName_; }

    void Reset();

    // ── CPU bus ───────────────────────────────────────────────────────────

    [[nodiscard]] uint8_t  CpuRead (uint16_t addr);
    void                   CpuWrite(uint16_t addr, uint8_t value);

    // ── PPU bus ───────────────────────────────────────────────────────────

    [[nodiscard]] uint8_t  PpuRead (uint16_t addr);
    void                   PpuWrite(uint16_t addr, uint8_t value);

    // ── Mirroring query ───────────────────────────────────────────────────

    [[nodiscard]] MirrorMode GetMirrorMode() const noexcept;

    // ── IRQ (scanline-based mappers like MMC3) ────────────────────────────

    void NotifyScanline();
    [[nodiscard]] bool IrqPending() const noexcept;

    // ── ISaveStateable ────────────────────────────────────────────────────

    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    bool        loaded_  = false;
    std::string romName_;

    std::vector<uint8_t> prgRom_;
    std::vector<uint8_t> chrRom_;  // empty if CHR RAM
    std::array<uint8_t, 8192> chrRam_{};

    uint8_t     mapperId_    = 0;
    MirrorMode  mirrorMode_  = MirrorMode::Horizontal;
    bool        hasChrRam_   = false;

    std::unique_ptr<IMapper> mapper_;

    void ParseHeader(std::span<const uint8_t> data);
    void InstantiateMapper();
};

} // namespace AIO::Emulator::NES
