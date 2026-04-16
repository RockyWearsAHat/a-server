#pragma once

#include <cstdint>

namespace SaturnEmulator {

// System clocks
static constexpr uint64_t kMasterClockHz       = 28636363ULL;  // NTSC master (~28.6 MHz)
static constexpr uint64_t kShClockHz           = 28636363ULL;  // SH-2 CPU (~28.6 MHz, × 1 of master)
static constexpr uint64_t kScsClockHz          = 11289600ULL;  // SCSP audio clock
static constexpr uint64_t kSmpcClockHz         =  4000000ULL;  // SMPC system management

// Memory layout (physical)
static constexpr uint32_t kBootRomBase         = 0x00000000U;  // Boot ROM (512 KB)
static constexpr uint32_t kBootRomSize         = 0x00080000U;
static constexpr uint32_t kSmpcBase            = 0x00100000U;  // SMPC registers
static constexpr uint32_t kBackupRamBase       = 0x00180000U;  // Internal backup RAM (32 KB)
static constexpr uint32_t kBackupRamSize       = 0x00008000U;
static constexpr uint32_t kLowRamBase          = 0x00200000U;  // Work RAM-L (1 MB SDRAM)
static constexpr uint32_t kLowRamSize          = 0x00100000U;
static constexpr uint32_t kStvIoBase           = 0x00400000U;  // STV-specific I/O (arcade)
static constexpr uint32_t kVdp1VramBase        = 0x05C00000U;  // VDP1 VRAM (512 KB)
static constexpr uint32_t kVdp1VramSize        = 0x00080000U;
static constexpr uint32_t kVdp1FbBase          = 0x05C80000U;  // VDP1 framebuffer (256 KB)
static constexpr uint32_t kVdp1FbSize          = 0x00040000U;
static constexpr uint32_t kVdp1RegsBase        = 0x05D00000U;  // VDP1 registers
static constexpr uint32_t kVdp2VramBase        = 0x05E00000U;  // VDP2 VRAM (512 KB)
static constexpr uint32_t kVdp2VramSize        = 0x00080000U;
static constexpr uint32_t kVdp2CramBase        = 0x05F00000U;  // VDP2 colour RAM (4 KB)
static constexpr uint32_t kVdp2CramSize        = 0x00001000U;
static constexpr uint32_t kVdp2RegsBase        = 0x05F80000U;  // VDP2 registers
static constexpr uint32_t kScsRegsBase         = 0x05A00000U;  // SCSP audio registers
static constexpr uint32_t kSoundRamBase        = 0x05A80000U;  // SCSP sound RAM (512 KB)
static constexpr uint32_t kSoundRamSize        = 0x00080000U;
static constexpr uint32_t kHighRamBase         = 0x06000000U;  // Work RAM-H (1 MB SDRAM)
static constexpr uint32_t kHighRamSize         = 0x00100000U;
static constexpr uint32_t kCdBaseAddr          = 0x04000000U;  // CD block registers

// Video timing (NTSC 320×224)
static constexpr uint16_t kVdpWidth            = 320;
static constexpr uint16_t kVdpHeight           = 224;
static constexpr uint16_t kScanlines           = 263;           // NTSC scanlines/frame
static constexpr uint32_t kCyclesPerScanline   = 508;           // SH-2 cycles per scanline

}  // namespace SaturnEmulator
