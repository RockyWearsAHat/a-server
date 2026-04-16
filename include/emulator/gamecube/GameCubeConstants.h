#pragma once

#include <cstdint>

namespace GameCubeEmulator {

// ──────────────────────── Clock frequencies ────────────────────────
static constexpr uint64_t kGeckoCpuHz     = 486000000ULL;  // IBM 750CL "Gecko" @ 486 MHz
static constexpr uint64_t kFlipperBusHz   = 162000000ULL;  // Flipper system bus @ 162 MHz
static constexpr uint64_t kFlipperGpuHz   = 162000000ULL;  // TEV / pixel pipeline clock
static constexpr uint64_t kDspCoreHz      =  81000000ULL;  // DSP core @ 81 MHz
static constexpr uint64_t kAiSampleRate   =  32000ULL;     // Audio Interface sample rate

// ────────────────────────── Memory map ─────────────────────────────
static constexpr uint32_t kRamBase        = 0x00000000U;   // Main RAM (24 MB)
static constexpr uint32_t kRamSize        = 0x01800000U;
static constexpr uint32_t kAramBase       = 0x7E000000U;   // Audio RAM (16 MB, DMA-only stub)
static constexpr uint32_t kAramSize       = 0x01000000U;
static constexpr uint32_t kFifoBase       = 0x08000000U;   // GP FIFO (write-only, WC buffer)
static constexpr uint32_t kFlipperRegsBase= 0xCC000000U;   // Flipper registers (uncached)
static constexpr uint32_t kFlipperRegsSize= 0x00010000U;
static constexpr uint32_t kBiosBase       = 0xFFF00000U;   // IPL ROM (1 MB, read-only)
static constexpr uint32_t kBiosSize       = 0x00100000U;

// ─────────────────────── Video timing (NTSC) ───────────────────────
static constexpr uint16_t kDisplayWidth   = 640;
static constexpr uint16_t kDisplayHeight  = 480;
static constexpr uint16_t kScanlinesNtsc  = 525;
static constexpr uint32_t kCyclesPerLine  = 1710;   // Gecko cycles per NTSC scanline (approx)

// ────────────────────── Flipper TE FIFO constants ──────────────────
static constexpr uint32_t kTevStages      = 16;
static constexpr uint32_t kEfbWidth       = 640;
static constexpr uint32_t kEfbHeight      = 480;
static constexpr uint32_t kTexCacheSize   = 0x00020000U;   // 128 KB texture cache

}  // namespace GameCubeEmulator
