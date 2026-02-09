#pragma once

// PS1 Hardware Constants — Single source of truth
// All magic numbers, timing values, memory sizes, and register addresses
// are defined here. NO magic numbers in implementation files.

#include <cstdint>

namespace AIO::Emulator::PS1 {

// ─── Clock Rates ────────────────────────────────────────────────────────
namespace Clock {
inline constexpr uint32_t CPU_HZ = 33868800;
inline constexpr uint32_t CPU_HZ_NTSC = 33868800;
inline constexpr uint32_t CPU_HZ_PAL = 33868800;
inline constexpr double FRAME_RATE_NTSC = 59.94;
inline constexpr double FRAME_RATE_PAL = 50.0;
inline constexpr uint32_t SCANLINES_NTSC = 263;
inline constexpr uint32_t SCANLINES_PAL = 314;
inline constexpr uint32_t VISIBLE_SCANLINES_NTSC = 240;
inline constexpr uint32_t VISIBLE_SCANLINES_PAL = 256;
inline constexpr uint32_t DOTS_PER_SCANLINE_NTSC = 3413;
inline constexpr uint32_t DOTS_PER_SCANLINE_PAL = 3406;
inline constexpr uint32_t CPU_CYCLES_PER_SCANLINE_NTSC = 2171;
inline constexpr uint32_t CPU_CYCLES_PER_SCANLINE_PAL = 2165;
inline constexpr uint32_t SPU_SAMPLE_RATE = 44100;
} // namespace Clock

// ─── Memory Sizes ───────────────────────────────────────────────────────
namespace MemSize {
inline constexpr uint32_t RAM = 0x200000;        // 2 MB
inline constexpr uint32_t BIOS = 0x80000;        // 512 KB
inline constexpr uint32_t SCRATCHPAD = 0x400;    // 1 KB
inline constexpr uint32_t VRAM = 0x100000;       // 1 MB
inline constexpr uint32_t SPU_RAM = 0x80000;     // 512 KB
inline constexpr uint32_t EXPANSION1 = 0x800000; // 8 MB
inline constexpr uint32_t EXPANSION2 = 0x1000;   // 4 KB
inline constexpr uint32_t IO_PORTS = 0x2000;     // 8 KB
} // namespace MemSize

// ─── Memory Map (Physical Addresses) ────────────────────────────────────
namespace MemMap {
inline constexpr uint32_t RAM_START = 0x00000000;
inline constexpr uint32_t RAM_END = 0x001FFFFF;
inline constexpr uint32_t RAM_MIRROR_SIZE = 0x200000;
inline constexpr uint32_t RAM_REGION_SIZE = 0x800000; // 8 MB region (mirrors)

inline constexpr uint32_t EXPANSION1_START = 0x1F000000;
inline constexpr uint32_t EXPANSION1_END = 0x1F7FFFFF;

inline constexpr uint32_t SCRATCHPAD_START = 0x1F800000;
inline constexpr uint32_t SCRATCHPAD_END = 0x1F8003FF;

inline constexpr uint32_t IO_START = 0x1F801000;
inline constexpr uint32_t IO_END = 0x1F802FFF;

inline constexpr uint32_t EXPANSION2_START = 0x1F802000;
inline constexpr uint32_t EXPANSION2_END = 0x1F802FFF;

inline constexpr uint32_t EXPANSION3_START = 0x1FA00000;
inline constexpr uint32_t EXPANSION3_END = 0x1FBFFFFF;

inline constexpr uint32_t BIOS_START = 0x1FC00000;
inline constexpr uint32_t BIOS_END = 0x1FC7FFFF;

inline constexpr uint32_t CACHE_CTRL_START = 0xFFFE0000;
inline constexpr uint32_t CACHE_CTRL_END = 0xFFFE01FF;

// KSEG masks for virtual → physical address translation
inline constexpr uint32_t KSEG_MASK = 0x1FFFFFFF;
inline constexpr uint32_t KSEG0_START = 0x80000000;
inline constexpr uint32_t KSEG0_END = 0x9FFFFFFF;
inline constexpr uint32_t KSEG1_START = 0xA0000000;
inline constexpr uint32_t KSEG1_END = 0xBFFFFFFF;
inline constexpr uint32_t KSEG2_START = 0xC0000000;
} // namespace MemMap

// ─── I/O Register Addresses ─────────────────────────────────────────────
namespace IO {

// Memory Control
inline constexpr uint32_t MEM_CTRL1_START = 0x1F801000;
inline constexpr uint32_t RAM_SIZE = 0x1F801060;

// Interrupt Controller
inline constexpr uint32_t I_STAT = 0x1F801070;
inline constexpr uint32_t I_MASK = 0x1F801074;

// DMA Registers
inline constexpr uint32_t DMA_BASE = 0x1F801080;
inline constexpr uint32_t DMA_CHANNEL_SIZE = 0x10;
inline constexpr uint32_t DMA_DPCR = 0x1F8010F0;
inline constexpr uint32_t DMA_DICR = 0x1F8010F4;

// Timer Registers
inline constexpr uint32_t TIMER_BASE = 0x1F801100;
inline constexpr uint32_t TIMER_CHANNEL_SIZE = 0x10;

// Controller / Memory Card (SIO0)
inline constexpr uint32_t SIO0_DATA = 0x1F801040;
inline constexpr uint32_t SIO0_STAT = 0x1F801044;
inline constexpr uint32_t SIO0_MODE = 0x1F801048;
inline constexpr uint32_t SIO0_CTRL = 0x1F80104A;
inline constexpr uint32_t SIO0_BAUD = 0x1F80104E;

// Serial Port (SIO1)
inline constexpr uint32_t SIO1_DATA = 0x1F801050;
inline constexpr uint32_t SIO1_STAT = 0x1F801054;
inline constexpr uint32_t SIO1_MODE = 0x1F801058;
inline constexpr uint32_t SIO1_CTRL = 0x1F80105A;
inline constexpr uint32_t SIO1_BAUD = 0x1F80105E;

// CD-ROM
inline constexpr uint32_t CDROM_BASE = 0x1F801800;
inline constexpr uint32_t CDROM_REG0 = 0x1F801800;
inline constexpr uint32_t CDROM_REG1 = 0x1F801801;
inline constexpr uint32_t CDROM_REG2 = 0x1F801802;
inline constexpr uint32_t CDROM_REG3 = 0x1F801803;

// GPU
inline constexpr uint32_t GPU_GP0 = 0x1F801810;
inline constexpr uint32_t GPU_GPUREAD = 0x1F801810;
inline constexpr uint32_t GPU_GP1 = 0x1F801814;
inline constexpr uint32_t GPU_GPUSTAT = 0x1F801814;

// MDEC
inline constexpr uint32_t MDEC_CMD = 0x1F801820;
inline constexpr uint32_t MDEC_DATA = 0x1F801820;
inline constexpr uint32_t MDEC_CTRL = 0x1F801824;
inline constexpr uint32_t MDEC_STAT = 0x1F801824;

// SPU
inline constexpr uint32_t SPU_START = 0x1F801C00;
inline constexpr uint32_t SPU_END = 0x1F801FFF;
inline constexpr uint32_t SPU_VOICE_BASE = 0x1F801C00;
inline constexpr uint32_t SPU_VOICE_SIZE = 0x10;
inline constexpr uint32_t SPU_MAIN_VOL_L = 0x1F801D80;
inline constexpr uint32_t SPU_MAIN_VOL_R = 0x1F801D82;
inline constexpr uint32_t SPU_REVERB_VOL_L = 0x1F801D84;
inline constexpr uint32_t SPU_REVERB_VOL_R = 0x1F801D86;
inline constexpr uint32_t SPU_KEY_ON = 0x1F801D88;
inline constexpr uint32_t SPU_KEY_OFF = 0x1F801D8C;
inline constexpr uint32_t SPU_FM_MODE = 0x1F801D90;
inline constexpr uint32_t SPU_NOISE_MODE = 0x1F801D94;
inline constexpr uint32_t SPU_REVERB_ON = 0x1F801D98;
inline constexpr uint32_t SPU_VOICE_STATUS = 0x1F801D9C;
inline constexpr uint32_t SPU_REVERB_BASE = 0x1F801DA2;
inline constexpr uint32_t SPU_IRQ_ADDR = 0x1F801DA4;
inline constexpr uint32_t SPU_DATA_ADDR = 0x1F801DA6;
inline constexpr uint32_t SPU_DATA_FIFO = 0x1F801DA8;
inline constexpr uint32_t SPU_CTRL = 0x1F801DAA;
inline constexpr uint32_t SPU_TRANSFER_CTRL = 0x1F801DAC;
inline constexpr uint32_t SPU_STATUS = 0x1F801DAE;
inline constexpr uint32_t SPU_CD_VOL_L = 0x1F801DB0;
inline constexpr uint32_t SPU_CD_VOL_R = 0x1F801DB2;

} // namespace IO

// ─── CPU Constants ──────────────────────────────────────────────────────
namespace CPU {
inline constexpr uint32_t RESET_VECTOR = 0xBFC00000;
inline constexpr uint32_t EXCEPTION_VECTOR = 0x80000080;
inline constexpr uint32_t BOOT_EXCEPTION_VEC = 0xBFC00180;
inline constexpr uint32_t NUM_REGS = 32;
inline constexpr uint32_t REG_RA = 31;

// COP0 Register indices
namespace COP0 {
inline constexpr uint32_t BPC = 3;
inline constexpr uint32_t BDA = 5;
inline constexpr uint32_t JUMPDEST = 6;
inline constexpr uint32_t DCIC = 7;
inline constexpr uint32_t BADVADDR = 8;
inline constexpr uint32_t BDAM = 9;
inline constexpr uint32_t BPCM = 11;
inline constexpr uint32_t SR = 12;
inline constexpr uint32_t CAUSE = 13;
inline constexpr uint32_t EPC = 14;
inline constexpr uint32_t PRID = 15;
} // namespace COP0

// Status Register bits
namespace SR {
inline constexpr uint32_t IEc = 1 << 0;
inline constexpr uint32_t KUc = 1 << 1;
inline constexpr uint32_t IEp = 1 << 2;
inline constexpr uint32_t KUp = 1 << 3;
inline constexpr uint32_t IEo = 1 << 4;
inline constexpr uint32_t KUo = 1 << 5;
inline constexpr uint32_t IM_MASK = 0xFF00;
inline constexpr uint32_t Isc = 1 << 16;
inline constexpr uint32_t Swc = 1 << 17;
inline constexpr uint32_t BEV = 1 << 22;
inline constexpr uint32_t CU0 = 1 << 25;
inline constexpr uint32_t CU2 = 1 << 27;
} // namespace SR

// Exception cause codes
namespace ExcCode {
inline constexpr uint32_t INTERRUPT = 0x00;
inline constexpr uint32_t ADDR_LOAD = 0x04;
inline constexpr uint32_t ADDR_STORE = 0x05;
inline constexpr uint32_t BUS_FETCH = 0x06;
inline constexpr uint32_t BUS_DATA = 0x07;
inline constexpr uint32_t SYSCALL = 0x08;
inline constexpr uint32_t BREAKPOINT = 0x09;
inline constexpr uint32_t RESERVED_INSTR = 0x0A;
inline constexpr uint32_t COP_UNUSABLE = 0x0B;
inline constexpr uint32_t ARITHMETIC_OVERFLOW = 0x0C;
} // namespace ExcCode
} // namespace CPU

// ─── DMA Constants ──────────────────────────────────────────────────────
namespace DMA {
inline constexpr uint32_t NUM_CHANNELS = 7;

namespace Channel {
inline constexpr uint32_t MDEC_IN = 0;
inline constexpr uint32_t MDEC_OUT = 1;
inline constexpr uint32_t GPU = 2;
inline constexpr uint32_t CDROM = 3;
inline constexpr uint32_t SPU = 4;
inline constexpr uint32_t PIO = 5;
inline constexpr uint32_t OTC = 6;
} // namespace Channel

namespace SyncMode {
inline constexpr uint32_t MANUAL = 0;
inline constexpr uint32_t REQUEST = 1;
inline constexpr uint32_t LINKED_LIST = 2;
} // namespace SyncMode
} // namespace DMA

// ─── GPU Constants ──────────────────────────────────────────────────────
namespace GPU {
inline constexpr uint32_t VRAM_WIDTH = 1024;
inline constexpr uint32_t VRAM_HEIGHT = 512;
inline constexpr uint32_t VRAM_SIZE_PIXELS = VRAM_WIDTH * VRAM_HEIGHT;
inline constexpr uint32_t VRAM_SIZE_BYTES = VRAM_SIZE_PIXELS * 2;

namespace Resolution {
inline constexpr uint32_t W_256 = 256;
inline constexpr uint32_t W_320 = 320;
inline constexpr uint32_t W_368 = 368;
inline constexpr uint32_t W_512 = 512;
inline constexpr uint32_t W_640 = 640;
inline constexpr uint32_t H_240 = 240;
inline constexpr uint32_t H_480 = 480;
} // namespace Resolution
} // namespace GPU

// ─── Interrupt Bits ─────────────────────────────────────────────────────
namespace IRQ {
inline constexpr uint32_t VBLANK = 1 << 0;
inline constexpr uint32_t GPU_IRQ = 1 << 1;
inline constexpr uint32_t CDROM = 1 << 2;
inline constexpr uint32_t DMA = 1 << 3;
inline constexpr uint32_t TIMER0 = 1 << 4;
inline constexpr uint32_t TIMER1 = 1 << 5;
inline constexpr uint32_t TIMER2 = 1 << 6;
inline constexpr uint32_t SIO0 = 1 << 7;
inline constexpr uint32_t SIO1 = 1 << 8;
inline constexpr uint32_t SPU = 1 << 9;
inline constexpr uint32_t LIGHTPEN = 1 << 10;
} // namespace IRQ

// ─── Timer Constants ────────────────────────────────────────────────────
namespace Timer {
inline constexpr uint32_t NUM_TIMERS = 3;
} // namespace Timer

// ─── SPU Constants ──────────────────────────────────────────────────────
namespace SPU {
inline constexpr uint32_t NUM_VOICES = 24;
inline constexpr uint32_t ADPCM_BLOCK_SIZE = 16;
inline constexpr uint32_t ADPCM_SAMPLES_PER_BLOCK = 28;
inline constexpr uint16_t SAMPLE_RATE_BASE = 0x1000; // 0x1000 = 44.1 kHz
} // namespace SPU

// ─── Controller Constants ───────────────────────────────────────────────
namespace Controller {
inline constexpr uint8_t DIGITAL_PAD_ID = 0x41;
inline constexpr uint8_t ANALOG_PAD_ID = 0x73;
inline constexpr uint8_t READY_BYTE = 0x5A;
} // namespace Controller

// PS1 digital pad button bits (active LOW: 0 = pressed, 1 = released)
namespace PadButton {
inline constexpr uint16_t Select = 1 << 0;
inline constexpr uint16_t L3 = 1 << 1;
inline constexpr uint16_t R3 = 1 << 2;
inline constexpr uint16_t Start = 1 << 3;
inline constexpr uint16_t Up = 1 << 4;
inline constexpr uint16_t Right = 1 << 5;
inline constexpr uint16_t Down = 1 << 6;
inline constexpr uint16_t Left = 1 << 7;
inline constexpr uint16_t L2 = 1 << 8;
inline constexpr uint16_t R2 = 1 << 9;
inline constexpr uint16_t L1 = 1 << 10;
inline constexpr uint16_t R1 = 1 << 11;
inline constexpr uint16_t Triangle = 1 << 12;
inline constexpr uint16_t Circle = 1 << 13;
inline constexpr uint16_t Cross = 1 << 14;
inline constexpr uint16_t Square = 1 << 15;
} // namespace PadButton

// ─── Debug Tracing (compile-time flags — zero-cost when disabled) ──────
namespace Trace {
inline constexpr bool CPU = false;
inline constexpr bool CPU_REGS = false;
inline constexpr bool MEMORY = false;
inline constexpr bool DMA_TRACE = false;
inline constexpr bool GPU_CMD = false;
inline constexpr bool GPU_RENDER = false;
inline constexpr bool SPU_TRACE = false;
inline constexpr bool CDROM_TRACE = false;
inline constexpr bool IRQ_TRACE = false;
inline constexpr bool TIMER_TRACE = false;
inline constexpr bool GTE_TRACE = false;
inline constexpr bool CONTROLLER_TRACE = false;
inline constexpr bool EXCEPTIONS = false;
} // namespace Trace

} // namespace AIO::Emulator::PS1
