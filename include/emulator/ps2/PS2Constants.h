#pragma once

#include <cstdint>

namespace PS2Emulator {

// ──────────────────────── Clock frequencies ────────────────────────
static constexpr uint64_t kEEClockHz      = 294912000ULL;  // R5900 EE @ 294.912 MHz
static constexpr uint64_t kIopClockHz     =  36864000ULL;  // IOP R3000A @ 36.864 MHz
static constexpr uint64_t kBusClockHz     = 147456000ULL;  // System bus (EE/2)
static constexpr uint64_t kGsClockHz      = 147456000ULL;  // GS @ bus clock

// ────────────────────────── Memory map ─────────────────────────────
static constexpr uint32_t kEERamBase      = 0x00000000U;   // EE main RAM (32 MB)
static constexpr uint32_t kEERamSize      = 0x02000000U;
static constexpr uint32_t kIopRamBase     = 0x1C000000U;   // IOP RAM (2 MB)
static constexpr uint32_t kIopRamSize     = 0x00200000U;
static constexpr uint32_t kBiosBase       = 0x1FC00000U;   // BIOS ROM (4 MB)
static constexpr uint32_t kBiosSize       = 0x00400000U;
static constexpr uint32_t kScratchBase    = 0x70000000U;   // EE scratch pad (16 KB)
static constexpr uint32_t kScratchSize    = 0x00004000U;
static constexpr uint32_t kVu0CodeBase    = 0x11000000U;   // VU0 code memory (4 KB)
static constexpr uint32_t kVu0DataBase    = 0x11004000U;   // VU0 data memory (4 KB)
static constexpr uint32_t kVu1CodeBase    = 0x11008000U;   // VU1 code memory (16 KB)
static constexpr uint32_t kVu1DataBase    = 0x1100C000U;   // VU1 data memory (16 KB)
static constexpr uint32_t kGsRegsBase     = 0x12000000U;   // GS privileged registers
static constexpr uint32_t kGsRegsSize     = 0x00002000U;
static constexpr uint32_t kEeRegsBase     = 0x10000000U;   // EE hardware registers
static constexpr uint32_t kEeRegsSize     = 0x00010000U;

// ─────────────────────── Video / GS timing ─────────────────────────
static constexpr uint16_t kDisplayWidth   = 640;
static constexpr uint16_t kDisplayHeight  = 448;   // NTSC progressive (480i interlaced native)
static constexpr uint16_t kScanlinesNtsc  = 525;
static constexpr uint32_t kCyclesPerLine  = 2236;  // EE cycles per NTSC scanline (approx)

// ─────────────────────────── GS constants ──────────────────────────
static constexpr uint32_t kGsVramSize     = 0x00400000U;   // GS internal VRAM (4 MB)

}  // namespace PS2Emulator
