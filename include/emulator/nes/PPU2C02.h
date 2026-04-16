#pragma once

#include "emulator/common/ISaveStateable.h"
#include "NESConstants.h"
#include <array>
#include <cstdint>
#include <functional>

namespace AIO::Emulator::NES {

class NESCartridge;

/// PPU 2C02 — the NES Picture Processing Unit.
///
/// Renders 256×240 pixels per frame. Outputs a 256-pixel-wide RGBA framebuffer
/// that the host can blit to screen at 60 Hz (NTSC).
///
/// Timing model (NTSC):
///   - 341 PPU cycles per scanline.
///   - 262 scanlines per frame.
///   - Scanlines 0–239:  visible render.
///   - Scanline 240:     post-render idle.
///   - Scanlines 241–260: VBlank (NMI fires at cycle 1 of scanline 241).
///   - Scanline 261:     pre-render (fetches tile data for first visible line).
///   - Odd frames skip the idle cycle at the start of scanline 0.
///
/// Register map ($2000–$2007):
///   $2000 PPUCTRL    — nametable select, sprite/bg pattern table, NMI enable
///   $2001 PPUMASK    — rendering enables, color emphasis
///   $2002 PPUSTATUS  — VBlank flag, sprite 0 hit
///   $2003 OAMADDR    — OAM write address
///   $2004 OAMDATA    — OAM read/write
///   $2005 PPUSCROLL  — X/Y scroll (two writes)
///   $2006 PPUADDR    — VRAM address (two writes)
///   $2007 PPUDATA    — VRAM read/write
class PPU2C02 : public AIO::Emulator::Common::ISaveStateable {
public:
    explicit PPU2C02(NESCartridge& cartridge);
    ~PPU2C02() override = default;

    // ── Lifecycle ─────────────────────────────────────────────────────────

    void Reset();

    /// Advance the PPU by exactly @p cycles PPU-clock ticks.
    /// May fire the NMI callback if a VBlank event occurs during these cycles.
    void Tick(uint32_t cycles);

    // ── Register interface (called from NESMemory) ────────────────────────

    [[nodiscard]] uint8_t ReadRegister (uint8_t reg);
    void                  WriteRegister(uint8_t reg, uint8_t value);

    /// OAM DMA: copy 256 bytes from CPU memory into OAM.
    /// @param page  High byte of source address (0x00–0xFF → $xx00–$xxFF).
    void OamDma(const uint8_t* sourcePage);

    // ── Interrupt output ──────────────────────────────────────────────────

    using NmiCallback = std::function<void()>;
    void SetNmiCallback(NmiCallback cb);

    // ── Framebuffer ───────────────────────────────────────────────────────

    /// Pointer to a 256×240 RGBA8888 framebuffer (256*240*4 bytes).
    [[nodiscard]] const uint32_t* GetFramebuffer() const noexcept;

    // ── Timing queries ────────────────────────────────────────────────────

    [[nodiscard]] int     CurrentScanline() const noexcept { return scanline_; }
    [[nodiscard]] int     CurrentCycle()    const noexcept { return cycle_; }
    [[nodiscard]] bool    IsInVBlank()      const noexcept { return inVblank_; }
    [[nodiscard]] uint64_t FrameCount()    const noexcept { return frameCount_; }

    // ── ISaveStateable ────────────────────────────────────────────────────

    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    NESCartridge& cart_;

    // ── Internal VRAM ─────────────────────────────────────────────────────
    std::array<uint8_t, kVramSize>      vram_{};
    std::array<uint8_t, kPaletteRamSize> palette_{};
    std::array<uint8_t, 256>            oam_{};       // 64 sprites × 4 bytes

    // ── Registers ─────────────────────────────────────────────────────────
    uint8_t  ctrl_   = 0; // PPUCTRL
    uint8_t  mask_   = 0; // PPUMASK
    uint8_t  status_ = 0; // PPUSTATUS
    uint8_t  oamAddr_= 0; // OAMADDR
    uint8_t  dataReadBuf_ = 0; // Internal read buffer for PPUDATA

    // ── Loopy (nametable scroll) registers ────────────────────────────────
    // Named after Loopy's seminal "The Skinny on NES Scrolling" document.
    uint16_t v_ = 0;   // current VRAM address (15 bits)
    uint16_t t_ = 0;   // temporary VRAM address (15 bits)
    uint8_t  fx_= 0;   // fine X scroll (3 bits)
    bool     w_ = false; // write latch (0=first, 1=second)

    // ── Timing ────────────────────────────────────────────────────────────
    int      scanline_  = 0;
    int      cycle_     = 0;
    bool     oddFrame_  = false;
    bool     inVblank_  = false;
    uint64_t frameCount_= 0;

    // ── Background shift registers ────────────────────────────────────────
    uint16_t bgShiftPatLo_  = 0;
    uint16_t bgShiftPatHi_  = 0;
    uint16_t bgShiftAttrLo_ = 0;
    uint16_t bgShiftAttrHi_ = 0;
    uint8_t  bgNextTileId_  = 0;
    uint8_t  bgNextAttr_    = 0;
    uint8_t  bgNextPatLo_   = 0;
    uint8_t  bgNextPatHi_   = 0;

    // ── Sprite evaluation ─────────────────────────────────────────────────
    std::array<uint8_t, 32> spriteScanline_{};
    uint8_t  spriteCount_       = 0;
    std::array<uint8_t, 8> spriteShiftPatLo_{};
    std::array<uint8_t, 8> spriteShiftPatHi_{};
    std::array<uint8_t, 8> spriteAttr_{};
    std::array<uint8_t, 8> spriteX_{};

    bool     spriteZeroHitPossible_ = false;
    bool     spriteZeroBeingRendered_ = false;

    // ── Framebuffer ───────────────────────────────────────────────────────
    std::array<uint32_t, kVisibleDotsPerLine * kVisibleScanlines> framebuffer_{};

    // ── NMI ───────────────────────────────────────────────────────────────
    NmiCallback onNmi_;

    // ── Rendering helpers ─────────────────────────────────────────────────
    void TickVisible();
    void TickPreRender();
    void TickVBlank();

    void IncrementScrollX();
    void IncrementScrollY();
    void TransferAddressX();
    void TransferAddressY();
    void LoadBackgroundShifters();
    void UpdateShifters();

    void SpriteEvaluation();

    [[nodiscard]] uint8_t  ReadVRAM (uint16_t addr)               const;
    void                   WriteVRAM(uint16_t addr, uint8_t value);
    [[nodiscard]] uint16_t MirrorNametableAddr(uint16_t addr)     const;
    [[nodiscard]] uint32_t GetNtscColor(uint8_t paletteIndex)     const;

    [[nodiscard]] bool RenderingEnabled() const noexcept {
        return (mask_ & 0x18) != 0;
    }
};

} // namespace AIO::Emulator::NES
