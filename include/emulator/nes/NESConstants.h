#pragma once
#include <cstdint>

/// NES hardware constants — all values sourced from NESDev Wiki and
/// Christian Bauer's "Inside the NES" documentation (Tier-1 sources).
///
/// NTSC timing model (PAL variants noted where they differ).

namespace AIO::Emulator::NES {

// ── Master clock & CPU ────────────────────────────────────────────────────────

/// NTSC master clock: 21.477272 MHz (crystal frequency).
/// All subsystem clocks are derived from this by integer division.
static constexpr double kMasterClockHz = 21'477'272.0;

/// CPU (RP2A03) runs at master / 12 = ~1.789 MHz.
static constexpr uint32_t kCpuClockDivisor = 12;

/// PPU pixel clock: master / 4 = ~5.369 MHz.
static constexpr uint32_t kPpuClockDivisor = 4;

/// The Scheduler uses CPU cycles as its unit; the PPU ticks at 3 CPU cycles
/// per PPU tick (master/4 ÷ master/12 = 3). This is the well-known "3 PPU
/// pixels per CPU cycle" invariant of NTSC NES timing.
static constexpr uint32_t kPpuTicksPerCpuCycle = 3;

// ── PPU (2C02) timing ─────────────────────────────────────────────────────────

/// PPU cycles (pixels) per scanline, including the 1 idle cycle.
static constexpr uint32_t kPpuCyclesPerScanline = 341;

/// Total scanlines per frame (NTSC). Lines 0–239: visible. 240: post-render.
/// 241–260: VBlank. 261: pre-render ("dummy").
static constexpr uint32_t kScanlinesPerFrame    = 262;

/// Visible scanline range: [0, kVisibleScanlines).
static constexpr uint32_t kVisibleScanlines     = 240;

/// VBlank begins at scanline 241.
static constexpr uint32_t kVBlankScanline       = 241;

/// Pre-render scanline (frame reset, odd-frame skip).
static constexpr uint32_t kPreRenderScanline    = 261;

/// Visible pixels per scanline.
static constexpr uint32_t kVisibleDotsPerLine   = 256;

/// Total PPU cycles per frame (NTSC even frame).
static constexpr uint32_t kPpuCyclesPerFrame    =
    kPpuCyclesPerScanline * kScanlinesPerFrame;

// ── CPU cycles per frame (for scheduler target) ───────────────────────────────
static constexpr uint32_t kCpuCyclesPerFrame =
    kPpuCyclesPerFrame / kPpuTicksPerCpuCycle; // 29780 for NTSC

// ── Memory map ────────────────────────────────────────────────────────────────

static constexpr uint16_t kWramBase   = 0x0000;
static constexpr uint16_t kWramSize   = 0x0800; // 2 KB
static constexpr uint16_t kWramMirrorEnd = 0x2000; // mirrored to $1FFF

static constexpr uint16_t kPpuRegBase = 0x2000;
static constexpr uint16_t kPpuRegSize = 0x0008;
static constexpr uint16_t kPpuRegMirrorEnd = 0x4000;

static constexpr uint16_t kApuIoBase  = 0x4000;
static constexpr uint16_t kApuIoSize  = 0x0018;

static constexpr uint16_t kCartridgeBase = 0x4020;
static constexpr uint16_t kCartridgeEnd  = 0xFFFF;

// ── Stack & vectors ────────────────────────────────────────────────────────────

static constexpr uint16_t kStackBase    = 0x0100;
static constexpr uint16_t kNmiVector    = 0xFFFA; // low byte at FFFA, high at FFFB
static constexpr uint16_t kResetVector  = 0xFFFC;
static constexpr uint16_t kIrqBrkVector = 0xFFFE;

// ── CPU status flags (P register bit positions) ───────────────────────────────

static constexpr uint8_t kFlagCarry     = 0x01;
static constexpr uint8_t kFlagZero      = 0x02;
static constexpr uint8_t kFlagIrqDisable= 0x04;
static constexpr uint8_t kFlagDecimal   = 0x08; // BCD mode — disabled on 2A03
static constexpr uint8_t kFlagBreak     = 0x10; // not a real hardware flag
static constexpr uint8_t kFlagUnused    = 0x20; // always 1 in push/pulls
static constexpr uint8_t kFlagOverflow  = 0x40;
static constexpr uint8_t kFlagNegative  = 0x80;

// ── OAM & sprite ──────────────────────────────────────────────────────────────

static constexpr uint8_t  kOamSize        = 64;   // 64 sprites × 4 bytes
static constexpr uint8_t  kMaxSpritesPerLine = 8;

// ── VRAM layout (nametable, palette) ─────────────────────────────────────────

static constexpr uint16_t kVramSize        = 0x0800; // 2 KB nametable RAM
static constexpr uint16_t kPaletteRamBase  = 0x3F00;
static constexpr uint8_t  kPaletteRamSize  = 32;

} // namespace AIO::Emulator::NES
