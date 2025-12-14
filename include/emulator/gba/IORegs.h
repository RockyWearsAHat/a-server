#pragma once
#include <cstdint>

namespace AIO::Emulator::GBA {

// ==================== MEMORY MAP (GBATEK) ====================
namespace MemoryMap {
// BIOS ROM: 0x00000000 - 0x00003FFF
inline constexpr uint32_t BIOS_START   = 0x00000000;
inline constexpr uint32_t BIOS_END     = 0x00003FFF;
inline constexpr uint32_t BIOS_SIZE    = 16 * 1024;

// On-board WRAM: 0x02000000 - 0x0203FFFF
inline constexpr uint32_t WRAM_BOARD_START  = 0x02000000;
inline constexpr uint32_t WRAM_BOARD_END    = 0x0203FFFF;
inline constexpr uint32_t WRAM_BOARD_SIZE   = 256 * 1024;
inline constexpr uint32_t WRAM_BOARD_MASK   = 0x3FFFF;

// On-chip WRAM: 0x03000000 - 0x03007FFF
inline constexpr uint32_t WRAM_CHIP_START   = 0x03000000;
inline constexpr uint32_t WRAM_CHIP_END     = 0x03007FFF;
inline constexpr uint32_t WRAM_CHIP_SIZE    = 32 * 1024;
inline constexpr uint32_t WRAM_CHIP_MASK    = 0x7FFF;

// I/O Registers: 0x04000000 - 0x040003FF
inline constexpr uint32_t IO_REG_START      = 0x04000000;
inline constexpr uint32_t IO_REG_END        = 0x040003FF;
inline constexpr uint32_t IO_REG_SIZE       = 0x400;
inline constexpr uint32_t IO_REG_MASK       = 0x3FF;

// Palette RAM: 0x05000000 - 0x050003FF
inline constexpr uint32_t PALETTE_START     = 0x05000000;
inline constexpr uint32_t PALETTE_END       = 0x050003FF;
inline constexpr uint32_t PALETTE_SIZE      = 1 * 1024;
inline constexpr uint32_t PALETTE_MASK      = 0x3FF;

// VRAM: 0x06000000 - 0x06017FFF
inline constexpr uint32_t VRAM_START        = 0x06000000;
inline constexpr uint32_t VRAM_END          = 0x06017FFF;
inline constexpr uint32_t VRAM_SIZE         = 96 * 1024;
inline constexpr uint32_t VRAM_MASK_POW2    = 0xFFFF; // For 64K masking
inline constexpr uint32_t VRAM_ACTUAL_SIZE  = 0x18000; // Actual non-POW2 size

// OAM: 0x07000000 - 0x070003FF
inline constexpr uint32_t OAM_START         = 0x07000000;
inline constexpr uint32_t OAM_END           = 0x070003FF;
inline constexpr uint32_t OAM_SIZE          = 1 * 1024;
inline constexpr uint32_t OAM_MASK          = 0x3FF;

// Game Pak ROM: 0x08000000 - 0x0DFFFFFF (32MB max)
inline constexpr uint32_t ROM_START         = 0x08000000;
inline constexpr uint32_t ROM_END           = 0x0DFFFFFF;
inline constexpr uint32_t ROM_MAX_SIZE      = 32 * 1024 * 1024;
inline constexpr uint32_t ROM_MIRROR_MASK   = 0x01FFFFFF;

// Game Pak SRAM/EEPROM: 0x0E000000 - 0x0E00FFFF
inline constexpr uint32_t SAVE_START        = 0x0E000000;
inline constexpr uint32_t SAVE_END          = 0x0E00FFFF;
inline constexpr uint32_t SAVE_SIZE_MAX     = 0x10000;

// EEPROM I/O: 0x0D000000
inline constexpr uint32_t EEPROM_IO_ADDR    = 0x0D000000;

} // namespace MemoryMap

// ==================== I/O REGISTERS (GBATEK) ====================
namespace IORegs {
inline constexpr uint32_t BASE = 0x04000000;

// Display Registers
inline constexpr uint32_t DISPCNT      = 0x000; // Display Control
inline constexpr uint32_t DISPSTAT     = 0x004; // Display Status
inline constexpr uint32_t VCOUNT       = 0x006; // Vertical Counter
inline constexpr uint32_t BG0CNT       = 0x008; // BG0 Control
inline constexpr uint32_t BG1CNT       = 0x00A; // BG1 Control
inline constexpr uint32_t BG2CNT       = 0x00C; // BG2 Control
inline constexpr uint32_t BG3CNT       = 0x00E; // BG3 Control
inline constexpr uint32_t BG0HOFS      = 0x010; // BG0 Horizontal Offset
inline constexpr uint32_t BG0VOFS      = 0x012; // BG0 Vertical Offset
inline constexpr uint32_t BG1HOFS      = 0x014; // BG1 Horizontal Offset
inline constexpr uint32_t BG1VOFS      = 0x016; // BG1 Vertical Offset
inline constexpr uint32_t BG2HOFS      = 0x018; // BG2 Horizontal Offset
inline constexpr uint32_t BG2VOFS      = 0x01A; // BG2 Vertical Offset
inline constexpr uint32_t BG3HOFS      = 0x01C; // BG3 Horizontal Offset
inline constexpr uint32_t BG3VOFS      = 0x01E; // BG3 Vertical Offset
inline constexpr uint32_t BG2PA        = 0x020; // BG2 Rotation/Scale A
inline constexpr uint32_t BG2PB        = 0x022; // BG2 Rotation/Scale B
inline constexpr uint32_t BG2PC        = 0x024; // BG2 Rotation/Scale C
inline constexpr uint32_t BG2PD        = 0x026; // BG2 Rotation/Scale D
inline constexpr uint32_t BG2X         = 0x028; // BG2 Reference X
inline constexpr uint32_t BG2Y         = 0x02C; // BG2 Reference Y
inline constexpr uint32_t BG3PA        = 0x030; // BG3 Rotation/Scale A
inline constexpr uint32_t BG3PB        = 0x032; // BG3 Rotation/Scale B
inline constexpr uint32_t BG3PC        = 0x034; // BG3 Rotation/Scale C
inline constexpr uint32_t BG3PD        = 0x036; // BG3 Rotation/Scale D
inline constexpr uint32_t BG3X         = 0x038; // BG3 Reference X
inline constexpr uint32_t BG3Y         = 0x03C; // BG3 Reference Y
inline constexpr uint32_t WIN0H        = 0x040; // Window 0 Horizontal
inline constexpr uint32_t WIN1H        = 0x042; // Window 1 Horizontal
inline constexpr uint32_t WIN0V        = 0x044; // Window 0 Vertical
inline constexpr uint32_t WIN1V        = 0x046; // Window 1 Vertical
inline constexpr uint32_t WININ        = 0x048; // Window IN (0, 1)
inline constexpr uint32_t WINOUT       = 0x04A; // Window OUT
inline constexpr uint32_t MOSAIC       = 0x04C; // Mosaic
inline constexpr uint32_t BLDCNT       = 0x050; // Blend Control
inline constexpr uint32_t BLDALPHA     = 0x052; // Blend Alpha
inline constexpr uint32_t BLDY         = 0x054; // Blend Brightness

// Sound Registers
inline constexpr uint32_t SOUND1CNT_L  = 0x060; // Sound 1 Sweep
inline constexpr uint32_t SOUND1CNT_H  = 0x062; // Sound 1 Length/Envelope
inline constexpr uint32_t SOUND1CNT_X  = 0x064; // Sound 1 Frequency
inline constexpr uint32_t SOUND2CNT_L  = 0x068; // Sound 2 Length/Envelope
inline constexpr uint32_t SOUND2CNT_H  = 0x06C; // Sound 2 Frequency
inline constexpr uint32_t SOUND3CNT_L  = 0x070; // Sound 3 Enable
inline constexpr uint32_t SOUND3CNT_H  = 0x072; // Sound 3 Length/Volume
inline constexpr uint32_t SOUND3CNT_X  = 0x074; // Sound 3 Frequency
inline constexpr uint32_t SOUND4CNT_L  = 0x078; // Sound 4 Length/Envelope
inline constexpr uint32_t SOUND4CNT_H  = 0x07C; // Sound 4 Frequency
inline constexpr uint32_t SOUNDCNT_L   = 0x080; // Sound Control L
inline constexpr uint32_t SOUNDCNT_H   = 0x082; // Sound Control H (DMA)
inline constexpr uint32_t SOUNDCNT_X   = 0x084; // Sound Control X (Master)
inline constexpr uint32_t SOUNDBIAS    = 0x088; // Sound Bias
inline constexpr uint32_t WAVE_RAM     = 0x090; // Wave Pattern RAM
inline constexpr uint32_t FIFO_A       = 0x0A0; // DMA Sound FIFO A
inline constexpr uint32_t FIFO_B       = 0x0A4; // DMA Sound FIFO B

// DMA Registers (4 channels)
inline constexpr uint32_t DMA0SAD      = 0x0B0; // DMA0 Source
inline constexpr uint32_t DMA0DAD      = 0x0B4; // DMA0 Destination
inline constexpr uint32_t DMA0CNT_L    = 0x0B8; // DMA0 Count
inline constexpr uint32_t DMA0CNT_H    = 0x0BA; // DMA0 Control
inline constexpr uint32_t DMA1SAD      = 0x0BC; // DMA1 Source
inline constexpr uint32_t DMA1DAD      = 0x0C0; // DMA1 Destination
inline constexpr uint32_t DMA1CNT_L    = 0x0C4; // DMA1 Count
inline constexpr uint32_t DMA1CNT_H    = 0x0C6; // DMA1 Control
inline constexpr uint32_t DMA2SAD      = 0x0C8; // DMA2 Source
inline constexpr uint32_t DMA2DAD      = 0x0CC; // DMA2 Destination
inline constexpr uint32_t DMA2CNT_L    = 0x0D0; // DMA2 Count
inline constexpr uint32_t DMA2CNT_H    = 0x0D2; // DMA2 Control
inline constexpr uint32_t DMA3SAD      = 0x0D4; // DMA3 Source
inline constexpr uint32_t DMA3DAD      = 0x0D8; // DMA3 Destination
inline constexpr uint32_t DMA3CNT_L    = 0x0DC; // DMA3 Count
inline constexpr uint32_t DMA3CNT_H    = 0x0DE; // DMA3 Control
inline constexpr uint32_t DMA_CHANNEL_SIZE = 0x00C;

// Timer Registers (4 timers)
inline constexpr uint32_t TM0CNT_L     = 0x100; // Timer 0 Counter
inline constexpr uint32_t TM0CNT_H     = 0x102; // Timer 0 Control
inline constexpr uint32_t TM1CNT_L     = 0x104; // Timer 1 Counter
inline constexpr uint32_t TM1CNT_H     = 0x106; // Timer 1 Control
inline constexpr uint32_t TM2CNT_L     = 0x108; // Timer 2 Counter
inline constexpr uint32_t TM2CNT_H     = 0x10A; // Timer 2 Control
inline constexpr uint32_t TM3CNT_L     = 0x10C; // Timer 3 Counter
inline constexpr uint32_t TM3CNT_H     = 0x10E; // Timer 3 Control
inline constexpr uint32_t TIMER_CHANNEL_SIZE = 0x004;

// Serial Registers
inline constexpr uint32_t SIODATA32    = 0x120; // Serial I/O 32-bit
inline constexpr uint32_t SIOMULTI0    = 0x120; // Serial Multi-boot 0
inline constexpr uint32_t SIOMULTI1    = 0x122; // Serial Multi-boot 1
inline constexpr uint32_t SIOMULTI2    = 0x124; // Serial Multi-boot 2
inline constexpr uint32_t SIOMULTI3    = 0x126; // Serial Multi-boot 3
inline constexpr uint32_t SIOCNT       = 0x128; // Serial Control
inline constexpr uint32_t SIOMLT_SEND  = 0x12A; // Serial Multi-boot Send
inline constexpr uint32_t SIODATA8     = 0x12A; // Serial 8-bit

// Keypad
inline constexpr uint32_t KEYINPUT     = 0x130; // Key Input
inline constexpr uint32_t KEYCNT       = 0x132; // Key Interrupt Control

// Interrupt Registers
inline constexpr uint32_t IE           = 0x200; // Interrupt Enable
inline constexpr uint32_t IF           = 0x202; // Interrupt Flags
inline constexpr uint32_t WAITCNT      = 0x204; // Waitstate Control
inline constexpr uint32_t IME          = 0x208; // Master Interrupt Enable

// Absolute addresses
inline constexpr uint32_t REG_DISPCNT    = BASE + DISPCNT;
inline constexpr uint32_t REG_DISPSTAT   = BASE + DISPSTAT;
inline constexpr uint32_t REG_VCOUNT     = BASE + VCOUNT;
inline constexpr uint32_t REG_BG0CNT     = BASE + BG0CNT;
inline constexpr uint32_t REG_BG1CNT     = BASE + BG1CNT;
inline constexpr uint32_t REG_BG2CNT     = BASE + BG2CNT;
inline constexpr uint32_t REG_BG3CNT     = BASE + BG3CNT;
inline constexpr uint32_t REG_SOUNDCNT_H = BASE + SOUNDCNT_H;
inline constexpr uint32_t REG_SOUNDCNT_X = BASE + SOUNDCNT_X;
inline constexpr uint32_t REG_FIFO_A     = BASE + FIFO_A;
inline constexpr uint32_t REG_FIFO_B     = BASE + FIFO_B;
inline constexpr uint32_t REG_DMA0CNT_H  = BASE + DMA0CNT_H;
inline constexpr uint32_t REG_TM0CNT_L   = BASE + TM0CNT_L;
inline constexpr uint32_t REG_TM0CNT_H   = BASE + TM0CNT_H;
inline constexpr uint32_t REG_IE         = BASE + IE;
inline constexpr uint32_t REG_IF         = BASE + IF;
inline constexpr uint32_t REG_WAITCNT    = BASE + WAITCNT;
inline constexpr uint32_t REG_IME        = BASE + IME;
inline constexpr uint32_t REG_KEYINPUT   = BASE + KEYINPUT;
inline constexpr uint32_t REG_KEYCNT     = BASE + KEYCNT;
} // namespace IORegs

// ==================== BIOS ADDRESSES ====================
namespace BIOS {
// Interrupt/Exception vectors (in BIOS)
inline constexpr uint32_t VECTOR_RESET      = 0x00000000;
inline constexpr uint32_t VECTOR_UNDEF      = 0x00000004;
inline constexpr uint32_t VECTOR_SWI        = 0x00000008;
inline constexpr uint32_t VECTOR_PREFETCH   = 0x0000000C;
inline constexpr uint32_t VECTOR_DATA_ABORT = 0x00000010;
inline constexpr uint32_t VECTOR_RESERVED   = 0x00000014;
inline constexpr uint32_t VECTOR_IRQ        = 0x00000018;
inline constexpr uint32_t VECTOR_FIQ        = 0x0000001C;

// IWRAM fixed locations (set by BIOS)
inline constexpr uint32_t IWRAM_BIOS_IF     = 0x03007FF8; // Interrupt flags (read by user handler)
inline constexpr uint32_t IWRAM_BIOS_HANDLER = 0x03007FFC; // User interrupt handler pointer
inline constexpr uint32_t IWRAM_BIOS_VBLANK_INTR_WAIT_FLAGS = 0x03007FF4; // Pre-saved trigger bits

// Entry point after BIOS boot
inline constexpr uint32_t ENTRY_POINT        = 0x08000000; // Game ROM start
} // namespace BIOS

// ==================== DISPLAY ====================
namespace Display {
inline constexpr uint32_t WIDTH         = 240;
inline constexpr uint32_t HEIGHT        = 160;
inline constexpr uint32_t VBLANK_START  = 160;
inline constexpr uint32_t VBLANK_END    = 227;
inline constexpr uint32_t CYCLES_PER_LINE = 960; // CPU cycles per scanline
inline constexpr uint32_t CYCLES_HBLANK = 272;
inline constexpr uint32_t CYCLES_HDRAW  = 1232 - 272;
} // namespace Display

// ==================== EEPROM ====================
namespace EEPROM {
inline constexpr uint32_t SIZE_4K    = 512;
inline constexpr uint32_t SIZE_64K   = 8192;
inline constexpr uint8_t ERASED_VALUE = 0xFF;
inline constexpr uint8_t BLANK_VALUE  = 0x00;
} // namespace EEPROM

// ==================== SAVE TYPES ====================
namespace SaveTypes {
inline constexpr uint32_t FLASH_512K_SIZE   = 64 * 1024;
inline constexpr uint32_t FLASH_1M_SIZE     = 128 * 1024;
inline constexpr uint32_t SRAM_SIZE         = 32 * 1024;
inline constexpr uint8_t FLASH_MAKER_ID     = 0xC2; // Macronix
inline constexpr uint8_t FLASH_DEVICE_512K  = 0x1C;
inline constexpr uint8_t FLASH_DEVICE_1M    = 0x09;
inline constexpr uint32_t FLASH_SECTOR_SIZE = 0x1000; // 4KB
} // namespace SaveTypes

namespace DMAControl {
inline constexpr uint16_t DEST_ADDR_CONTROL_MASK = 0x0060;
inline constexpr uint16_t DEST_INCREMENT         = 0x0000;
inline constexpr uint16_t DEST_DECREMENT         = 0x0020;
inline constexpr uint16_t DEST_FIXED             = 0x0040;
inline constexpr uint16_t DEST_INCREMENT_RELOAD  = 0x0060;
inline constexpr uint16_t SRC_ADDR_CONTROL_MASK  = 0x0180;
inline constexpr uint16_t SRC_INCREMENT          = 0x0000;
inline constexpr uint16_t SRC_DECREMENT          = 0x0080;
inline constexpr uint16_t SRC_FIXED              = 0x0100;
inline constexpr uint16_t REPEAT                 = 0x0200;
inline constexpr uint16_t TRANSFER_32BIT         = 0x0400;
inline constexpr uint16_t START_TIMING_MASK      = 0x3000;
inline constexpr uint16_t START_IMMEDIATE        = 0x0000;
inline constexpr uint16_t START_VBLANK           = 0x1000;
inline constexpr uint16_t START_HBLANK           = 0x2000;
inline constexpr uint16_t START_SPECIAL          = 0x3000;
inline constexpr uint16_t IRQ_ENABLE             = 0x4000;
inline constexpr uint16_t ENABLE                 = 0x8000;
}

namespace TimerControl {
inline constexpr uint16_t PRESCALER_MASK         = 0x0003;
inline constexpr uint16_t PRESCALER_1            = 0x0000;
inline constexpr uint16_t PRESCALER_64           = 0x0001;
inline constexpr uint16_t PRESCALER_256          = 0x0002;
inline constexpr uint16_t PRESCALER_1024         = 0x0003;
inline constexpr uint16_t COUNT_UP               = 0x0004;
inline constexpr uint16_t IRQ_ENABLE             = 0x0040;
inline constexpr uint16_t ENABLE                 = 0x0080;
}

namespace InterruptFlags {
inline constexpr uint16_t VBLANK  = 0x0001;
inline constexpr uint16_t HBLANK  = 0x0002;
inline constexpr uint16_t VCOUNT  = 0x0004;
inline constexpr uint16_t TIMER0  = 0x0008;
inline constexpr uint16_t TIMER1  = 0x0010;
inline constexpr uint16_t TIMER2  = 0x0020;
inline constexpr uint16_t TIMER3  = 0x0040;
inline constexpr uint16_t SERIAL  = 0x0080;
inline constexpr uint16_t DMA0    = 0x0100;
inline constexpr uint16_t DMA1    = 0x0200;
inline constexpr uint16_t DMA2    = 0x0400;
inline constexpr uint16_t DMA3    = 0x0800;
inline constexpr uint16_t KEYPAD  = 0x1000;
inline constexpr uint16_t GAMEPAK = 0x2000;
}

} // namespace AIO::Emulator::GBA
