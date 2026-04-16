#pragma once

#include <cstdint>

namespace DreamcastEmulator {

// System clocks
static constexpr uint64_t kShClockHz          = 200000000ULL;  // SH-4 @ 200 MHz
static constexpr uint64_t kBusClockHz         = 100000000ULL;  // System bus @ 100 MHz
static constexpr uint64_t kAicaClockHz        =  44100ULL * 512; // AICA ARM7DI ~22.6 MHz
static constexpr uint64_t kVideoClockHz       =  13500000ULL;  // Pixel clock (13.5 MHz NTSC)

// Memory layout (physical, SH-4 P4 and area-decoded)
static constexpr uint32_t kBootRomBase        = 0x00000000U;  // Boot ROM (2 MB)
static constexpr uint32_t kBootRomSize        = 0x00200000U;
static constexpr uint32_t kFlashBase          = 0x00200000U;  // Flash ROM (128 KB settings)
static constexpr uint32_t kFlashSize          = 0x00020000U;
static constexpr uint32_t kRamBase            = 0x0C000000U;  // System RAM (16 MB SDRAM)
static constexpr uint32_t kRamSize            = 0x01000000U;
static constexpr uint32_t kVramBase           = 0x04000000U;  // VRAM (8 MB)
static constexpr uint32_t kVramSize           = 0x00800000U;
static constexpr uint32_t kAicaBase           = 0x00700000U;  // AICA wave memory  (8 MB)
static constexpr uint32_t kAicaSize           = 0x00800000U;
static constexpr uint32_t kAicaRegsBase       = 0x00702C00U;  // AICA register block
static constexpr uint32_t kPvrRegsBase        = 0x005F8000U;  // PowerVR2 registers
static constexpr uint32_t kTaFifoBase         = 0x10000000U;  // TA command FIFO
static constexpr uint32_t kGdRomBase          = 0x005F7000U;  // GD-ROM registers
static constexpr uint32_t kMapleBase          = 0x005F6C00U;  // Maple bus registers
static constexpr uint32_t kG2Base             = 0x005F7400U;  // G2 bus bridge

// Video timing (NTSC 640×480 native, 320×240 common)
static constexpr uint16_t kDisplayWidth       = 640;
static constexpr uint16_t kDisplayHeight      = 480;
static constexpr uint16_t kScanlinesPerFrame  = 525;
static constexpr uint32_t kCyclesPerScanline  = 857;   // SH-4 cycles per NTSC line

// GD-ROM disc
static constexpr uint32_t kDiscSectorSize     = 2352;

}  // namespace DreamcastEmulator
