#pragma once

#include <cstdint>

namespace N64Emulator {

// System clocks
static constexpr uint64_t kMasterClockHz   = 62500000ULL;   // 62.5 MHz (VI/RCP reference)
static constexpr uint64_t kCpuClockHz      = 93750000ULL;   // 93.75 MHz (R4300i)
static constexpr uint64_t kRspClockHz      = 62500000ULL;   // 62.5 MHz (RSP)

// Memory layout (physical address space)
static constexpr uint32_t kRdramBase       = 0x00000000U;
static constexpr uint32_t kRdramSize       = 0x00800000U;   // 8 MB (standard) expandable to 8 MB total
static constexpr uint32_t kRdramMax        = 0x01000000U;   // 16 MB (expansion pak)
static constexpr uint32_t kRspDmemBase     = 0x04000000U;   // RSP data memory
static constexpr uint32_t kRspDmemSize     = 0x00001000U;   // 4 KB
static constexpr uint32_t kRspImemBase     = 0x04001000U;   // RSP instruction memory
static constexpr uint32_t kRspImemSize     = 0x00001000U;   // 4 KB
static constexpr uint32_t kRdpBase         = 0x04100000U;   // RDP command registers
static constexpr uint32_t kMiBase          = 0x04300000U;   // MIPS interface
static constexpr uint32_t kViBase          = 0x04400000U;   // Video interface
static constexpr uint32_t kAiBase          = 0x04500000U;   // Audio interface
static constexpr uint32_t kPiBase          = 0x04600000U;   // Peripheral interface (cartridge)
static constexpr uint32_t kRiBase          = 0x04700000U;   // RDRAM interface
static constexpr uint32_t kSiBase          = 0x04800000U;   // Serial interface (controller)
static constexpr uint32_t kCartRomBase     = 0x10000000U;   // Cartridge ROM (domain 1)
static constexpr uint32_t kCartRomSize     = 0x0FC00000U;   // ~252 MB max

// PPU/VI timing
static constexpr uint16_t kViWidth         = 320;           // Visible width
static constexpr uint16_t kViHeight        = 240;           // Visible height
static constexpr uint16_t kViLinesPerFrame = 525;           // NTSC scanlines
static constexpr uint32_t kViCyclesPerLine = 3093;          // VI cycles per scanline

// Cartridge limits
static constexpr uint32_t kRomMaxSize      = 0x04000000U;   // 64 MB max cart ROM
static constexpr uint32_t kRomMinSize      = 0x00100000U;   // 1 MB minimum

}  // namespace N64Emulator
