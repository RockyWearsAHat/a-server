#pragma once

#include <cstdint>

namespace GBEmulator {

// CPU timing
static constexpr uint32_t kMasterClockHz = 4194304;  // 4.19 MHz (NTSC reference)

// Memory layout
static constexpr uint16_t kRomBank0Base = 0x0000;
static constexpr uint16_t kRomBank0Size = 0x4000;     // 16 KB
static constexpr uint16_t kRomBankNBase = 0x4000;
static constexpr uint16_t kRomBankNSize = 0x4000;     // 16 KB switchable
static constexpr uint16_t kVramBase = 0x8000;
static constexpr uint16_t kVramSize = 0x2000;         // 8 KB
static constexpr uint16_t kExternalRamBase = 0xA000;
static constexpr uint16_t kExternalRamSize = 0x2000;  // 8 KB switchable
static constexpr uint16_t kInternalRamBase = 0xC000;
static constexpr uint16_t kInternalRamSize = 0x2000;  // 8 KB
static constexpr uint16_t kEchoRamBase = 0xE000;
static constexpr uint16_t kEchoRamSize = 0x1E00;      // Echo of 0xC000–0xDDFF
static constexpr uint16_t kOamBase = 0xFE00;
static constexpr uint16_t kOamSize = 0xA0;            // 160 bytes (40 sprites × 4 bytes)
static constexpr uint16_t kIoRegBase = 0xFF00;
static constexpr uint16_t kIoRegSize = 0x80;          // 128 bytes
static constexpr uint16_t kHramBase = 0xFF80;
static constexpr uint16_t kHramSize = 0x7F;           // 127 bytes

// PPU timing
static constexpr uint16_t kPpuDotWidth = 160;         // Visible pixels
static constexpr uint16_t kPpuDotHeight = 144;        // Visible lines
static constexpr uint16_t kPpuCyclesPerLine = 456;    // 4 M-cycles per dot = 456 cycles per line
static constexpr uint16_t kPpuLinesPerFrame = 154;    // 144 visible + 10 vblank

// Cartridge ROM/RAM limits
static constexpr uint32_t kRomMaxSize = 0x800000;     // 8 MB max
static constexpr uint32_t kRamMaxSize = 0x80000;      // 512 KB max (GBC extended)

}  // namespace GBEmulator
