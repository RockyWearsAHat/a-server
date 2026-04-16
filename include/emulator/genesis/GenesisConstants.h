#pragma once
#include <cstdint>

/// Genesis/Mega Drive hardware constants.
/// Sources:
/// - Charles MacDonald, "Genesis Technical Overview" (Tier-1)
/// - Kabuto's VDP documentation (Tier-1)
/// - Eke-Eke genesis_plus_gx reference annotations (Tier-2)
/// - NESDev / SegaDev community wiki (Tier-2)

namespace AIO::Emulator::Genesis {

// ─── CPU clocks ───────────────────────────────────────────────────────────────
/// Master clock: 53.693175 MHz (NTSC) / 53.203424 MHz (PAL)
static constexpr uint32_t kMasterClockNTSC = 53'693'175u;
static constexpr uint32_t kMasterClockPAL  = 53'203'424u;

/// M68000 runs at master / 7
static constexpr uint32_t kM68kDivider    = 7u;
/// Z80 runs at master / 15
static constexpr uint32_t kZ80Divider     = 15u;
/// VDP pixel clock at master / 8 (H32) or master / 10 (H40)
static constexpr uint32_t kVdpDividerH32  = 8u;
static constexpr uint32_t kVdpDividerH40  = 10u;

// ─── Memory map — M68000 address space ───────────────────────────────────────
static constexpr uint32_t kRomStart       = 0x000000u;
static constexpr uint32_t kRomEnd         = 0x3FFFFFu; ///< Up to 4 MB ROM
static constexpr uint32_t kZ80BankStart   = 0xA00000u;
static constexpr uint32_t kZ80BankEnd     = 0xA0FFFFu;
static constexpr uint32_t kZ80BusReqAddr  = 0xA11100u;
static constexpr uint32_t kZ80ResetAddr   = 0xA11200u;
static constexpr uint32_t kVdpBaseAddr    = 0xC00000u;
static constexpr uint32_t kVdpDataPort    = 0xC00000u;
static constexpr uint32_t kVdpCtrlPort    = 0xC00004u;
static constexpr uint32_t kVdpHVCounter   = 0xC00008u;
static constexpr uint32_t kPsgPort        = 0xC00011u;
static constexpr uint32_t kRamStart       = 0xFF0000u;
static constexpr uint32_t kRamEnd         = 0xFFFFFFu;
static constexpr uint32_t kRamSize        = 0x10000u;  ///< 64 KB

// ─── Z80 address space ────────────────────────────────────────────────────────
static constexpr uint32_t kZ80RamBase     = 0x0000u;
static constexpr uint32_t kZ80RamSize     = 0x2000u;  ///< 8 KB
static constexpr uint32_t kYm2612Base     = 0x4000u;
static constexpr uint32_t kZ80PsgBase     = 0x7F11u;
static constexpr uint32_t kZ80BankWindow  = 0x8000u;  ///< 32 KB M68K bank window

// ─── VDP geometry ─────────────────────────────────────────────────────────────
static constexpr uint32_t kVdpVramSize    = 0x10000u; ///< 64 KB VRAM
static constexpr uint32_t kVdpCramSize    = 0x80u;    ///< 64 palette entries × 2 bytes
static constexpr uint32_t kVdpVsramSize   = 0x50u;    ///< 40 × 2 bytes vertical scroll

static constexpr int kLinesNTSC           = 262;
static constexpr int kLinesActiveNTSC     = 224; ///< 240 in Mode 4
static constexpr int kLinesPAL            = 312;
static constexpr int kLinesActivePAL      = 224; ///< 240 in PAL extended

static constexpr int kPixelsH32           = 256; ///< H32 mode
static constexpr int kPixelsH40           = 320; ///< H40 mode

// ─── Interrupt vectors ────────────────────────────────────────────────────────
/// M68000 interrupt levels used by the Genesis
static constexpr int kVdpVblankIrqLevel  = 6; ///< VINT
static constexpr int kVdpHblankIrqLevel  = 4; ///< HINT

// ─── Cartridge ───────────────────────────────────────────────────────────────
static constexpr uint32_t kMaxCartSize    = 0x400000u; ///< 4 MB max standard ROM
static constexpr uint32_t kSramStart      = 0x200000u;
static constexpr uint32_t kSramEnd        = 0x20FFFFu;

} // namespace AIO::Emulator::Genesis
