#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <emulator/common/Logger.h>
#include <emulator/gba/APU.h>
#include <emulator/gba/ARM7TDMI.h>
#include <emulator/gba/GBAMemory.h>
#include <emulator/gba/GameDB.h>
#include <emulator/gba/IORegs.h>
#include <emulator/gba/PPU.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace AIO::Emulator::GBA {

namespace {
constexpr uint32_t kIrqHandlerAddress = 0x03007FFCu;
constexpr uint32_t kIrqHandlerOffset = 0x7FFCu;
constexpr uint32_t kIrqHandlerDefault = 0x00003FF0u;

inline bool IsIwramMappedAddress(uint32_t address) {
  // IWRAM is 32KB at 0x03000000-0x03007FFF.
  // Hardware also mirrors the last 32KB of the 0x03xxxxxx region at
  // 0x03FF8000-0x03FFFFFF. Avoid aliasing arbitrary 0x03xxxxxx addresses (e.g.
  // 0x03057FFC) into IWRAM.
  if ((address & 0xFF000000u) != 0x03000000u)
    return false;
  const uint32_t lo = address & 0x00FFFFFFu;
  return (lo < 0x00008000u) || (lo >= 0x00FF8000u);
}

inline bool IsValidIrqHandlerAddress(uint32_t addr) {
  const uint32_t normalized = addr & ~1u;
  if (normalized == 0u)
    return false;
  if (normalized < 0x00004000u)
    return true; // BIOS space
  if (normalized >= 0x02000000u && normalized < 0x02040000u)
    return true; // EWRAM
  if (normalized >= 0x03000000u && normalized < 0x03008000u)
    return true; // IWRAM
  // ROM is valid, but exclude EEPROM I/O region (0x0Dxxxxxx).
  if (normalized >= 0x08000000u && normalized < 0x0D000000u)
    return true;
  return false;
}

inline bool EnvTruthy(const char *v) {
  return v != nullptr && v[0] != '\0' && v[0] != '0';
}

template <size_t N> inline bool EnvFlagCached(const char (&name)[N]) {
  static const bool enabled = EnvTruthy(std::getenv(name));
  return enabled;
}

bool TraceGbaSpam() { return EnvFlagCached("AIO_TRACE_GBA_SPAM"); }

inline bool IsPpuIoOffset(uint32_t offAligned) {
  switch (offAligned) {
  case 0x000: // DISPCNT
  case 0x008: // BG0CNT
  case 0x00A: // BG1CNT
  case 0x00C: // BG2CNT
  case 0x00E: // BG3CNT
  case 0x010: // BG0HOFS
  case 0x012: // BG0VOFS
  case 0x014: // BG1HOFS
  case 0x016: // BG1VOFS
  case 0x018: // BG2HOFS
  case 0x01A: // BG2VOFS
  case 0x01C: // BG3HOFS
  case 0x01E: // BG3VOFS
  case 0x040: // WIN0H
  case 0x042: // WIN1H
  case 0x044: // WIN0V
  case 0x046: // WIN1V
  case 0x048: // WININ
  case 0x04A: // WINOUT
  case 0x04C: // MOSAIC
  case 0x050: // BLDCNT
  case 0x052: // BLDALPHA
  case 0x054: // BLDY
    return true;
  default:
    return false;
  }
}

inline void TracePpuIoWrite8(uint32_t address, uint8_t value, uint32_t pc) {
  if (!EnvFlagCached("AIO_TRACE_PPU_IO_WRITES"))
    return;
  if ((address & 0xFF000000u) != 0x04000000u)
    return;
  const uint32_t off = address & 0x3FFu;
  const uint32_t offAligned = off & ~1u;
  if (!IsPpuIoOffset(offAligned))
    return;

  static uint8_t last8[0x400] = {};
  static bool seen8[0x400] = {};
  if (seen8[off] && last8[off] == value)
    return;
  seen8[off] = true;
  last8[off] = value;

  static int logs = 0;
  if (logs++ >= 50000)
    return;
  AIO::Emulator::Common::Logger::Instance().LogFmt(
      AIO::Emulator::Common::LogLevel::Info, "PPU_IO",
      "W8 off=0x%03x addr=0x%08x val=0x%02x PC=0x%08x", (unsigned)off,
      (unsigned)address, (unsigned)value, (unsigned)pc);
}

inline void TracePpuIoWrite16(uint32_t address, uint16_t value, uint32_t pc) {
  if (!EnvFlagCached("AIO_TRACE_PPU_IO_WRITES"))
    return;
  if ((address & 0xFF000000u) != 0x04000000u)
    return;
  const uint32_t offAligned = (address & 0x3FFu) & ~1u;
  if (!IsPpuIoOffset(offAligned))
    return;

  static uint16_t last16[0x400 / 2] = {};
  static bool seen16[0x400 / 2] = {};
  const uint32_t idx = offAligned >> 1;
  if (seen16[idx] && last16[idx] == value)
    return;
  seen16[idx] = true;
  last16[idx] = value;

  static int logs = 0;
  if (logs++ >= 50000)
    return;
  AIO::Emulator::Common::Logger::Instance().LogFmt(
      AIO::Emulator::Common::LogLevel::Info, "PPU_IO",
      "W16 off=0x%03x addr=0x%08x val=0x%04x PC=0x%08x", (unsigned)offAligned,
      (unsigned)address, (unsigned)value, (unsigned)pc);
}

inline void TracePpuIoWrite32(uint32_t address, uint32_t value, uint32_t pc) {
  if (!EnvFlagCached("AIO_TRACE_PPU_IO_WRITES"))
    return;
  if ((address & 0xFF000000u) != 0x04000000u)
    return;
  const uint32_t offAligned = (address & 0x3FFu) & ~3u;
  if (!IsPpuIoOffset(offAligned & ~1u) &&
      !IsPpuIoOffset((offAligned + 2u) & ~1u))
    return;

  static uint32_t last32[0x400 / 4] = {};
  static bool seen32[0x400 / 4] = {};
  const uint32_t idx = offAligned >> 2;
  if (seen32[idx] && last32[idx] == value)
    return;
  seen32[idx] = true;
  last32[idx] = value;

  static int logs = 0;
  if (logs++ >= 50000)
    return;
  AIO::Emulator::Common::Logger::Instance().LogFmt(
      AIO::Emulator::Common::LogLevel::Info, "PPU_IO",
      "W32 off=0x%03x addr=0x%08x val=0x%08x PC=0x%08x", (unsigned)offAligned,
      (unsigned)address, (unsigned)value, (unsigned)pc);
}
} // namespace

GBAMemory::GBAMemory() {
  // Initialize memory vectors with correct sizes (GBATEK)
  bios.resize(MemoryMap::BIOS_SIZE, 0);
  wram_board.resize(MemoryMap::WRAM_BOARD_SIZE);
  wram_chip.resize(MemoryMap::WRAM_CHIP_SIZE);
  io_regs.resize(MemoryMap::IO_REG_SIZE);
  palette_ram.resize(MemoryMap::PALETTE_SIZE);
  vram.resize(MemoryMap::VRAM_SIZE);
  oam.resize(MemoryMap::OAM_SIZE);
  // ROM and SRAM sizes depend on the loaded game, but we can set defaults
  rom.resize(MemoryMap::ROM_MAX_SIZE);
  sram.resize(SaveTypes::SRAM_SIZE);
  // Default EEPROM state is erased (0xFF); game-specific init happens in
  // LoadSave
  eepromData.resize(EEPROM::SIZE_64K, EEPROM::ERASED_VALUE);

  eepromIs64Kbit = true; // SMA2 uses 64Kbit EEPROM
  saveTypeLocked = false;

  // Initialize HLE BIOS
  // The BIOS is High-Level Emulated - we don't need the actual copyrighted BIOS
  // We just need to provide expected values at key addresses
  InitializeHLEBIOS();
}

GBAMemory::~GBAMemory() = default;

void GBAMemory::InitializeHLEBIOS() {
  // High-Level Emulated BIOS initialization
  // This provides the minimum BIOS content for games to boot
  // We use DirectBoot mode (start at 0x08000000) but initialize
  // hardware state as if the BIOS had run

  // Fill BIOS with NOP instructions (0xE320F000 = ARM NOP)
  // Real BIOS code is not executed, but region must be readable
  for (size_t i = 0; i < bios.size(); i += 4) {
    bios[i] = 0x00;
    bios[i + 1] = 0xF0;
    bios[i + 2] = 0x20;
    bios[i + 3] = 0xE3;
  }

  // BIOS entry points (required by games)
  // SWI table at 0x00-0x7F (32 SWI calls)
  for (int swi = 0; swi < 32; ++swi) {
    uint32_t addr = swi * 4;
    // Each entry: branch to actual handler
    // B instruction: 0xEA000000 | ((offset >> 2) & 0xFFFFFF)
    // For HLE, we just put NOPs since CPU will intercept SWI
    bios[addr] = 0x00;
    bios[addr + 1] = 0xF0;
    bios[addr + 2] = 0x20;
    bios[addr + 3] = 0xE3;
  }

  // BIOS IRQ vector at 0x18
  // Real BIOS branches into its internal IRQ dispatcher, which then calls the
  // user handler pointer at 0x03007FFC. We install a small dispatcher in the
  // BIOS region.
  //
  // IMPORTANT: Do NOT place this dispatcher at 0x180..0x1A4, because the real
  // BIOS uses 0x188/0x194 for VBlankIntrWait/IntrWait entrypoints. Placing a
  // trampoline there collides with direct-call BIOS usage in DirectBoot titles.

  constexpr uint32_t kIrqTrampolineBase = 0x00003F00u;

  // At 0x18: B kIrqTrampolineBase
  // B immediate uses PC-relative: target = (pc+8) + (imm24<<2)
  // imm24 = (kIrqTrampolineBase - (0x18+8)) >> 2
  const uint32_t imm24 = (kIrqTrampolineBase - 0x00000020u) >> 2;
  const uint32_t bInstr = 0xEA000000u | (imm24 & 0x00FFFFFFu);
  bios[0x18] = (uint8_t)(bInstr & 0xFFu);
  bios[0x19] = (uint8_t)((bInstr >> 8) & 0xFFu);
  bios[0x1A] = (uint8_t)((bInstr >> 16) & 0xFFu);
  bios[0x1B] = (uint8_t)((bInstr >> 24) & 0xFFu);

  // IRQ Trampoline
  // This trampoline is a minimal IRQ dispatcher suitable for DirectBoot.
  //
  // Key behaviors:
  // - Calls the user handler pointer stored at 0x03007FFC
  //   (accessed via mirror address 0x03FFFFFC).
  // - Runs the user handler in IRQ mode.
  // - Acknowledges/clears REG_IF *after* the user handler returns, using
  //   the triggered mask stored at 0x03007FF4 by the CPU IRQ entry path.
  //
  // Rationale: Many handlers read REG_IF to determine the IRQ source.
  // Clearing IF before the call can hide the source; clearing after the
  // call avoids IRQ storms if the handler forgets to ack.

  uint32_t base = kIrqTrampolineBase;
  const uint32_t trampoline[] = {
      // Real BIOS IRQ dispatcher behavior (simplified but compatible):
      // - Save volatile regs on SP_irq
      // - Switch to System mode (so user handler runs on SP_sys)
      // - Call user handler at [0x03FFFFFC] (mirror of 0x03007FFC)
      // - Switch back to IRQ mode
      // - Clear triggered IF bits (mask stored by CPU at 0x03007FF4)
      // - Restore regs and exception-return via SUBS PC, LR, #4

      // Save volatile regs in IRQ mode.
      0xE92D500F, // +0x00: STMDB SP!, {R0-R3, R12, LR}

      // Prepare pointer base for [0x03FFFFFC] (mirror of 0x03007FFC).
      0xE3A02404, // +0x04: MOV   R2, #0x04000000

      // Switch to System mode (preserve flags/IRQ mask).
      0xE10F3000, // +0x08: MRS   R3, CPSR
      0xE3C3301F, // +0x0c: BIC   R3, R3, #0x1F
      0xE383301F, // +0x10: ORR   R3, R3, #0x1F      ; System mode
      0xE129F003, // +0x14: MSR   CPSR_c, R3

      // Call user handler at [0x03FFFFFC].
      // Do NOT clear IF before the call; many dispatchers read REG_IF.
      0xE28FE000, // +0x18: ADD   LR, PC, #0
      0xE512F004, // +0x1c: LDR   PC, [R2, #-4]

      // Switch back to IRQ mode so we can pop from SP_irq and exception-return.
      0xE10F3000, // +0x20: MRS   R3, CPSR
      0xE3C3301F, // +0x24: BIC   R3, R3, #0x1F
      0xE3833012, // +0x28: ORR   R3, R3, #0x12      ; IRQ mode
      0xE129F003, // +0x2c: MSR   CPSR_c, R3

      // After user handler returns, clear the triggered IF bits.
      0xE3A02404, // +0x30: MOV   R2, #0x04000000
      0xE59F1010, // +0x34: LDR   R1, [PC, #16]     ; &0x03007FF4 (literal)
      0xE1D110B0, // +0x38: LDRH  R1, [R1]          ; triggered mask
      0xE2820F80, // +0x3c: ADD   R0, R2, #0x200     ; R0 = 0x04000200
      0xE1C010B2, // +0x40: STRH  R1, [R0, #2]       ; REG_IF (write-1-to-clear)

      // Restore regs and return from IRQ.
      0xE8BD500F, // +0x44: LDMIA SP!, {R0-R3, R12, LR}
      0xE25EF004, // +0x48: SUBS  PC, LR, #4

      0x03007FF4 // +0x4c: literal
  };

  for (size_t i = 0; i < sizeof(trampoline) / sizeof(uint32_t); ++i) {
    uint32_t instr = trampoline[i];
    bios[base + i * 4 + 0] = instr & 0xFF;
    bios[base + i * 4 + 1] = (instr >> 8) & 0xFF;
    bios[base + i * 4 + 2] = (instr >> 16) & 0xFF;
    bios[base + i * 4 + 3] = (instr >> 24) & 0xFF;
  }

  // VBlankIntrWait Trampoline at 0x200
  // MOV R0, #1
  // MOV R1, #1
  // SWI 0x04
  // BX LR
  base = 0x200;
  // MOV R0, #1
  bios[base + 0] = 0x01;
  bios[base + 1] = 0x00;
  bios[base + 2] = 0xA0;
  bios[base + 3] = 0xE3;
  // MOV R1, #1
  bios[base + 4] = 0x01;
  bios[base + 5] = 0x10;
  bios[base + 6] = 0xA0;
  bios[base + 7] = 0xE3;
  // SWI 0x04
  bios[base + 8] = 0x04;
  bios[base + 9] = 0x00;
  bios[base + 10] = 0x00;
  bios[base + 11] = 0xEF;
  // BX LR
  bios[base + 12] = 0x1E;
  bios[base + 13] = 0xFF;
  bios[base + 14] = 0x2F;
  bios[base + 15] = 0xE1;

  // Dummy IRQ Handler at 0x3FF0 (BX LR)
  // Used as default if game hasn't set one
  if (bios.size() > 0x3FF4) {
    bios[0x3FF0] = 0x1E;
    bios[0x3FF1] = 0xFF;
    bios[0x3FF2] = 0x2F;
    bios[0x3FF3] = 0xE1;
  }

  // NOTE: IRQ trampoline must NOT be overwritten!
  // BIOS direct function calls are supported via ARM7TDMI::ExecuteBIOSFunction
  // for common entry points (used by DirectBoot titles), while IRQ dispatch
  // uses the instruction-level vector + trampoline above.
}

void GBAMemory::Reset() {
  // Initialize WRAM to match real GBA hardware state after BIOS boot
  // Real hardware: BIOS does NOT clear all WRAM, leaving undefined values
  // Testing shows simple incremental pattern matches observed behavior

  // EWRAM: Initialize to 0 (safer for audio buffers that may be read before
  // written)
  std::fill(wram_board.begin(), wram_board.end(), 0);

  // IWRAM: Initialize to 0 BUT preserve BIOS-managed regions.
  // For HLE stability, keep the IRQ stack region deterministic (0-filled).
  std::fill(wram_chip.begin(), wram_chip.end(), 0);

  // BIOS HLE: Initialize IRQ stack region (0x03007FA0-0x03007FDF = 64 bytes)
  // Real BIOS reserves this for IRQ mode stack.
  if (wram_chip.size() >= 0x8000) {

    // Initialize User Interrupt Handler to Dummy Handler in BIOS (0x00003FF0)
    // 0x03007FFC points to game's IRQ handler (real games set this)
    wram_chip[0x7FFC] = 0xF0;
    wram_chip[0x7FFD] = 0x3F;
    wram_chip[0x7FFE] = 0x00;
    wram_chip[0x7FFF] = 0x00;
  }

  std::fill(io_regs.begin(), io_regs.end(), 0);

  // Reset timing state used by GetAccessCycles().
  lastGamePakAccessAddr = 0xFFFFFFFFu;
  lastGamePakAccessRegionGroup = 0xFFu;
  std::fill(palette_ram.begin(), palette_ram.end(), 0);
  std::fill(vram.begin(), vram.end(), 0);
  std::fill(oam.begin(), oam.end(), 0);

  // Initialize KEYINPUT to 0x03FF (All Released)
  if (io_regs.size() > 0x131) {
    io_regs[0x130] = 0xFF;
    io_regs[0x131] = 0x03;
  }

  // Initialize display control: Mode 0 with BG0 enabled so something is visible
  // before the game configures it.
  if (io_regs.size() > IORegs::DISPCNT + 1) {
    uint16_t dispcnt = 0x0100; // Mode 0 (bits 0-2 = 0), BG0 enable (bit 8 = 1)
    io_regs[IORegs::DISPCNT] = dispcnt & 0xFF;
    io_regs[IORegs::DISPCNT + 1] = (dispcnt >> 8) & 0xFF;
  }

  // Initialize SOUNDCNT_X (0x84) with Master Enable set
  // The BIOS enables sound on boot, so bit 7 should be set
  if (io_regs.size() > 0x85) {
    io_regs[0x84] = 0x80; // Master Enable = 1
  }

  // Initialize SOUNDBIAS (0x88) to proper default value
  // Real hardware has SOUNDBIAS = 0x200 after boot (PWM mode, bias = 0x200)
  if (io_regs.size() > 0x89) {
    io_regs[0x88] = 0x00;
    io_regs[0x89] = 0x02; // 0x200 = default bias
  }

  // BIOS sets POSTFLG=1 (0x04000300) after completing boot.
  // Some titles check this to distinguish cold vs warm boot behavior.
  if (io_regs.size() > 0x300) {
    io_regs[0x300] = 0x01;
  }

  // Initialize DMA Registers to Safe Defaults
  // All DMA channels should be disabled (Enable bit = 0) on boot
  // DMA3 specifically: initialize control register (0x0DE) to 0x0000
  // This prevents any accidental DMA transfers on boot
  for (int i = 0; i < 4; ++i) {
    uint32_t cntOffset = IORegs::DMA0CNT_H + i * IORegs::DMA_CHANNEL_SIZE;
    if (io_regs.size() > cntOffset + 1) {
      io_regs[cntOffset] = 0x00;
      io_regs[cntOffset + 1] = 0x00;
    }
  }

  // Initialize Interrupt Enable (IE) Register
  // CRITICAL: Leave interrupts DISABLED on boot. Real BIOS leaves IE=0 and
  // IME=0. Games explicitly enable specific interrupts after installing their
  // handler. Enabling VBlank prematurely causes IRQ storm before game handler
  // is ready.
  if (io_regs.size() > IORegs::IE + 1) {
    io_regs[IORegs::IE] = 0x00;
    io_regs[IORegs::IE + 1] = 0x00;
  }

  // Initialize Master Interrupt Enable (IME) Register
  // CRITICAL: Leave IME DISABLED on boot (IME=0). Real BIOS does NOT enable
  // global IRQs. Games enable IME after setting up their interrupt handler.
  if (io_regs.size() > IORegs::IME + 1) {
    io_regs[IORegs::IME] = 0x00;
    io_regs[IORegs::IME + 1] = 0x00;
  }

  // Initialize WAITCNT (0x04000204) to BIOS default.
  // GBATEK: BIOS configures waitstates/prefetch during boot; leaving this as 0
  // can change timing-sensitive game init and save routines. Common post-BIOS
  // value is 0x4317.
  if (io_regs.size() > 0x205) {
    io_regs[0x204] = 0x17;
    io_regs[0x205] = 0x43;
  }

  if (EnvFlagCached("AIO_TRACE_WAITCNT")) {
    const uint16_t waitcnt = (uint16_t)(io_regs[0x204] | (io_regs[0x205] << 8));
    AIO::Emulator::Common::Logger::Instance().LogFmt(
        AIO::Emulator::Common::LogLevel::Info, "WAITCNT",
        "BOOT WAITCNT init = 0x%04x", (unsigned)waitcnt);
  }

  eepromState = EEPROMState::Idle;
  eepromBitCounter = 0;
  eepromBuffer = 0;
  eepromAddress = 0;
  eepromLatch = 0;      // Initialize latch
  eepromWriteDelay = 0; // Reset write delay
  // saveTypeLocked = false; // Do NOT reset this, as it's set by LoadGamePak

  // BIOS HLE: Initialize critical system state that real BIOS sets up
  // Many games poll specific IWRAM addresses waiting for BIOS background tasks
  // to complete Without full BIOS emulation, we must pre-initialize these to
  // unblock boot sequences

  // System-ready flags: Games check various addresses for non-zero to confirm
  // init complete Common addresses: 0x3002b64, 0x3007ff8 (BIOS_IF), 0x3007ffc
  // (IRQ handler) Strategy: Set multiple known init flags to bypass common wait
  // loops
  if (wram_chip.size() >= 0x8000) {
    // 0x3002b64: System init flag (SMA2, Pokemon, others)
    // Keep this minimally non-zero to unblock init loops without injecting a
    // distinctive magic value that game logic might treat as meaningful.
    wram_chip[0x2b64] = 0x01;
    wram_chip[0x2b65] = 0x00;

    // 0x3007FF8: BIOS_IF (interrupt acknowledge from BIOS)
    wram_chip[0x7FF8] = 0x00;
    wram_chip[0x7FF9] = 0x00;
    wram_chip[0x7FFA] = 0x00;
    wram_chip[0x7FFB] = 0x00;

    // 0x3007FFC: User IRQ handler (already set above to 0x3FF0)
    // 0x3007FF4: Temp storage for triggered interrupts (used by BIOS IRQ
    // dispatcher)
    wram_chip[0x7FF4] = 0x00;
    wram_chip[0x7FF5] = 0x00;
  }

  // Debug: Check EEPROM content
  if (eepromData.empty()) {
    eepromData.resize(8192, 0xFF);
  }
}

bool GBAMemory::Is4KbitEEPROM(const std::vector<uint8_t> &data) {
  // Deprecated: We now use DMA transfer length detection for accurate sizing.
  // Keeping this stub if we want to add generic header checks later.
  return false;
}

bool GBAMemory::ScanForEEPROMSize(const std::vector<uint8_t> &data) {
  // Preprocess the ROM code to determine EEPROM size (4Kbit vs 64Kbit)
  // We look for the DMA3CNT_L register address (0x040000DC) being loaded,
  // and then check for the transfer count (9 or 17) being set nearby.

  int score4k = 0;
  int score64k = 0;

  // Search for the literal 0x040000DC (DMA3CNT_L)
  const uint8_t targetBytes[] = {0xDC, 0x00, 0x00, 0x04};
  // Also search for the base 0x04000000
  const uint8_t baseBytes[] = {0x00, 0x00, 0x00, 0x04};

  for (size_t i = 0; i < data.size() - 4; i += 4) {
    bool foundLiteral =
        (data[i] == targetBytes[0] && data[i + 1] == targetBytes[1] &&
         data[i + 2] == targetBytes[2] && data[i + 3] == targetBytes[3]);
    bool foundBase =
        (data[i] == baseBytes[0] && data[i + 1] == baseBytes[1] &&
         data[i + 2] == baseBytes[2] && data[i + 3] == baseBytes[3]);

    if (foundLiteral || foundBase) {
      // Scan a window of code before the literal
      size_t searchStart = (i > 1024) ? i - 1024 : 0;
      size_t searchEnd = i + 128; // Also look slightly after
      if (searchEnd > data.size())
        searchEnd = data.size();

      // THUMB SCAN
      for (size_t pc = searchStart; pc < searchEnd; pc += 2) {
        uint16_t instr = data[pc] | (data[pc + 1] << 8);

        // LDR Rn, [PC, #imm] -> 0100 1xxx iiiiiiii (4800 - 4FFF)
        if ((instr & 0xF800) == 0x4800) {
          int imm = (instr & 0xFF) * 4;
          size_t targetAddr = (pc & ~2) + 4 + imm;

          if (targetAddr == i) {
            // Found an instruction loading the address!
            // Now look nearby for MOV Rn, #9 or MOV Rn, #17

            // Scan small window around this instruction
            size_t contextStart = (pc > 64) ? pc - 64 : 0;
            size_t contextEnd = pc + 64;
            if (contextEnd > data.size())
              contextEnd = data.size();

            for (size_t j = contextStart; j < contextEnd; j += 2) {
              uint16_t ctxInstr = data[j] | (data[j + 1] << 8);

              // MOV Rn, #9 (0x2n09)
              if ((ctxInstr & 0xF8FF) == 0x2009)
                score4k++;
              // MOV Rn, #17 (0x2n11)
              if ((ctxInstr & 0xF8FF) == 0x2011)
                score64k++;

              // Also check for LDR Rn, [PC, #imm] loading 9 or 17
              if ((ctxInstr & 0xF800) == 0x4800) {
                int valImm = (ctxInstr & 0xFF) * 4;
                size_t valTarget = (j & ~2) + 4 + valImm;
                if (valTarget + 4 <= data.size()) {
                  uint32_t val = data[valTarget] | (data[valTarget + 1] << 8) |
                                 (data[valTarget + 2] << 16) |
                                 (data[valTarget + 3] << 24);
                  if (val == 9)
                    score4k++;
                  if (val == 17)
                    score64k++;
                  // Check for 32-bit DMA control + count (0x8xxx0011)
                  if ((val & 0xFFFF) == 9 && (val & 0x80000000))
                    score4k += 2;
                  if ((val & 0xFFFF) == 17 && (val & 0x80000000))
                    score64k += 2;
                }
              }
            }
          }
        }
      }

      // ARM SCAN
      for (size_t pc = searchStart & ~3; pc < searchEnd; pc += 4) {
        uint32_t instr = data[pc] | (data[pc + 1] << 8) | (data[pc + 2] << 16) |
                         (data[pc + 3] << 24);

        // LDR Rd, [PC, #offset] (E59Fxxxx)
        if ((instr & 0xFFFF0000) == 0xE59F0000) {
          int offset = instr & 0xFFF;
          size_t targetAddr = pc + 8 + offset;

          if (targetAddr == i) {
            // Found ARM LDR loading the address
            size_t contextStart = (pc > 128) ? pc - 128 : 0;
            size_t contextEnd = pc + 128;
            if (contextEnd > data.size())
              contextEnd = data.size();

            for (size_t j = contextStart & ~3; j < contextEnd; j += 4) {
              uint32_t ctxInstr = data[j] | (data[j + 1] << 8) |
                                  (data[j + 2] << 16) | (data[j + 3] << 24);

              // MOV Rd, #9 (E3A0x009)
              if ((ctxInstr & 0xFFF000FF) == 0xE3A00009)
                score4k++;
              // MOV Rd, #17 (E3A0x011)
              if ((ctxInstr & 0xFFF000FF) == 0xE3A00011)
                score64k++;

              // LDR Rd, [PC, #offset] (E59Fxxxx) loading 9 or 17
              if ((ctxInstr & 0xFFFF0000) == 0xE59F0000) {
                int valOffset = ctxInstr & 0xFFF;
                size_t valTarget = j + 8 + valOffset;
                if (valTarget + 4 <= data.size()) {
                  uint32_t val = data[valTarget] | (data[valTarget + 1] << 8) |
                                 (data[valTarget + 2] << 16) |
                                 (data[valTarget + 3] << 24);
                  if (val == 9)
                    score4k++;
                  if (val == 17)
                    score64k++;
                  // Check for 32-bit DMA control + count (0x8xxx0011)
                  if ((val & 0xFFFF) == 9 && (val & 0x80000000))
                    score4k += 2;
                  if ((val & 0xFFFF) == 17 && (val & 0x80000000))
                    score64k += 2;
                }
              }
            }
          }
        }
      }
    }
  }

  if (score64k > score4k)
    return false; // 64Kbit
  if (score4k > score64k)
    return true; // 4Kbit

  return false; // Default to 64Kbit if inconclusive
}

void GBAMemory::LoadGamePak(const std::vector<uint8_t> &data) {
  if (data.size() > rom.size()) {
    rom.resize(data.size());
  }
  std::copy(data.begin(), data.end(), rom.begin());

  // GBA BIOS Header Validation - Required for games to boot correctly!
  // The real BIOS validates the header checksum and sets a flag in IWRAM.
  // If validation fails, games detect this as piracy/invalid cartridge.
  if (data.size() >= 0xBE) {
    // Calculate complement checksum (offset 0xA0-0xBC)
    uint8_t chk = 0;
    for (uint32_t i = 0xA0; i <= 0xBC; i++) {
      chk = (chk - data[i]) & 0xFF;
    }
    chk = (chk - 0x19) & 0xFF;

    // Check against stored checksum at 0xBD
    if (chk == data[0xBD]) {
      // Header valid - Set BIOS validation flag in IWRAM
      // Real BIOS writes 01h to 0x03007FFA after successful validation
      std::cout << "[GBAMemory] ROM header checksum valid (0x" << std::hex
                << (int)chk << std::dec << ")" << std::endl;
      if (wram_chip.size() >= 0x7FFB) {
        wram_chip[0x7FFA] = 0x01; // Header validated
      }
    } else {
      std::cerr << "[GBAMemory] WARNING: ROM header checksum mismatch!"
                << std::endl;
      std::cerr << "[GBAMemory] Expected: 0x" << std::hex << (int)data[0xBD]
                << ", Calculated: 0x" << (int)chk << std::dec << std::endl;
      // Set flag to 0 (validation failed)
      if (wram_chip.size() >= 0x7FFB) {
        wram_chip[0x7FFA] = 0x00; // Header validation failed
      }
    }
  }

  SaveType saveType = SaveType::Auto;
  bool locked = false;

  // Note: Detailed save type detection is now handled by ROMMetadataAnalyzer
  // in GBA::LoadROM() which runs AFTER LoadGamePak and calls SetSaveType().
  // Here we do minimal detection to ensure the save system is initialized
  // properly.

  // Store game code for reference
  if (data.size() >= 0xB0) {
    std::string detectedCode(reinterpret_cast<const char *>(&data[0xAC]), 4);
    this->gameCode = detectedCode;
  }

  // Fallback: DMA Scan for EEPROM size detection (if string markers not found)
  if (saveType == SaveType::Auto) {
    bool is4k = ScanForEEPROMSize(data);
    saveType = is4k ? SaveType::EEPROM_4K : SaveType::EEPROM_64K;
  }

  // Apply Configuration
  isFlash = false;
  hasSRAM = false;
  eepromIs64Kbit = true;
  this->saveTypeLocked = locked;

  switch (saveType) {
  case SaveType::EEPROM_4K:
    eepromIs64Kbit = false;
    eepromData.resize(EEPROM::SIZE_4K, EEPROM::ERASED_VALUE);
    break;
  case SaveType::EEPROM_64K:
    eepromIs64Kbit = true;
    eepromData.resize(EEPROM::SIZE_64K, EEPROM::ERASED_VALUE);
    break;
  case SaveType::SRAM:
    hasSRAM = true;
    sram.resize(SaveTypes::SRAM_SIZE, EEPROM::ERASED_VALUE);
    break;
  case SaveType::Flash512:
    isFlash = true;
    hasSRAM = true;
    sram.resize(SaveTypes::FLASH_512K_SIZE, EEPROM::ERASED_VALUE);
    break;
  case SaveType::Flash1M:
    isFlash = true;
    hasSRAM = true;
    sram.resize(SaveTypes::FLASH_1M_SIZE, EEPROM::ERASED_VALUE);
    break;
  default:
    // Default to 64K EEPROM (GBATEK)
    eepromIs64Kbit = true;
    eepromData.resize(EEPROM::SIZE_64K, EEPROM::ERASED_VALUE);
    break;
  }
}

void GBAMemory::LoadSave(const std::vector<uint8_t> &data) {
  const bool usesEEPROM = (!hasSRAM && !isFlash);

  // Ensure the backing store is initialized for the configured type.
  if (usesEEPROM) {
    const size_t targetSize =
        eepromIs64Kbit ? EEPROM::SIZE_64K : EEPROM::SIZE_4K;
    if (eepromData.size() != targetSize) {
      eepromData.assign(targetSize, EEPROM::ERASED_VALUE);
    }
  } else {
    if (sram.empty()) {
      // Conservative default. Most SRAM titles use 32KB; flash titles set a
      // proper size via SetSaveType().
      sram.assign(SaveTypes::SRAM_SIZE, EEPROM::ERASED_VALUE);
    }
  }

  std::vector<uint8_t> &backing = usesEEPROM ? eepromData : sram;
  const size_t targetSize = backing.size();

  if (!data.empty()) {
    backing.assign(targetSize, EEPROM::ERASED_VALUE);
    const size_t copySize = std::min(data.size(), targetSize);
    std::copy(data.begin(), data.begin() + copySize, backing.begin());
  } else {
    backing.assign(targetSize, EEPROM::ERASED_VALUE);
  }

  // If the save is fully erased, it's safe to write a clean image once so we
  // have a file on disk.
  const bool allFF = std::all_of(backing.begin(), backing.end(),
                                 [](uint8_t b) { return b == 0xFF; });
  if (allFF) {
    FlushSave();
  }
}

std::vector<uint8_t> GBAMemory::GetSaveData() const {
  const bool usesEEPROM = (!hasSRAM && !isFlash);
  return usesEEPROM ? eepromData : sram;
}

void GBAMemory::SetSavePath(const std::string &path) { savePath = path; }

void GBAMemory::SetSaveType(SaveType type) {
  configuredSaveType = type;
  if (verboseLogs) {
    std::cout << "[GBAMemory] Configuring save type" << std::endl;
  }

  const std::vector<uint8_t> existingEeprom = eepromData;
  const std::vector<uint8_t> existingSram = sram;

  switch (type) {
  case SaveType::SRAM:
    if (verboseLogs)
      std::cout << "SRAM" << std::endl;
    hasSRAM = true;
    isFlash = false;
    flashBank = 0;
    flashState = 0;
    sram.assign(SaveTypes::SRAM_SIZE, EEPROM::ERASED_VALUE);
    if (!existingSram.empty()) {
      const size_t copySize = std::min(existingSram.size(), sram.size());
      std::copy(existingSram.begin(), existingSram.begin() + copySize,
                sram.begin());
    }
    break;
  case SaveType::Flash512:
    if (verboseLogs)
      std::cout << "Flash 512K" << std::endl;
    hasSRAM = true;
    isFlash = true;
    flashBank = 0;
    flashState = 0;
    sram.assign(SaveTypes::FLASH_512K_SIZE, EEPROM::ERASED_VALUE);
    if (!existingSram.empty()) {
      const size_t copySize = std::min(existingSram.size(), sram.size());
      std::copy(existingSram.begin(), existingSram.begin() + copySize,
                sram.begin());
    }
    break;
  case SaveType::Flash1M:
    if (verboseLogs)
      std::cout << "Flash 1M" << std::endl;
    hasSRAM = true;
    isFlash = true;
    flashBank = 0;
    flashState = 0;
    sram.assign(SaveTypes::FLASH_1M_SIZE, EEPROM::ERASED_VALUE);
    if (!existingSram.empty()) {
      const size_t copySize = std::min(existingSram.size(), sram.size());
      std::copy(existingSram.begin(), existingSram.begin() + copySize,
                sram.begin());
    }
    break;
  case SaveType::EEPROM_4K:
    if (verboseLogs)
      std::cout << "EEPROM 4K" << std::endl;
    hasSRAM = false;
    isFlash = false;
    eepromIs64Kbit = false;
    if (eepromData.size() != 512) {
      eepromData.resize(512, 0xFF); // 4Kbit EEPROM
      if (!existingEeprom.empty()) {
        size_t copySize = std::min(existingEeprom.size(), eepromData.size());
        std::copy(existingEeprom.begin(), existingEeprom.begin() + copySize,
                  eepromData.begin());
      }
    }
    break;
  case SaveType::EEPROM_64K:
    if (verboseLogs)
      std::cout << "EEPROM 64K" << std::endl;
    hasSRAM = false;
    isFlash = false;
    eepromIs64Kbit = true;
    if (eepromData.size() != 8192) {
      if (verboseLogs)
        std::cout << "[SetSaveType] Resizing from " << eepromData.size()
                  << " to 8192" << std::endl;
      eepromData.resize(8192, 0xFF); // 64Kbit EEPROM
      if (!existingEeprom.empty()) {
        size_t copySize = std::min(existingEeprom.size(), eepromData.size());
        if (verboseLogs)
          std::cout << "[SetSaveType] Copying " << copySize
                    << " bytes from existingData" << std::endl;
        std::copy(existingEeprom.begin(), existingEeprom.begin() + copySize,
                  eepromData.begin());
      }
    } else {
      if (verboseLogs)
        std::cout
            << "[SetSaveType] Size already correct (8192), no resize needed"
            << std::endl;
    }
    break;
  case SaveType::Auto:
    if (verboseLogs)
      std::cout << "Auto-detect (no change)" << std::endl;
    break;
  default:
    if (verboseLogs)
      std::cout << "Unknown" << std::endl;
  }

  if (verboseLogs) {
    std::cout << "[SetSaveType] Final eepromData.size() = " << eepromData.size()
              << std::endl;
    if (!eepromData.empty()) {
      std::cout << "[SetSaveType] Final first 16 bytes: " << std::hex;
      for (size_t i = 0; i < std::min(eepromData.size(), size_t(16)); i++) {
        std::cout << std::setw(2) << std::setfill('0') << (int)eepromData[i]
                  << " ";
      }
      std::cout << std::dec << std::endl;
    }
  }

  saveTypeLocked = true; // Prevent further dynamic detection
}

void GBAMemory::FlushSave() {
  if (savePath.empty()) {
    return;
  }

  const bool usesEEPROM = (!hasSRAM && !isFlash);
  const std::vector<uint8_t> &backing = usesEEPROM ? eepromData : sram;
  if (backing.empty()) {
    return;
  }

  std::ofstream file(savePath, std::ios::binary);
  if (file.is_open()) {
    file.write(reinterpret_cast<const char *>(backing.data()), backing.size());
    file.close();
  }
}

void GBAMemory::EvaluateKeypadIRQ() {
  static int keypadIrqDebug = -1;
  if (keypadIrqDebug < 0) {
    keypadIrqDebug = EnvFlagCached("AIO_KEYPAD_IRQ_DEBUG") ? 1 : 0;
  }

  if (io_regs.size() <= IORegs::KEYCNT + 1 ||
      io_regs.size() <= IORegs::KEYINPUT + 1 ||
      io_regs.size() <= IORegs::IF + 1) {
    return;
  }

  // GBATEK: KEYCNT (0x04000132)
  // - Bits 0-9: key mask
  // - Bit 14: IRQ enable
  // - Bit 15: condition (0=OR, 1=AND)
  const uint16_t keycnt =
      io_regs[IORegs::KEYCNT] | (io_regs[IORegs::KEYCNT + 1] << 8);
  const bool irqEnable = (keycnt & 0x4000) != 0;
  if (!irqEnable) {
    return;
  }

  const uint16_t mask = keycnt & 0x03FF;
  if (mask == 0) {
    return;
  }

  const bool andMode = (keycnt & 0x8000) != 0;

  // KEYINPUT is active-low: 0=pressed
  const uint16_t keyinput =
      io_regs[IORegs::KEYINPUT] | (io_regs[IORegs::KEYINPUT + 1] << 8);
  const uint16_t pressed = static_cast<uint16_t>((~keyinput) & 0x03FF);

  const bool conditionMet =
      andMode ? ((pressed & mask) == mask) : ((pressed & mask) != 0);
  if (!conditionMet) {
    return;
  }

  if (keypadIrqDebug) {
    static int logCount = 0;
    if (logCount < 200) {
      AIO::Emulator::Common::Logger::Instance().LogFmt(
          AIO::Emulator::Common::LogLevel::Debug, "GBA/KEYPAD",
          "KEYCNT=0x%04x KEYINPUT=0x%04x pressed=0x%04x mask=0x%04x mode=%s",
          keycnt, keyinput, pressed, mask, andMode ? "AND" : "OR");
      logCount++;
    }
  }

  // Hardware requests keypad interrupt by setting IF bit 12.
  uint16_t if_reg = io_regs[IORegs::IF] | (io_regs[IORegs::IF + 1] << 8);
  if_reg |= InterruptFlags::KEYPAD;
  io_regs[IORegs::IF] = if_reg & 0xFF;
  io_regs[IORegs::IF + 1] = (if_reg >> 8) & 0xFF;
}

void GBAMemory::SetKeyInput(uint16_t value) {
  if (io_regs.size() > IORegs::KEYINPUT + 1) {
    const uint16_t old =
        io_regs[IORegs::KEYINPUT] | (io_regs[IORegs::KEYINPUT + 1] << 8);
    io_regs[IORegs::KEYINPUT] = value & 0xFF;
    io_regs[IORegs::KEYINPUT + 1] = (value >> 8) & 0xFF;

    // If the game is using keypad interrupts to wake from HALT or to detect
    // prompt input, request the KEYPAD interrupt when KEYCNT conditions are
    // met.
    EvaluateKeypadIRQ();

    // Targeted input trace for SMA2: helps confirm that A/B/Start etc are
    // reaching the core when stuck at "saved data is corrupt".
    if ((gameCode == "AMQE" || gameCode == "AMQP" || gameCode == "AMQJ" ||
         gameCode == "AA2E") &&
        old != value) {
      static int keyLogCount = 0;
      if (keyLogCount < 80) {
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "GBA/KEYINPUT",
            "game=%s old=0x%04x new=0x%04x", gameCode.c_str(), old, value);
        keyLogCount++;
      }
    }
  }
}

int GBAMemory::GetAccessCycles(uint32_t address, int accessSize) const {
  // GBA Memory Access Timing (GBATEK)
  // Returns cycles for the given access size (1=8bit, 2=16bit, 4=32bit)
  const uint8_t region = (uint8_t)(address >> 24);

  // WAITCNT affects SRAM and Game Pak waitstates (0x08000000-0x0DFFFFFF).
  // We approximate sequential timing by tracking the last Game Pak access.
  const uint16_t waitcnt = (io_regs.size() > (IORegs::WAITCNT + 1))
                               ? (uint16_t)(io_regs[IORegs::WAITCNT] |
                                            (io_regs[IORegs::WAITCNT + 1] << 8))
                               : 0u;

  const auto decodeNonSeqWait = [](uint32_t code) -> int {
    // GBATEK WAITCNT N-cycle encodings: 0=4, 1=3, 2=2, 3=8
    switch (code & 3u) {
    case 0:
      return 4;
    case 1:
      return 3;
    case 2:
      return 2;
    default:
      return 8;
    }
  };
  const auto decodeSeqWait = [](uint32_t bit) -> int {
    // GBATEK WAITCNT S-cycle encodings: 0=2, 1=1
    return (bit & 1u) ? 1 : 2;
  };

  const auto gamePakRegionGroup = [](uint8_t r) -> uint8_t {
    // 0x08/09 -> 0x08 (WS0)
    // 0x0A/0B -> 0x0A (WS1)
    // 0x0C/0D -> 0x0C (WS2)
    // 0x0E    -> 0x0E (SRAM)
    if (r == 0x0E)
      return 0x0E;
    if (r >= 0x08 && r <= 0x0D)
      return (uint8_t)(r & 0xFE);
    return r;
  };

  const uint8_t group = gamePakRegionGroup(region);
  const bool isGamePak = (group == 0x08 || group == 0x0A || group == 0x0C);
  const bool isSram = (group == 0x0E);

  bool sequential = false;
  if (isGamePak || isSram) {
    sequential = (lastGamePakAccessRegionGroup == group) &&
                 (lastGamePakAccessAddr + (uint32_t)accessSize == address);
    lastGamePakAccessAddr = address;
    lastGamePakAccessRegionGroup = group;
  }

  switch (region) {
  case 0x00: // BIOS (16-bit bus)
    return (accessSize == 4) ? 2 : 1;

  case 0x02: // EWRAM (16-bit bus, 2 wait states)
    return (accessSize == 4) ? 6 : 3;

  case 0x03: // IWRAM (32-bit bus, 0 wait states)
    return 1;

  case 0x04: // I/O (16-bit bus, 0 wait states)
    return 1;

  case 0x05: // Palette RAM (16-bit bus, 0 wait states)
    return (accessSize == 4) ? 2 : 1;

  case 0x06: // VRAM (16-bit bus, 0 wait states)
    return (accessSize == 4) ? 2 : 1;

  case 0x07: // OAM (32-bit bus, 0 wait states)
    return 1;

  case 0x08: // Game Pak ROM, Wait State 0
  case 0x09:
  case 0x0A: // Game Pak ROM, Wait State 1
  case 0x0B:
  case 0x0C: // Game Pak ROM, Wait State 2
  case 0x0D: {
    int nWait = 4;
    int sWait = 2;
    if (group == 0x08) {
      nWait = decodeNonSeqWait((waitcnt >> 2) & 3u);
      sWait = decodeSeqWait((waitcnt >> 4) & 1u);
    } else if (group == 0x0A) {
      nWait = decodeNonSeqWait((waitcnt >> 5) & 3u);
      sWait = decodeSeqWait((waitcnt >> 7) & 1u);
    } else {
      nWait = decodeNonSeqWait((waitcnt >> 8) & 3u);
      sWait = decodeSeqWait((waitcnt >> 10) & 1u);
    }

    // Base 1 cycle + configured wait.
    // For 32-bit on a 16-bit bus, this becomes two 16-bit accesses: first
    // nonseq/seq, second seq.
    const int first = 1 + (sequential ? sWait : nWait);
    if (accessSize == 4) {
      const int second = 1 + sWait;
      return first + second;
    }
    return first;
  }

  case 0x0E: // SRAM/Flash (8-bit bus)
  {
    const int nWait = decodeNonSeqWait(waitcnt & 3u);
    const int perByte = 1 + nWait;
    return perByte * std::max(1, accessSize);
  }

  default:
    return 1;
  }
}

uint8_t GBAMemory::Read8(uint32_t address) {
  uint8_t region = (address >> 24);
  switch (region) {
  case 0x00: // BIOS (GBATEK: 0x00000000-0x00003FFF)
    // BIOS read protection / open-bus behavior:
    // When the CPU is executing outside of BIOS, reads from the BIOS region
    // return the last prefetched instruction data (open bus) rather than BIOS
    // ROM. Many titles rely on this for entropy or basic checks.
    if (cpu && cpu->GetRegister(15) >= 0x00004000) {
      const uint32_t pc = cpu->GetRegister(15);
      const uint32_t pcAligned = pc & ~3u;
      const uint32_t openBusWord =
          cpu->IsThumbModeFlag()
              ? (uint32_t)ReadInstruction16(pcAligned) |
                    ((uint32_t)ReadInstruction16(pcAligned + 2) << 16)
              : ReadInstruction32(pcAligned);
      return (openBusWord >> ((address & 3u) * 8u)) & 0xFFu;
    }

    if (address < bios.size()) {
      return bios[address];
    }
    break;
  case 0x02: // WRAM (Board) (GBATEK: 0x02000000-0x0203FFFF)
  {
    const uint32_t off = address & MemoryMap::WRAM_BOARD_MASK;
    const uint8_t v = wram_board[off];
    // SMA2 investigation: trace reads of the 8-byte header staging area.
    // Enable with: AIO_TRACE_SMA2_HEADER_READS=1
    if (EnvFlagCached("AIO_TRACE_SMA2_HEADER_READS") && cpu) {
      if (off >= 0x03C0u && off < 0x03C8u) {
        static int hdrReads = 0;
        if (hdrReads < 400) {
          const uint32_t pc = (uint32_t)cpu->GetRegister(15);
          AIO::Emulator::Common::Logger::Instance().LogFmt(
              AIO::Emulator::Common::LogLevel::Info, "SMA2",
              "HDR_READ addr=0x%08x off=0x%04x val=0x%02x PC=0x%08x",
              (unsigned)address, (unsigned)off, (unsigned)v, (unsigned)pc);
          hdrReads++;
        }
      }
    }
    return v;
  }
  case 0x03: // WRAM (Chip) (GBATEK: 0x03000000-0x03007FFF)
    if (IsIwramMappedAddress(address)) {
      return wram_chip[address & MemoryMap::WRAM_CHIP_MASK];
    }
    return 0;
  case 0x04: // IO Registers (GBATEK: 0x04000000-0x040003FF)
  {
    uint32_t offset = address & MemoryMap::IO_REG_MASK;
    uint8_t val = 0;
    if (offset < io_regs.size())
      val = io_regs[offset];

    // SOUNDCNT_X (0x84) - Return proper status
    if (offset == IORegs::SOUNDCNT_X) {
      val =
          io_regs[IORegs::SOUNDCNT_X] & 0x80; // Only preserve master enable bit
    }

    // DMA3 read trace (useful for save validation loops that poll DMA regs)
    if (EnvFlagCached("AIO_TRACE_DMA3_READS") && offset >= 0xD4 &&
        offset <= 0xDF) {
      static int dma3ReadLogs = 0;
      if (dma3ReadLogs < 400) {
        const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "DMA3",
            "Read8 off=0x%03x val=0x%02x PC=0x%08x", (unsigned)offset,
            (unsigned)val, (unsigned)pc);
        dma3ReadLogs++;
      }
    }

    // Generic IO polling trace (8-bit). Useful for diagnosing games stuck in
    // init loops while DISPCNT remains forced blank.
    // Enable with: AIO_TRACE_IO_POLL=1
    if (EnvFlagCached("AIO_TRACE_IO_POLL") && cpu) {
      const uint32_t pc = (uint32_t)cpu->GetRegister(15);
      if (pc >= 0x03000000u && pc < 0x03008000u) {
        static uint32_t lastOffset8 = 0xFFFFFFFFu;
        static uint8_t lastVal8 = 0xFFu;
        static uint32_t repeats8 = 0;
        static int logs8 = 0;
        if (logs8 < 800) {
          if (offset == lastOffset8 && val == lastVal8) {
            repeats8++;
            if (repeats8 == 1u || (repeats8 % 4096u) == 0u) {
              AIO::Emulator::Common::Logger::Instance().LogFmt(
                  AIO::Emulator::Common::LogLevel::Info, "IOPOLL",
                  "IO poll8 repeat off=0x%03x val=0x%02x PC=0x%08x repeats=%u",
                  (unsigned)offset, (unsigned)val, (unsigned)pc,
                  (unsigned)repeats8);
              logs8++;
            }
          } else {
            repeats8 = 0;
            lastOffset8 = offset;
            lastVal8 = val;
            AIO::Emulator::Common::Logger::Instance().LogFmt(
                AIO::Emulator::Common::LogLevel::Info, "IOPOLL",
                "IO poll8 off=0x%03x val=0x%02x PC=0x%08x", (unsigned)offset,
                (unsigned)val, (unsigned)pc);
            logs8++;
          }
        }
      }
    }

    return val;
  }
  case 0x05: // Palette RAM (GBATEK: 0x05000000-0x050003FF)
  {
    uint32_t offset = address & MemoryMap::PALETTE_MASK;
    if (offset < palette_ram.size())
      return palette_ram[offset];
    break;
  }
  case 0x06: // VRAM (GBATEK: 0x06000000-0x06017FFF)
  {
    // VRAM address space is 0x06000000-0x0601FFFF (128KB), with 96KB of real
    // memory. The upper 32KB (0x06018000-0x0601FFFF) mirrors the OBJ region
    // (0x06010000-0x06017FFF).
    uint32_t offset = address & 0x1FFFFu; // mirror within 128KB window
    if (offset >= MemoryMap::VRAM_ACTUAL_SIZE) {
      offset -= 0x8000u;
    }
    if (offset < vram.size())
      return vram[offset];
    break;
  }
  case 0x07: // OAM (GBATEK: 0x07000000-0x070003FF)
  {
    uint32_t offset = address & MemoryMap::OAM_MASK;
    if (offset < oam.size())
      return oam[offset];
    break;
  }
  case 0x08: // Game Pak ROM (GBATEK: 0x08000000-0x0DFFFFFF)
  case 0x09:
  case 0x0A:
  case 0x0B:
  case 0x0C: {
    // Universal ROM Mirroring (max 32MB space, mirrored every rom.size())
    uint32_t offset = address & MemoryMap::ROM_MIRROR_MASK;
    if (!rom.empty()) {
      return rom[offset % rom.size()];
    }
    break;
  }
  case 0x0D: // Game Pak ROM (WS2) or EEPROM depending on cart save type
  {
    // GBATEK: 0x08000000-0x0DFFFFFF is Game Pak ROM space.
    // EEPROM-accessible cartridges multiplex EEPROM protocol at 0x0Dxxxxxx;
    // for non-EEPROM carts, this must behave like ROM mirroring.
    const bool usesEEPROM = (!hasSRAM && !isFlash);
    if (usesEEPROM) {
      // Route reads through the EEPROM state machine so the serial line
      // returns READY/BUSY bits instead of zero.
      return ReadEEPROM() & 0xFF;
    }

    // Non-EEPROM cart: treat as ROM mirror (same behavior as 0x08-0x0C).
    const uint32_t offset = address & MemoryMap::ROM_MIRROR_MASK;
    if (!rom.empty()) {
      return rom[offset % rom.size()];
    }
    break;
  }
  case 0x0E: // SRAM/Flash (GBATEK: 0x0E000000-0x0E00FFFF)
  {
    if (!hasSRAM) {
      // EEPROM-only cartridges have no SRAM/Flash chip.
      // In practice the bus is pulled-up and reads appear erased (0xFF).
      // Returning address-dependent garbage can confuse save probes and
      // lead to false "save data corrupt" paths.
      return EEPROM::ERASED_VALUE;
    }

    if (isFlash && flashState == 3) { // ID Mode
      uint32_t offset = address & 0xFFFF;
      if (offset == 0)
        return SaveTypes::FLASH_MAKER_ID; // Maker: Macronix
      if (offset == 1)
        return SaveTypes::FLASH_DEVICE_512K; // Device: 512K (TODO: Handle
                                             // 1Mbit)
      return 0;
    }
    // Normal Read
    uint32_t offset = address & 0xFFFF;
    // Handle 1Mbit banking if needed (offset > 64K)
    // But standard addressing is 64K window.
    // If sram.size() > 64K, use flashBank.
    if (sram.size() > SaveTypes::FLASH_512K_SIZE) {
      offset += (flashBank * SaveTypes::FLASH_512K_SIZE);
    }
    if (offset < sram.size())
      return sram[offset];
    return EEPROM::ERASED_VALUE; // Open bus / erased
  }
  }
  return 0;
}

uint16_t GBAMemory::Read16(uint32_t address) {
  // EEPROM Handling: only for EEPROM-save cartridges.
  uint8_t region = (address >> 24);
  if (region == 0x0D && (!hasSRAM && !isFlash)) {
    return ReadEEPROM();
  }

  // Diagnostics: detect unaligned IO halfword reads.
  // Enable with: AIO_TRACE_IO_UNALIGNED=1 (or legacy
  // AIO_TRACE_IO_ODD_HALFWORD=1)
  const bool traceIoUnaligned = EnvFlagCached("AIO_TRACE_IO_UNALIGNED") ||
                                EnvFlagCached("AIO_TRACE_IO_ODD_HALFWORD");
  if (traceIoUnaligned && region == 0x04 && (address & 1u)) {
    static int unalignedIoRead16Logs = 0;
    if (unalignedIoRead16Logs < 400) {
      const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
      AIO::Emulator::Common::Logger::Instance().LogFmt(
          AIO::Emulator::Common::LogLevel::Info, "IO",
          "UNALIGNED Read16 addr=0x%08x PC=0x%08x", (unsigned)address,
          (unsigned)pc);
      unalignedIoRead16Logs++;
    }
  }

  // GBA IO registers are fundamentally 16-bit; halfword accesses are aligned.
  // Some titles issue unaligned halfword loads/stores into IO space; on
  // hardware these behave like aligned accesses.
  if (region == 0x04 && (address & 1u)) {
    address &= ~1u;
  }

  // SMA2 investigation: trace reads of the save/validator object pointer.
  // Enable with: AIO_TRACE_SMA2_SAVEOBJ_READ=1
  if (EnvFlagCached("AIO_TRACE_SMA2_SAVEOBJ_READ")) {
    if ((address & 0xFFFFFFFCu) == 0x03007BC8u) {
      static int readLogs16 = 0;
      if (readLogs16 < 400) {
        const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "SMA2",
            "SAVEOBJ Read16 addr=0x%08x val=0x%04x PC=0x%08x",
            (unsigned)address,
            (unsigned)(Read8(address) | (Read8(address + 1) << 8)),
            (unsigned)pc);
        readLogs16++;
      }
    }
  }

  uint16_t val = Read8(address) | (Read8(address + 1) << 8);

  // Trace tight IO polling loops (diagnostic for "stuck in forced blank").
  // Enable with: AIO_TRACE_IO_POLL=1
  // This is intentionally very low-volume (change-triggered + periodic).
  if (EnvFlagCached("AIO_TRACE_IO_POLL") && cpu && region == 0x04 &&
      (address & 0xFF000000u) == 0x04000000u) {
    const uint32_t pc = (uint32_t)cpu->GetRegister(15);
    // Many titles run hot loops from IWRAM; focus there to keep signal high.
    if (pc >= 0x03000000u && pc < 0x03008000u) {
      const uint32_t offset = address & 0x3FFu;
      static uint32_t lastOffset = 0xFFFFFFFFu;
      static uint16_t lastVal = 0xFFFFu;
      static uint32_t repeats = 0;

      if (offset == lastOffset && val == lastVal) {
        repeats++;
        if (repeats == 1u || (repeats % 4096u) == 0u) {
          AIO::Emulator::Common::Logger::Instance().LogFmt(
              AIO::Emulator::Common::LogLevel::Info, "IOPOLL",
              "IO poll repeat off=0x%03x val=0x%04x PC=0x%08x repeats=%u",
              (unsigned)offset, (unsigned)val, (unsigned)pc, (unsigned)repeats);
        }
      } else {
        repeats = 0;
        lastOffset = offset;
        lastVal = val;
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "IOPOLL",
            "IO poll off=0x%03x val=0x%04x PC=0x%08x", (unsigned)offset,
            (unsigned)val, (unsigned)pc);
      }
    }
  }

  // SMA2 investigation: trace reads from the EEPROM DMA destination buffers in
  // IWRAM. Enable with: AIO_TRACE_SMA2_EEPDMA_BUF_READS=1 This helps confirm
  // whether the game is actually consuming the expected 0xFFFE/0xFFFF bitstream
  // halfwords after DMA completes.
  if (EnvFlagCached("AIO_TRACE_SMA2_EEPDMA_BUF_READS") && cpu) {
    if (region == 0x03 && address >= 0x03007C80u && address < 0x03007D80u) {
      static int eepBufR16 = 0;
      if (eepBufR16 < 1200) {
        const uint32_t pc = (uint32_t)cpu->GetRegister(15);
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "SMA2",
            "EEPDMA R16 addr=0x%08x val=0x%04x PC=0x%08x", (unsigned)address,
            (unsigned)val, (unsigned)pc);
        eepBufR16++;
      }
    }
  }

  // Timer Counters (Read from internal state)
  if ((address & 0xFF000000) == IORegs::BASE) {
    uint32_t offset = address & MemoryMap::IO_REG_MASK;

    if (offset >= IORegs::TM0CNT_L && offset <= IORegs::TM3CNT_H) {
      int timerIdx = (offset - IORegs::TM0CNT_L) / IORegs::TIMER_CHANNEL_SIZE;
      if ((offset % IORegs::TIMER_CHANNEL_SIZE) == 0) {
        return timerCounters[timerIdx];
      }
    }
  }

  return val;
}

uint16_t GBAMemory::ReadInstruction16(uint32_t address) {
  // Direct ROM access for instruction fetch - bypass EEPROM and other checks
  if ((address >> 24) >= 0x08 && (address >> 24) <= 0x0C) {
    uint32_t offset = address & 0x01FFFFFF;
    if (!rom.empty()) {
      // Handle wrapping if needed, though usually not for code
      uint8_t b0 = rom[offset % rom.size()];
      uint8_t b1 = rom[(offset + 1) % rom.size()];
      return b0 | (b1 << 8);
    }
  }
  // Fallback
  return Read8(address) | (Read8(address + 1) << 8);
}

uint32_t GBAMemory::Read32(uint32_t address) {
  // EEPROM Handling - 32-bit read performs two 16-bit reads for EEPROM-save
  // cartridges only.
  uint8_t region = (address >> 24);
  if (region == 0x0D && (!hasSRAM && !isFlash)) {
    uint16_t low = ReadEEPROM();
    uint16_t high = ReadEEPROM();
    return low | (high << 16);
  }

  // Diagnostics: detect unaligned IO word reads.
  // Enable with: AIO_TRACE_IO_UNALIGNED=1 (or legacy
  // AIO_TRACE_IO_ODD_HALFWORD=1)
  const bool traceIoUnaligned = EnvFlagCached("AIO_TRACE_IO_UNALIGNED") ||
                                EnvFlagCached("AIO_TRACE_IO_ODD_HALFWORD");
  if (traceIoUnaligned && region == 0x04 && (address & 3u)) {
    static int unalignedIoRead32Logs = 0;
    if (unalignedIoRead32Logs < 400) {
      const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
      AIO::Emulator::Common::Logger::Instance().LogFmt(
          AIO::Emulator::Common::LogLevel::Info, "IO",
          "UNALIGNED Read32 addr=0x%08x PC=0x%08x", (unsigned)address,
          (unsigned)pc);
      unalignedIoRead32Logs++;
    }
  }

  // IO space word accesses are aligned on hardware.
  if (region == 0x04 && (address & 3u)) {
    address &= ~3u;
  }

  // SMA2 investigation: trace reads of the save/validator object pointer.
  // Enable with: AIO_TRACE_SMA2_SAVEOBJ_READ=1
  if (EnvFlagCached("AIO_TRACE_SMA2_SAVEOBJ_READ")) {
    if (address == 0x03007BC8u) {
      static int readLogs32 = 0;
      if (readLogs32 < 400) {
        const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
        const uint32_t v = Read8(address) | (Read8(address + 1) << 8) |
                           (Read8(address + 2) << 16) |
                           (Read8(address + 3) << 24);
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "SMA2",
            "SAVEOBJ Read32 addr=0x%08x val=0x%08x PC=0x%08x",
            (unsigned)address, (unsigned)v, (unsigned)pc);
        readLogs32++;
      }
    }
  }

  uint32_t val = Read8(address) | (Read8(address + 1) << 8) |
                 (Read8(address + 2) << 16) | (Read8(address + 3) << 24);

  // Trace tight IO polling loops via 32-bit reads.
  // Enable with: AIO_TRACE_IO_POLL=1
  if (EnvFlagCached("AIO_TRACE_IO_POLL") && cpu && region == 0x04 &&
      (address & 0xFF000000u) == 0x04000000u) {
    const uint32_t pc = (uint32_t)cpu->GetRegister(15);
    if (pc >= 0x03000000u && pc < 0x03008000u) {
      const uint32_t offset = address & 0x3FFu;
      static uint32_t lastOffset32 = 0xFFFFFFFFu;
      static uint32_t lastVal32 = 0xFFFFFFFFu;
      static uint32_t repeats32 = 0;

      if (offset == lastOffset32 && val == lastVal32) {
        repeats32++;
        if (repeats32 == 1u || (repeats32 % 4096u) == 0u) {
          AIO::Emulator::Common::Logger::Instance().LogFmt(
              AIO::Emulator::Common::LogLevel::Info, "IOPOLL",
              "IO poll32 repeat off=0x%03x val=0x%08x PC=0x%08x repeats=%u",
              (unsigned)offset, (unsigned)val, (unsigned)pc,
              (unsigned)repeats32);
        }
      } else {
        repeats32 = 0;
        lastOffset32 = offset;
        lastVal32 = val;
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "IOPOLL",
            "IO poll32 off=0x%03x val=0x%08x PC=0x%08x", (unsigned)offset,
            (unsigned)val, (unsigned)pc);
      }
    }
  }

  // SMA2 investigation: trace 32-bit reads from the EEPROM DMA destination
  // buffers in IWRAM. Enable with: AIO_TRACE_SMA2_EEPDMA_BUF_READS=1
  if (EnvFlagCached("AIO_TRACE_SMA2_EEPDMA_BUF_READS") && cpu) {
    if (region == 0x03 && address >= 0x03007C80u && address < 0x03007D80u) {
      static int eepBufR32 = 0;
      if (eepBufR32 < 800) {
        const uint32_t pc = (uint32_t)cpu->GetRegister(15);
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "SMA2",
            "EEPDMA R32 addr=0x%08x val=0x%08x PC=0x%08x", (unsigned)address,
            (unsigned)val, (unsigned)pc);
        eepBufR32++;
      }
    }
  }

  // Debug: trace Read32 from jump table area
  if ((address >> 24) == 0x03 && (address & 0x7FFF) == 0x3378) {
    static int read32_3378_count = 0;
    read32_3378_count++;
    if (val != 0xfffffca4 && read32_3378_count > 100 &&
        read32_3378_count <= 110) {
      std::cout << "[READ32 0x3003378 MISMATCH] val=0x" << std::hex << val
                << " expected 0xfffffca4 count=" << std::dec
                << read32_3378_count << std::endl;
    }
  }

  return val;
}

uint32_t GBAMemory::ReadInstruction32(uint32_t address) {
  // Direct ROM access for instruction fetch - bypass EEPROM and other checks
  if ((address >> 24) >= 0x08 && (address >> 24) <= 0x0C) {
    uint32_t offset = address & 0x01FFFFFF;
    if (!rom.empty()) {
      uint8_t b0 = rom[offset % rom.size()];
      uint8_t b1 = rom[(offset + 1) % rom.size()];
      uint8_t b2 = rom[(offset + 2) % rom.size()];
      uint8_t b3 = rom[(offset + 3) % rom.size()];
      return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }
  }
  // Fallback
  return Read8(address) | (Read8(address + 1) << 8) |
         (Read8(address + 2) << 16) | (Read8(address + 3) << 24);
}

// Internal write that bypasses the GBA 8-bit write quirks for video memory
void GBAMemory::Write8Internal(uint32_t address, uint8_t value) {
  uint8_t region = (address >> 24);
  switch (region) {
  case 0x02: // WRAM (Board) (GBATEK: 0x02000000-0x0203FFFF)
    wram_board[address & MemoryMap::WRAM_BOARD_MASK] = value;
    break;
  case 0x03: // WRAM (Chip) (GBATEK: 0x03000000-0x03007FFF)
  {
    if (!IsIwramMappedAddress(address)) {
      break;
    }
    uint32_t offset = address & MemoryMap::WRAM_CHIP_MASK;
    wram_chip[offset] = value;
    break;
  }
  case 0x04: // IO Registers (GBATEK: 0x04000000-0x040003FF)
  {
    uint32_t offset = address & MemoryMap::IO_REG_MASK;
    if (offset < io_regs.size()) {
      io_regs[offset] = value;
    }
    break;
  }
  case 0x05: // Palette RAM (GBATEK: 0x05000000-0x050003FF)
  {
    uint32_t offset = address & MemoryMap::PALETTE_MASK;

    if (TraceGbaSpam()) {
      static int paletteWriteCount = 0;
      if (paletteWriteCount < 50) {
        paletteWriteCount++;
        std::cout << "[PALETTE WRITE] offset=0x" << std::hex << offset
                  << " val=0x" << (int)value;
        if (cpu)
          std::cout << " PC=0x" << cpu->GetRegister(15);
        std::cout << std::dec << std::endl;
      }
    }

    if (offset < palette_ram.size())
      palette_ram[offset] = value;
    break;
  }
  case 0x06: // VRAM (GBATEK: 0x06000000-0x06017FFF)
  {
    uint32_t offset = address & 0x1FFFFu;
    if (offset >= MemoryMap::VRAM_ACTUAL_SIZE) {
      offset -= 0x8000u;
    }

    // DEBUG: Trace writes to BG character area 0 (first 16KB: 0x0000-0x3FFF)
    if (TraceGbaSpam()) {
      static int vramCharWriteCount = 0;
      if (offset < 0x4000 && value != 0) {
        vramCharWriteCount++;
        if (vramCharWriteCount <= 20) {
          std::cout << "[VRAM Char0 Write] offset=0x" << std::hex << offset
                    << " val=0x" << (int)value;
          if (cpu)
            std::cout << " PC=0x" << cpu->GetRegister(15);
          std::cout << std::dec << std::endl;
        }
      }
    }
    // DISABLED: Too verbose during boot
    // if (offset < 0x4000 && value != 0) {
    //     vramCharWriteCount++;
    //     if (vramCharWriteCount <= 20) {
    //         std::cout << "[VRAM Char0 Write] offset=0x" << std::hex << offset
    //                   << " val=0x" << (int)value;
    //         if (cpu) std::cout << " PC=0x" << cpu->GetRegister(15);
    //         std::cout << std::dec << std::endl;
    //     }
    // }

    if (offset < vram.size())
      vram[offset] = value;
    break;
  }
  case 0x07: // OAM (GBATEK: 0x07000000-0x070003FF)
  {
    uint32_t offset = address & MemoryMap::OAM_MASK;
    if (offset < oam.size())
      oam[offset] = value;
    break;
  }
  }
}

void GBAMemory::Write8(uint32_t address, uint8_t value) {
  const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
  TracePpuIoWrite8(address, value, pc);
  const bool traceIrqHandWrites = EnvFlagCached("AIO_TRACE_IRQHAND_WRITES");
  const bool isIwram = IsIwramMappedAddress(address);
  const uint32_t iwramOff = address & 0x7FFFu;
  const bool isIrqHandPtrByte =
      isIwram && (iwramOff >= 0x7FFCu) && (iwramOff <= 0x7FFFu);
  const uint32_t oldIrqHandWord =
      (traceIrqHandWrites && isIrqHandPtrByte) ? Read32(0x03007FFCu) : 0u;
  // DEBUG: Trace writes to IWRAM literal pool area 0x3003460-0x3003480
  // (disabled) if (address >= 0x03003460 && address < 0x03003480) {
  //     std::cout << "[POOL WRITE] 0x" << std::hex << address
  //               << " = 0x" << (int)value;
  //     if (cpu) {
  //         std::cout << " PC=0x" << cpu->GetRegister(15);
  //     }
  //     std::cout << std::dec << std::endl;
  // }

  // Trace ALL writes to 0x3001500 area (disabled for performance)
  // if ((address >> 24) == 0x03 && (address & 0x7FFF) >= 0x1500 && (address &
  // 0x7FFF) < 0x1508) {
  //     std::cout << "[MIXBUF] Write8 0x" << std::hex << address
  //               << " = 0x" << (int)value;
  //     if (cpu) {
  //         std::cout << " PC=0x" << cpu->GetRegister(15);
  //     }
  //     std::cout << std::dec << std::endl;
  // }

  if (EnvFlagCached("AIO_TRACE_SMA2_SAVEVALID_WRITES") && cpu) {
    if (address >= 0x03007B80u && address < 0x03007C80u) {
      static int wlogs8 = 0;
      if (wlogs8 < 400) {
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "SMA2",
            "SAVEVALID W8 addr=0x%08x val=0x%02x PC=0x%08x", (unsigned)address,
            (unsigned)value, cpu ? (unsigned)cpu->GetRegister(15) : 0u);
        wlogs8++;
      }
    }
  }

  // SMA2 investigation: trace non-zero writes into the header staging buffer in
  // EWRAM. Enable with: AIO_TRACE_SMA2_STAGE_WRITES=1
  if (EnvFlagCached("AIO_TRACE_SMA2_STAGE_WRITES") && cpu) {
    if (address >= 0x020003C0u && address < 0x02000440u && value != 0) {
      static int stage8 = 0;
      if (stage8 < 500) {
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "SMA2",
            "STAGE W8 addr=0x%08x val=0x%02x PC=0x%08x", (unsigned)address,
            (unsigned)value, (unsigned)cpu->GetRegister(15));
        stage8++;
      }
    }
  }

  switch (address >> 24) {
  case 0x02: // WRAM (Board)
  {
    const uint32_t offset = address & 0x3FFFF;
    wram_board[offset] = value;

    // SMA2 investigation: trace the *exact writes* to the header staging bytes.
    // Enable with: AIO_TRACE_SMA2_HEADER_WRITES=1
    if (EnvFlagCached("AIO_TRACE_SMA2_HEADER_WRITES") && (offset >= 0x03C0) &&
        (offset < 0x03C8) && cpu) {
      static int headerWriteLogs = 0;
      if (headerWriteLogs < 300) {
        const uint32_t pc = (uint32_t)cpu->GetRegister(15);
        const uint32_t lr = (uint32_t)cpu->GetRegister(14);
        const uint32_t sp = (uint32_t)cpu->GetRegister(13);
        const uint32_t cpsr = (uint32_t)cpu->GetCPSR();
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "SMA2",
            "HDR_WRITE addr=0x%08x off=0x%04x val=0x%02x PC=0x%08x LR=0x%08x "
            "SP=0x%08x CPSR=0x%08x R0=0x%08x R1=0x%08x R2=0x%08x R3=0x%08x",
            (unsigned)address, (unsigned)offset, (unsigned)value, (unsigned)pc,
            (unsigned)lr, (unsigned)sp, (unsigned)cpsr,
            (unsigned)cpu->GetRegister(0), (unsigned)cpu->GetRegister(1),
            (unsigned)cpu->GetRegister(2), (unsigned)cpu->GetRegister(3));
        headerWriteLogs++;
      }
    }

    // SMA2 investigation: trace when the save header staging bytes in EWRAM
    // change. Enable with: AIO_TRACE_SMA2_HEADER=1 Watches
    // 0x020003C0..0x020003C7 (8 bytes).
    if (EnvFlagCached("AIO_TRACE_SMA2_HEADER") && (offset >= 0x03C0) &&
        (offset < 0x0400)) {
      static bool initialized = false;
      static uint64_t lastHeaderLE = 0;

      constexpr uint32_t kHeaderBase = 0x03C0;
      if (kHeaderBase + 7 < wram_board.size()) {
        uint64_t headerLE = 0;
        for (int i = 0; i < 8; ++i) {
          headerLE |= (uint64_t)wram_board[kHeaderBase + (uint32_t)i]
                      << (i * 8);
        }

        if (!initialized) {
          initialized = true;
          lastHeaderLE = headerLE;
        } else if (headerLE != lastHeaderLE) {
          constexpr uint64_t kRef = 0xFEB801010101DA69ULL;
          constexpr uint64_t kOut = 0xFEBC00000000DA69ULL;
          const bool logAll = EnvFlagCached("AIO_TRACE_SMA2_HEADER_ALL");
          const bool interesting = (headerLE == kRef) || (headerLE == kOut) ||
                                   (lastHeaderLE == kRef) ||
                                   (lastHeaderLE == kOut);
          if (logAll || interesting) {
            static int headerAllLogs = 0;
            // Keep spam bounded when logging intermediates.
            if (!logAll || headerAllLogs < 400) {
              const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
              const uint32_t lr = cpu ? (uint32_t)cpu->GetRegister(14) : 0u;
              const uint32_t sp = cpu ? (uint32_t)cpu->GetRegister(13) : 0u;
              AIO::Emulator::Common::Logger::Instance().LogFmt(
                  AIO::Emulator::Common::LogLevel::Info, "SMA2",
                  "EWRAM header @0x020003C0 old=0x%016llx new=0x%016llx "
                  "PC=0x%08x LR=0x%08x SP=0x%08x",
                  (unsigned long long)lastHeaderLE,
                  (unsigned long long)headerLE, (unsigned)pc, (unsigned)lr,
                  (unsigned)sp);
              headerAllLogs++;
            }
          }
          lastHeaderLE = headerLE;
        }
      }
    }
    break;
  }
  case 0x03: // WRAM (Chip)
  {
    if (!IsIwramMappedAddress(address)) {
      break;
    }
    const uint32_t offset = address & 0x7FFFu;
    wram_chip[offset] = value;
    break;
  }
  case 0x04: // IO Registers (GBATEK: 0x04000000-0x040003FF)
  {
    uint32_t offset = address & MemoryMap::IO_REG_MASK;

    // Handle IF (Interrupt Request) - Write 1 to Clear (GBATEK)
    if (offset == IORegs::IF || offset == IORegs::IF + 1) {
      if (offset < io_regs.size()) {
        io_regs[offset] &= ~value;
      }
    } else {
      // Protect DISPSTAT (0x04) Read-Only bits (0-2) (GBATEK)
      if (offset == IORegs::DISPSTAT) {
        uint8_t currentVal = io_regs[offset];
        uint8_t readOnlyMask = 0x07;
        value = (value & ~readOnlyMask) | (currentVal & readOnlyMask);
      }

      // Handle DMA Enable (GBATEK: DMA0-3 CNT_H enable bit)
      bool dmaLatchNeeded = false;
      int dmaChannel = -1;
      if (offset == IORegs::DMA0CNT_H + 1 || offset == IORegs::DMA1CNT_H + 1 ||
          offset == IORegs::DMA2CNT_H + 1 || offset == IORegs::DMA3CNT_H + 1) {
        if (offset == IORegs::DMA0CNT_H + 1)
          dmaChannel = 0;
        else if (offset == IORegs::DMA1CNT_H + 1)
          dmaChannel = 1;
        else if (offset == IORegs::DMA2CNT_H + 1)
          dmaChannel = 2;
        else if (offset == IORegs::DMA3CNT_H + 1)
          dmaChannel = 3;

        // NOTE: Write8 to the high byte (offset+1) means value is only a single
        // byte Bit 15 (Enable) is bit 7 of the high byte!
        bool wasEnabled = (io_regs[offset] & 0x80) !=
                          0; // Bit 7 of high byte = bit 15 of full 16-bit
        bool willBeEnabled = (value & 0x80) != 0;

        if (!wasEnabled && willBeEnabled) {
          dmaLatchNeeded = true;
        }
      }

      if (offset < io_regs.size()) {
        io_regs[offset] = value;
      }

      // KEYCNT changes can make the keypad IRQ condition become true without
      // any immediate KEYINPUT transition, so re-evaluate after writes.
      if (offset == IORegs::KEYCNT || offset == IORegs::KEYCNT + 1) {
        EvaluateKeypadIRQ();
      }

      // Handle DMA latch after io_regs is updated
      if (dmaLatchNeeded && dmaChannel >= 0) {
        uint32_t dmaBase =
            IORegs::DMA0SAD + (dmaChannel * IORegs::DMA_CHANNEL_SIZE);
        dmaInternalSrc[dmaChannel] =
            io_regs[dmaBase] | (io_regs[dmaBase + 1] << 8) |
            (io_regs[dmaBase + 2] << 16) | (io_regs[dmaBase + 3] << 24);
        dmaInternalDst[dmaChannel] =
            io_regs[dmaBase + 4] | (io_regs[dmaBase + 5] << 8) |
            (io_regs[dmaBase + 6] << 16) | (io_regs[dmaBase + 7] << 24);

        static int dmaStartLogs[4] = {0, 0, 0, 0};
        if (verboseLogs && dmaStartLogs[dmaChannel] < 8) {
          uint16_t ctrl = io_regs[dmaBase + 10] | (io_regs[dmaBase + 11] << 8);
          uint16_t cnt = io_regs[dmaBase + 8] | (io_regs[dmaBase + 9] << 8);
          dmaStartLogs[dmaChannel]++;
          std::cout << "[DMA" << dmaChannel << " START] Src=0x" << std::hex
                    << dmaInternalSrc[dmaChannel] << " Dst=0x"
                    << dmaInternalDst[dmaChannel] << " Cnt=0x" << cnt
                    << " Ctrl=0x" << ctrl << " timing=" << std::dec
                    << ((ctrl >> 12) & 3) << " repeat=" << ((ctrl >> 9) & 1)
                    << " width="
                    << (((ctrl & DMAControl::TRANSFER_32BIT) != 0) ? 32 : 16)
                    << (cpu ? " PC=0x" : "") << std::hex;
          if (cpu)
            std::cout << cpu->GetRegister(15);
          std::cout << std::dec << std::endl;
        }

        // DEBUG: Trace DMA setup when dst is around 0x3001500
        {
          uint16_t ctrl = io_regs[dmaBase + 10] | (io_regs[dmaBase + 11] << 8);
          uint16_t cnt = io_regs[dmaBase + 8] | (io_regs[dmaBase + 9] << 8);
          uint32_t dst = dmaInternalDst[dmaChannel];
          uint32_t src = dmaInternalSrc[dmaChannel];
          if (dst == 0x3001500 || src == 0x3001500) {
            // Also show the raw DAD register bytes
            std::cout << "[DMA" << dmaChannel << " SETUP] "
                      << "Src=0x" << std::hex << src << " Dst=0x" << dst
                      << " (raw DAD: ";
            for (int i = 4; i < 8; i++) {
              std::cout << std::hex << std::setw(2) << std::setfill('0')
                        << (int)io_regs[dmaBase + i];
              if (i < 7)
                std::cout << " ";
            }
            std::cout << ")"
                      << " Cnt=0x" << cnt << " Ctrl=0x" << ctrl
                      << " (raw CNT_H: " << std::hex << std::setw(2)
                      << std::setfill('0') << (int)io_regs[dmaBase + 10] << " "
                      << std::setw(2) << std::setfill('0')
                      << (int)io_regs[dmaBase + 11] << ")"
                      << " DstCtrl=" << std::dec << ((ctrl >> 5) & 3)
                      << " SrcCtrl=" << ((ctrl >> 7) & 3) << " PC=0x"
                      << std::hex;
            if (cpu)
              std::cout << cpu->GetRegister(15);
            std::cout << std::dec << std::endl;
          }
        }

        uint16_t control = io_regs[dmaBase + 10] | (io_regs[dmaBase + 11] << 8);
        int timing = (control >> 12) & 3;
        if (timing == 0)
          PerformDMA(dmaChannel);
      }
    }
    break;
  }
  case 0x05: // Palette RAM - 8-bit writes duplicate byte
  {
    const uint16_t dispcnt = (uint16_t)(io_regs[IORegs::DISPCNT] |
                                        (io_regs[IORegs::DISPCNT + 1] << 8));
    const bool forcedBlank = (dispcnt & 0x0080u) != 0;
    if (ppuTimingValid && !forcedBlank) {
      const bool visible = (ppuTimingScanline < 160) && (ppuTimingCycle < 960);
      if (visible) {
        break;
      }
    }

    uint32_t offset = address & 0x3FF;
    uint32_t alignedOffset = offset & ~1;
    if (alignedOffset + 1 < palette_ram.size()) {
      palette_ram[alignedOffset] = value;
      palette_ram[alignedOffset + 1] = value;
    }
    break;
  }
  case 0x06: // VRAM - 8-bit writes duplicate on the 16-bit bus
  {
    const uint16_t dispcnt = (uint16_t)(io_regs[IORegs::DISPCNT] |
                                        (io_regs[IORegs::DISPCNT + 1] << 8));
    const bool forcedBlank = (dispcnt & 0x0080u) != 0;
    if (ppuTimingValid && !forcedBlank) {
      const bool visible = (ppuTimingScanline < 160) && (ppuTimingCycle < 960);
      if (visible) {
        break;
      }
    }

    uint32_t offset = address & 0x1FFFFu;
    if (offset >= 0x18000u) {
      offset -= 0x8000u;
    }

    const uint8_t mode = (uint8_t)(dispcnt & 0x7u);
    const bool bitmapMode = (mode >= 3u);
    const uint32_t objStart = bitmapMode ? 0x14000u : 0x10000u;
    const bool isObjVram = (offset >= objStart) && (offset < 0x18000u);
    if (isObjVram) {
      break; // GBATEK: OBJ VRAM byte writes are ignored.
    }

    uint32_t alignedOffset = offset & ~1;
    if (alignedOffset + 1 < vram.size()) {
      vram[alignedOffset] = value;
      vram[alignedOffset + 1] = value;
    }
    break;
  }
  case 0x07: // OAM - 8-bit writes ignored
  {
    // 8-bit writes are ignored by HW; halfword/word writes are handled
    // elsewhere.
    break;
  }
  case 0x0E: // SRAM/Flash
  {
    if (!hasSRAM)
      return; // Ignore writes if no SRAM/Flash present

    uint32_t offset = address & 0xFFFF;

    if (!isFlash) {
      // SRAM Write
      if (offset < sram.size()) {
        sram[offset] = value;
      }
      return;
    }

    // Flash Command State Machine
    // Handle Reset (0xF0) at any time
    if (value == 0xF0) {
      flashState = 0;
      return;
    }

    switch (flashState) {
    case 0: // Idle
      if (offset == 0x5555 && value == 0xAA)
        flashState = 1;
      break;
    case 1: // Seen 0xAA
      if (offset == 0x2AAA && value == 0x55)
        flashState = 2;
      else
        flashState = 0; // Reset on error
      break;
    case 2: // Seen 0x55, Expecting Command
      if (offset == 0x5555) {
        if (value == 0x90)
          flashState = 3; // ID Mode
        else if (value == 0x80)
          flashState = 5; // Erase Setup
        else if (value == 0xA0)
          flashState = 8; // Program
        else if (value == 0xB0)
          flashState = 9; // Bank Switch
        else
          flashState = 0;
      } else {
        flashState = 0;
      }
      break;
    case 3: // ID Mode
      // Writes in ID mode might reset? 0xF0 handled above.
      break;
    case 5: // Erase Setup (Seen 0x80), Expecting 0xAA
      if (offset == 0x5555 && value == 0xAA)
        flashState = 6;
      else
        flashState = 0;
      break;
    case 6: // Erase Cmd 1 (Seen 0xAA), Expecting 0x55
      if (offset == 0x2AAA && value == 0x55)
        flashState = 7;
      else
        flashState = 0;
      break;
    case 7: // Erase Cmd 2 (Seen 0x55), Expecting Action
      if (offset == 0x5555 && value == 0x10) {
        // Chip Erase
        std::fill(sram.begin(), sram.end(), 0xFF);
        flashState = 0;
      } else if (value == 0x30) {
        // Sector Erase (4KB)
        // Address determines sector
        uint32_t sectorBase = offset & 0xF000;
        if (sram.size() > 65536)
          sectorBase += (flashBank * 65536);

        if (sectorBase < sram.size()) {
          size_t end = std::min((size_t)sectorBase + 4096, sram.size());
          std::fill(sram.begin() + sectorBase, sram.begin() + end, 0xFF);
        }
        flashState = 0;
      } else {
        flashState = 0;
      }
      break;
    case 8: // Program Byte
    {
      uint32_t target = offset;
      if (sram.size() > 65536)
        target += (flashBank * 65536);

      if (target < sram.size()) {
        sram[target] = value;
      }
      flashState = 0;
      break;
    }
    case 9: // Bank Switch
      if (offset == 0) {
        flashBank = value & 1;
      }
      flashState = 0;
      break;
    }
    break;
  }
  case 0x0D: // EEPROM - ignore 8-bit writes
    // Some titles may clock the EEPROM serial interface via byte writes.
    // Treat this as a normal EEPROM bit write (D0).
    WriteEEPROM(value);
    break;
  }

  if (traceIrqHandWrites && isIrqHandPtrByte) {
    const uint32_t newIrqHandWord = Read32(0x03007FFCu);
    if (newIrqHandWord != oldIrqHandWord) {
      static int logs = 0;
      if (logs++ < 300) {
        const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
        const uint32_t lr = cpu ? (uint32_t)cpu->GetRegister(14) : 0u;
        const uint32_t sp = cpu ? (uint32_t)cpu->GetRegister(13) : 0u;
        const uint32_t cpsr = cpu ? (uint32_t)cpu->GetCPSR() : 0u;
        const uint32_t byteIndex = iwramOff - 0x7FFCu;
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "IRQHAND",
            "IRQHAND_WRITE8 addr=0x%08x byte=%u val=0x%02x old=0x%08x "
            "new=0x%08x PC=0x%08x LR=0x%08x SP=0x%08x CPSR=0x%08x",
            (unsigned)address, (unsigned)byteIndex, (unsigned)value,
            (unsigned)oldIrqHandWord, (unsigned)newIrqHandWord, (unsigned)pc,
            (unsigned)lr, (unsigned)sp, (unsigned)cpsr);
      }
    }
  }

  if (isIrqHandPtrByte) {
    ClampIrqHandlerWord();
  }
}

void GBAMemory::Write16(uint32_t address, uint16_t value) {
  const bool traceIrqHandWrites = EnvFlagCached("AIO_TRACE_IRQHAND_WRITES");
  const bool isIwram = IsIwramMappedAddress(address);
  const uint32_t iwramOff = address & 0x7FFFu;
  const bool isIrqHandPtrHalf = isIwram && ((iwramOff & ~1u) == 0x7FFCu);
  const uint32_t oldIrqHandWord =
      (traceIrqHandWrites && isIrqHandPtrHalf) ? Read32(0x03007FFCu) : 0u;
  // EEPROM Handling: only for EEPROM-save cartridges.
  uint8_t region = (address >> 24);

  if (region == 0x0D && (!hasSRAM && !isFlash)) {
    WriteEEPROM(value);
    return;
  }

  // Diagnostics: detect unaligned IO halfword writes.
  // Enable with: AIO_TRACE_IO_UNALIGNED=1 (or legacy
  // AIO_TRACE_IO_ODD_HALFWORD=1)
  const bool traceIoUnaligned = EnvFlagCached("AIO_TRACE_IO_UNALIGNED") ||
                                EnvFlagCached("AIO_TRACE_IO_ODD_HALFWORD");
  if (traceIoUnaligned && region == 0x04 && (address & 1u)) {
    static int unalignedIoWrite16Logs = 0;
    if (unalignedIoWrite16Logs < 400) {
      const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
      AIO::Emulator::Common::Logger::Instance().LogFmt(
          AIO::Emulator::Common::LogLevel::Info, "IO",
          "UNALIGNED Write16 addr=0x%08x val=0x%04x PC=0x%08x",
          (unsigned)address, (unsigned)value, (unsigned)pc);
      unalignedIoWrite16Logs++;
    }
  }

  // GBA IO registers are fundamentally 16-bit; halfword accesses are aligned.
  // Some titles issue unaligned halfword stores into IO space; on hardware
  // these behave like aligned accesses.
  if (region == 0x04 && (address & 1u)) {
    address &= ~1u;
  }

  const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
  TracePpuIoWrite16(address, value, pc);

  // SMA2 investigation: trace non-zero writes into the header staging buffer in
  // EWRAM. Enable with: AIO_TRACE_SMA2_STAGE_WRITES=1
  if (EnvFlagCached("AIO_TRACE_SMA2_STAGE_WRITES") && cpu) {
    if (address >= 0x020003C0u && address < 0x02000440u && value != 0) {
      static int stage16 = 0;
      if (stage16 < 500) {
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "SMA2",
            "STAGE W16 addr=0x%08x val=0x%04x PC=0x%08x", (unsigned)address,
            (unsigned)value, (unsigned)cpu->GetRegister(15));
        stage16++;
      }
    }
  }

  if (EnvFlagCached("AIO_TRACE_SMA2_SAVEVALID_WRITES") && cpu) {
    if (address >= 0x03007B80u && address < 0x03007C80u) {
      static int wlogs16 = 0;
      if (wlogs16 < 400) {
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "SMA2",
            "SAVEVALID W16 addr=0x%08x val=0x%04x PC=0x%08x", (unsigned)address,
            (unsigned)value, cpu ? (unsigned)cpu->GetRegister(15) : 0u);
        wlogs16++;
      }
    }
  }

  const bool traceSMA2DMABuf = EnvFlagCached("AIO_TRACE_SMA2_DMABUF");
  const bool isIWRAM = ((address >> 24) == 0x03);
  const uint32_t addrNoAlign = address;

  if ((address & 0xFF000000) == 0x04000000) {
    uint32_t offset = address & 0x3FF;

    // IF (Interrupt Request) is write-1-to-clear (GBATEK).
    // Many BIOS/game paths (including our IRQ trampoline) acknowledge IRQs via
    // halfword stores to 0x04000202.
    if (offset == IORegs::IF) {
      const uint16_t cur =
          (uint16_t)(io_regs[IORegs::IF] | (io_regs[IORegs::IF + 1] << 8));
      const uint16_t cleared = (uint16_t)(cur & (uint16_t)~value);
      io_regs[IORegs::IF] = (uint8_t)(cleared & 0xFFu);
      io_regs[IORegs::IF + 1] = (uint8_t)((cleared >> 8) & 0xFFu);
      return;
    }

    if (EnvFlagCached("AIO_TRACE_WAITCNT") && (offset == 0x204)) {
      static uint32_t lastPc = 0xFFFFFFFFu;
      static uint16_t lastVal = 0xFFFFu;
      static uint32_t suppressed = 0;

      const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
      const uint16_t oldVal =
          (uint16_t)(io_regs[0x204] | (io_regs[0x205] << 8));

      if (pc == lastPc && value == lastVal) {
        suppressed++;
        // Still provide some signal in case this is a tight loop.
        if (suppressed == 1 || (suppressed % 1024u) == 0u) {
          AIO::Emulator::Common::Logger::Instance().LogFmt(
              AIO::Emulator::Common::LogLevel::Info, "WAITCNT",
              "WAITCNT write16 repeat val=0x%04x PC=0x%08x repeats=%u "
              "(old=0x%04x)",
              (unsigned)value, (unsigned)pc, (unsigned)suppressed,
              (unsigned)oldVal);
        }
      } else {
        if (suppressed > 0) {
          AIO::Emulator::Common::Logger::Instance().LogFmt(
              AIO::Emulator::Common::LogLevel::Info, "WAITCNT",
              "WAITCNT write16 repeats summary val=0x%04x PC=0x%08x repeats=%u",
              (unsigned)lastVal, (unsigned)lastPc, (unsigned)suppressed);
        }
        suppressed = 0;
        lastPc = pc;
        lastVal = value;
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "WAITCNT",
            "WAITCNT write16 old=0x%04x new=0x%04x PC=0x%08x", (unsigned)oldVal,
            (unsigned)value, (unsigned)pc);
      }
    }

    // Instrument IE/IME writes to confirm interrupt enablement timing
    if (offset == IORegs::IE || offset == IORegs::IE + 1 ||
        offset == IORegs::IME || offset == IORegs::IME + 1) {
      static int irqLogCount = 0;
      if (irqLogCount < 400) {
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Debug, "IRQ",
            "IRQ REG WRITE16 #%d offset=0x%03x val=0x%04x PC=0x%08x",
            irqLogCount, (unsigned)offset, (unsigned)value,
            cpu ? (unsigned)cpu->GetRegister(15) : 0u);
        irqLogCount++;
      }
    }

    // Log early display configuration writes (limited to keep noise down)
    static int dispcntLogs = 0;
    static int bgcntLogs = 0;
    static int bghofsLogs = 0;
    static int bgvofsLogs = 0;

    auto logReg = [&](const char *name) {
      if (!TraceGbaSpam())
        return;
      std::cout << "[IO] " << name << " write16: 0x" << std::hex << value;
      if (cpu)
        std::cout << " PC=0x" << cpu->GetRegister(15);
      std::cout << std::dec << std::endl;
    };

    if (offset == IORegs::DISPCNT && dispcntLogs < 8) {
      dispcntLogs++;
      logReg("DISPCNT");
    }
    if ((offset >= 0x08 && offset <= 0x0E) && bgcntLogs < 16) {
      // BG0CNT, BG1CNT, BG2CNT, BG3CNT
      bgcntLogs++;
      std::ostringstream name;
      name << "BG" << ((offset - 0x08) / 2) << "CNT";
      logReg(name.str().c_str());
    }
    if ((offset == 0x10 || offset == 0x12 || offset == 0x14 ||
         offset == 0x16) &&
        bghofsLogs < 16) {
      // BGxHOFS
      bghofsLogs++;
      std::ostringstream name;
      name << "BG" << ((offset - 0x10) / 2) << "HOFS";
      logReg(name.str().c_str());
    }
    if ((offset == 0x11 || offset == 0x13 || offset == 0x15 ||
         offset == 0x17) &&
        bgvofsLogs < 16) {
      // BGxVOFS
      bgvofsLogs++;
      std::ostringstream name;
      name << "BG" << ((offset - 0x11) / 2) << "VOFS";
      logReg(name.str().c_str());
    }

    // DISPSTAT Write Masking - preserve read-only bits
    if (offset == 0x04) {
      uint16_t currentVal = io_regs[offset] | (io_regs[offset + 1] << 8);
      uint16_t readOnlyMask = 0x0007;
      value = (value & ~readOnlyMask) | (currentVal & readOnlyMask);
    }

    // SOUNDCNT_H (0x82) - DMA Sound Control
    if (offset == 0x82) {
      // Handle FIFO reset bits
      if (apu) {
        if (value & 0x0800)
          apu->ResetFIFO_A();
        if (value & 0x8000)
          apu->ResetFIFO_B();
      }
      value &= ~0x8800; // Clear reset bits
    }

    // SOUNDCNT_X - preserve status bits
    if (offset == IORegs::SOUNDCNT_X) {
      uint16_t currentVal = io_regs[offset] | (io_regs[offset + 1] << 8);
      value = (value & 0x80) | (currentVal & 0x0F);
    }

    // Timer Control (GBATEK timers 0-3)
    if (offset >= IORegs::TM0CNT_L && offset <= IORegs::TM3CNT_H) {
      int timerIdx = (offset - IORegs::TM0CNT_L) / IORegs::TIMER_CHANNEL_SIZE;

      // TMxCNT_L (reload) writes while enabled: hardware immediately reloads
      // the counter and resets the prescaler divider.
      if ((offset % IORegs::TIMER_CHANNEL_SIZE) == 0) { // TMxCNT_L
        const uint32_t ctrlOff =
            IORegs::TM0CNT_L + (timerIdx * IORegs::TIMER_CHANNEL_SIZE) + 2u;
        const uint16_t controlNow =
            (uint16_t)(io_regs[ctrlOff] | (io_regs[ctrlOff + 1] << 8));
        if (controlNow & TimerControl::ENABLE) {
          timerCounters[timerIdx] = value;
          timerPrescalerCounters[timerIdx] = 0;
        }
      }

      if ((offset % IORegs::TIMER_CHANNEL_SIZE) == 2) { // TMxCNT_H
        uint16_t oldControl = io_regs[offset] | (io_regs[offset + 1] << 8);
        bool wasEnabled = oldControl & TimerControl::ENABLE;
        bool nowEnabled = value & TimerControl::ENABLE;

        if (!wasEnabled && nowEnabled) {
          uint16_t reload = io_regs[offset - 2] | (io_regs[offset - 1] << 8);
          timerCounters[timerIdx] = reload;
          timerPrescalerCounters[timerIdx] = 0;
        }
      }
    }
  }

  // For video memory, bypass 8-bit quirks.
  // Also optionally align unaligned halfword stores to match HW behavior
  // (video memory is fundamentally 16-bit addressed).
  if (region == 0x05 || region == 0x06 || region == 0x07) {
    address &= ~1u;
  }

  // Video memory access restrictions (GBATEK): during active display
  // (non-forced-blank), CPU writes to VRAM/Palette/OAM are restricted;
  // VRAM/Palette are writable during HBlank/VBlank, and OAM is writable during
  // VBlank or during HBlank only if H-Blank Interval Free (DISPCNT bit 5) is
  // set.
  if (region == 0x05 || region == 0x06 || region == 0x07) {
    const uint16_t dispcnt = (uint16_t)(io_regs[IORegs::DISPCNT] |
                                        (io_regs[IORegs::DISPCNT + 1] << 8));
    const bool forcedBlank = (dispcnt & 0x0080u) != 0;

    // When PPU timing isn't active (e.g., unit-test setup before
    // constructing/stepping the PPU), allow deterministic writes.
    if (ppuTimingValid && !forcedBlank) {
      const bool vblank = (ppuTimingScanline >= 160);
      const bool hblank = (ppuTimingScanline < 160) && (ppuTimingCycle >= 960);
      const bool visible = (ppuTimingScanline < 160) && (ppuTimingCycle < 960);

      if (region == 0x05 || region == 0x06) {
        if (visible && !vblank && !hblank) {
          return;
        }
      } else {
        const bool hblankIntervalFree = (dispcnt & 0x0020u) != 0;
        const bool canWrite = vblank || (hblank && hblankIntervalFree);
        if (!canWrite) {
          return;
        }
      }
    }
  }

  // Optional: trace CPU halfword writes into Palette RAM.
  // Enable with: AIO_TRACE_PALETTE_CPU_WRITES=1
  if (region == 0x05 && EnvFlagCached("AIO_TRACE_PALETTE_CPU_WRITES") && cpu) {
    static int palW16Logs = 0;
    if (palW16Logs < 2000) {
      const uint32_t pc2 = (uint32_t)cpu->GetRegister(15);
      const uint32_t lr2 = (uint32_t)cpu->GetRegister(14);
      AIO::Emulator::Common::Logger::Instance().LogFmt(
          AIO::Emulator::Common::LogLevel::Info, "PAL",
          "CPU W16 pal addr=0x%08x val=0x%04x PC=0x%08x LR=0x%08x",
          (unsigned)address, (unsigned)value, (unsigned)pc2, (unsigned)lr2);
      palW16Logs++;
    }
  }

  // IMPORTANT: Don't implement halfword writes via two Write8() calls for RAM.
  // Write8() has IRQ handler clamping logic and must not run on intermediate
  // byte states.
  if (region == 0x03 && IsIwramMappedAddress(address)) {
    const uint32_t off0 = address & 0x7FFFu;
    const uint32_t off1 = (address + 1u) & 0x7FFFu;
    if (off0 < wram_chip.size())
      wram_chip[off0] = (uint8_t)(value & 0xFFu);
    if (off1 < wram_chip.size())
      wram_chip[off1] = (uint8_t)((value >> 8) & 0xFFu);

    if (isIrqHandPtrHalf) {
      ClampIrqHandlerWord();
    }

    if (traceIrqHandWrites && isIrqHandPtrHalf) {
      const uint32_t newIrqHandWord = Read32(0x03007FFCu);
      if (newIrqHandWord != oldIrqHandWord) {
        static int logs = 0;
        if (logs++ < 300) {
          const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
          const uint32_t lr = cpu ? (uint32_t)cpu->GetRegister(14) : 0u;
          const uint32_t sp = cpu ? (uint32_t)cpu->GetRegister(13) : 0u;
          const uint32_t cpsr = cpu ? (uint32_t)cpu->GetCPSR() : 0u;
          AIO::Emulator::Common::Logger::Instance().LogFmt(
              AIO::Emulator::Common::LogLevel::Info, "IRQHAND",
              "IRQHAND_WRITE16 addr=0x%08x val=0x%04x old=0x%08x new=0x%08x "
              "PC=0x%08x LR=0x%08x SP=0x%08x CPSR=0x%08x",
              (unsigned)address, (unsigned)value, (unsigned)oldIrqHandWord,
              (unsigned)newIrqHandWord, (unsigned)pc, (unsigned)lr,
              (unsigned)sp, (unsigned)cpsr);
        }
      }
    }
    return;
  } else if (region == 0x02) {
    const uint32_t off0 = address & 0x3FFFFu;
    const uint32_t off1 = (address + 1u) & 0x3FFFFu;
    if (off0 < wram_board.size())
      wram_board[off0] = (uint8_t)(value & 0xFFu);
    if (off1 < wram_board.size())
      wram_board[off1] = (uint8_t)((value >> 8) & 0xFFu);
    return;
  }

  if (region == 0x05 || region == 0x06 || region == 0x07) {
    Write8Internal(address, value & 0xFF);
    Write8Internal(address + 1, (value >> 8) & 0xFF);
  } else {
    Write8(address, value & 0xFF);
    Write8(address + 1, (value >> 8) & 0xFF);
  }

  // SMA2 investigation: detect when the EEPROM write DMA bitstream buffer is
  // constructed. Enable with: AIO_TRACE_SMA2_DMABUF=1 Known DMA3 SAD during
  // EEPROM writes (from rewrite tracer): 0x03007CBC, count=0x51 halfwords.
  if (traceSMA2DMABuf && isIWRAM) {
    constexpr uint32_t kBufBase = 0x03007CBC;
    constexpr uint32_t kBufSizeBytes = 0x51u * 2u;

    const uint32_t a = (addrNoAlign & ~1u);
    if (a >= kBufBase && a < (kBufBase + kBufSizeBytes)) {
      auto chip16 = [&](uint32_t addr) -> uint16_t {
        const uint32_t off = addr & 0x7FFF;
        if (off + 1 >= wram_chip.size())
          return 0;
        return (uint16_t)(wram_chip[off] | (wram_chip[off + 1] << 8));
      };

      // Decode current buffer state (LSB per halfword = serial bit).
      uint8_t bits[0x51];
      for (uint32_t i = 0; i < 0x51; ++i) {
        bits[i] = (uint8_t)(chip16(kBufBase + i * 2) & 1);
      }

      const uint8_t start = bits[0];
      const uint8_t cmd = bits[1];
      uint32_t addr14 = 0;
      for (uint32_t i = 0; i < 14; ++i) {
        addr14 = (addr14 << 1) | bits[2 + i];
      }
      uint64_t data64 = 0;
      for (uint32_t i = 0; i < 64; ++i) {
        data64 = (data64 << 1) | bits[2 + 14 + i];
      }
      const uint8_t term = bits[2 + 14 + 64];
      const uint32_t block = (addr14 & 0x3FF);

      // Only log when this is plausibly an EEPROM write request.
      if (start == 1 && cmd == 0 && term == 0) {
        static bool initialized = false;
        static uint32_t lastBlock = 0xFFFFFFFFu;
        static uint64_t lastData = 0;
        static uint8_t lastCmd = 0xFF;
        static uint32_t lastPc = 0xFFFFFFFFu;
        static uint32_t lastLr = 0xFFFFFFFFu;

        if (!initialized) {
          initialized = true;
          lastBlock = block;
          lastData = data64;
          lastCmd = cmd;
          lastPc = 0xFFFFFFFFu;
          lastLr = 0xFFFFFFFFu;
        }

        // Log on payload change (or new call-site) for the interesting
        // block(s).
        if (block == 2) {
          constexpr uint64_t kRef = 0xFEB801010101DA69ULL;
          constexpr uint64_t kOut = 0xFEBC00000000DA69ULL;
          if (data64 == kRef || data64 == kOut) {
            const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
            const uint32_t lr = cpu ? (uint32_t)cpu->GetRegister(14) : 0u;
            const bool siteChanged = (pc != lastPc) || (lr != lastLr);
            const bool payloadChanged = (data64 != lastData);
            if (siteChanged || payloadChanged) {
              lastPc = pc;
              lastLr = lr;
              AIO::Emulator::Common::Logger::Instance().LogFmt(
                  AIO::Emulator::Common::LogLevel::Info, "SMA2",
                  "DMA3 EEPROM buf decode: block=%u data=0x%016llx PC=0x%08x "
                  "LR=0x%08x",
                  (unsigned)block, (unsigned long long)data64, (unsigned)pc,
                  (unsigned)lr);
            }
          }
        }

        lastBlock = block;
        lastData = data64;
        lastCmd = cmd;
      }
    }
  }

  if (traceIrqHandWrites && isIrqHandPtrHalf) {
    const uint32_t newIrqHandWord = Read32(0x03007FFCu);
    if (newIrqHandWord != oldIrqHandWord) {
      static int logs = 0;
      if (logs++ < 300) {
        const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
        const uint32_t lr = cpu ? (uint32_t)cpu->GetRegister(14) : 0u;
        const uint32_t sp = cpu ? (uint32_t)cpu->GetRegister(13) : 0u;
        const uint32_t cpsr = cpu ? (uint32_t)cpu->GetCPSR() : 0u;
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "IRQHAND",
            "IRQHAND_WRITE16 addr=0x%08x val=0x%04x old=0x%08x new=0x%08x "
            "PC=0x%08x LR=0x%08x SP=0x%08x CPSR=0x%08x",
            (unsigned)address, (unsigned)value, (unsigned)oldIrqHandWord,
            (unsigned)newIrqHandWord, (unsigned)pc, (unsigned)lr, (unsigned)sp,
            (unsigned)cpsr);
      }
    }
  }

  if (isIrqHandPtrHalf) {
    ClampIrqHandlerWord();
  }
}

void GBAMemory::Write32(uint32_t address, uint32_t value) {
  const bool traceIrqHandWrites = EnvFlagCached("AIO_TRACE_IRQHAND_WRITES");
  const bool isIrqHandPtrWord =
      IsIwramMappedAddress(address) && ((address & 0x7FFFu) == 0x7FFCu);
  const uint32_t oldIrqHandWord =
      (traceIrqHandWrites && isIrqHandPtrWord) ? Read32(0x03007FFCu) : 0u;

  // Diagnostics: detect unaligned IO word writes.
  // Enable with: AIO_TRACE_IO_UNALIGNED=1 (or legacy
  // AIO_TRACE_IO_ODD_HALFWORD=1)
  const bool traceIoUnaligned = EnvFlagCached("AIO_TRACE_IO_UNALIGNED") ||
                                EnvFlagCached("AIO_TRACE_IO_ODD_HALFWORD");
  if (traceIoUnaligned && ((address >> 24) == 0x04) && (address & 3u)) {
    static int unalignedIoWrite32Logs = 0;
    if (unalignedIoWrite32Logs < 400) {
      const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
      AIO::Emulator::Common::Logger::Instance().LogFmt(
          AIO::Emulator::Common::LogLevel::Info, "IO",
          "UNALIGNED Write32 addr=0x%08x val=0x%08x PC=0x%08x",
          (unsigned)address, (unsigned)value, (unsigned)pc);
      unalignedIoWrite32Logs++;
    }
  }

  // IO space word accesses are aligned on hardware.
  if (((address >> 24) == 0x04) && (address & 3u)) {
    address &= ~3u;
  }

  // IE/IF are adjacent 16-bit registers at 0x04000200/0x04000202.
  // A 32-bit write starting at 0x04000200 should behave as:
  // - low16: normal write to IE
  // - high16: write-1-to-clear to IF
  if ((address & 0xFF000000u) == 0x04000000u) {
    const uint32_t offset = address & 0x3FFu;
    if (offset == IORegs::IE) {
      Write16(0x04000200u, (uint16_t)(value & 0xFFFFu));
      Write16(0x04000202u, (uint16_t)(value >> 16));
      return;
    }
  }

  const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
  TracePpuIoWrite32(address, value, pc);

  // Instrument IE/IME writes done via 32-bit access
  if ((address & 0xFF000000) == 0x04000000) {
    uint32_t offset = address & 0x3FF;

    if (EnvFlagCached("AIO_TRACE_WAITCNT") && (offset == 0x204)) {
      AIO::Emulator::Common::Logger::Instance().LogFmt(
          AIO::Emulator::Common::LogLevel::Info, "WAITCNT",
          "WAITCNT write32 val=0x%08x (low16=0x%04x) PC=0x%08x",
          (unsigned)value, (unsigned)(value & 0xFFFFu),
          cpu ? (unsigned)cpu->GetRegister(15) : 0u);
    }

    if ((offset == IORegs::IE) || (offset == IORegs::IE + 1) ||
        (offset == IORegs::IME) || (offset == IORegs::IME + 1) ||
        (offset == IORegs::IE - 2) || (offset == IORegs::IME - 2)) {
      static int irqLog32 = 0;
      if (irqLog32 < 400) {
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Debug, "IRQ",
            "IRQ REG WRITE32 #%d offset=0x%03x val=0x%08x PC=0x%08x", irqLog32,
            (unsigned)offset, (unsigned)value,
            cpu ? (unsigned)cpu->GetRegister(15) : 0u);
        irqLog32++;
      }
    }
  }

  // EEPROM Handling - only for EEPROM-save cartridges.
  uint8_t region = (address >> 24);
  if (region == 0x0D && (!hasSRAM && !isFlash)) {
    WriteEEPROM(value & 0xFFFF);
    WriteEEPROM(value >> 16);
    return;
  }

  // SMA2 investigation: trace non-zero writes into the header staging buffer in
  // EWRAM. Enable with: AIO_TRACE_SMA2_STAGE_WRITES=1
  if (EnvFlagCached("AIO_TRACE_SMA2_STAGE_WRITES") && cpu) {
    if (address >= 0x020003C0u && address < 0x02000440u && value != 0) {
      static int stage32 = 0;
      if (stage32 < 500) {
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "SMA2",
            "STAGE W32 addr=0x%08x val=0x%08x PC=0x%08x", (unsigned)address,
            (unsigned)value, (unsigned)cpu->GetRegister(15));
        stage32++;
      }
    }
  }

  if (EnvFlagCached("AIO_TRACE_SMA2_SAVEVALID_WRITES") && cpu) {
    if (address >= 0x03007B80u && address < 0x03007C80u) {
      static int wlogs32 = 0;
      if (wlogs32 < 400) {
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "SMA2",
            "SAVEVALID W32 addr=0x%08x val=0x%08x PC=0x%08x", (unsigned)address,
            (unsigned)value, cpu ? (unsigned)cpu->GetRegister(15) : 0u);
        wlogs32++;
      }
    }
  }

  // Instrument key IO register writes to diagnose display/IRQ state
  if (TraceGbaSpam() && (address & 0xFF000000) == IORegs::BASE) {
    uint32_t offset = address & MemoryMap::IO_REG_MASK;
    switch (offset) {
    case IORegs::DISPCNT:
      std::cout << "[IO] DISPCNT write32: 0x" << std::hex << value << std::dec
                << std::endl;
      break;
    case IORegs::DISPSTAT:
      std::cout << "[IO] DISPSTAT write32: 0x" << std::hex << value << std::dec
                << std::endl;
      break;
    case IORegs::VCOUNT:
      std::cout << "[IO] VCOUNT write32: 0x" << std::hex << value << std::dec
                << std::endl;
      break;
    case IORegs::IE:
      std::cout << "[IO] IE write32: 0x" << std::hex << value << std::dec
                << std::endl;
      break;
    case IORegs::IF:
      std::cout << "[IO] IF write32: 0x" << std::hex << value << std::dec
                << std::endl;
      break;
    case IORegs::IME:
      std::cout << "[IO] IME write32: 0x" << std::hex << value << std::dec
                << std::endl;
      break;
    default:
      break;
    }
  }

  // Sound FIFO writes (FIFO_A = 0x40000A0, FIFO_B = 0x40000A4)
  if (address == 0x040000A0) {
    static int fifoACount = 0;
    fifoACount++;
    if (TraceGbaSpam() && (fifoACount <= 20 || (fifoACount % 10000 == 0))) {
      std::cout << "[FIFO_A Write #" << fifoACount << "] val=0x" << std::hex
                << value;
      if (cpu)
        std::cout << " PC=0x" << cpu->GetRegister(15) << " LR=0x"
                  << cpu->GetRegister(14);
      std::cout << std::dec << std::endl;
    }
    if (apu)
      apu->WriteFIFO_A(value);
    return;
  }
  if (address == 0x040000A4) {
    static int fifoBCount = 0;
    fifoBCount++;
    if (TraceGbaSpam() && (fifoBCount <= 10 || (fifoBCount % 10000 == 0))) {
      std::cout << "[FIFO_B Write #" << fifoBCount << "] val=0x" << std::hex
                << value;
      if (cpu)
        std::cout << " PC=0x" << cpu->GetRegister(15);
      std::cout << std::dec << std::endl;
    }
    if (apu)
      apu->WriteFIFO_B(value);
    return;
  }

  // For VRAM, Palette, OAM - write directly without 8-bit quirk.
  // Optionally align unaligned word stores to match HW behavior.
  if (region == 0x05 || region == 0x06 || region == 0x07) {
    address &= ~3u;
  }

  // Optional: trace CPU word writes into Palette RAM.
  // Enable with: AIO_TRACE_PALETTE_CPU_WRITES=1
  if (region == 0x05 && EnvFlagCached("AIO_TRACE_PALETTE_CPU_WRITES") && cpu) {
    static int palW32Logs = 0;
    if (palW32Logs < 2000) {
      const uint32_t pc2 = (uint32_t)cpu->GetRegister(15);
      const uint32_t lr2 = (uint32_t)cpu->GetRegister(14);
      AIO::Emulator::Common::Logger::Instance().LogFmt(
          AIO::Emulator::Common::LogLevel::Info, "PAL",
          "CPU W32 pal addr=0x%08x val=0x%08x PC=0x%08x LR=0x%08x",
          (unsigned)address, (unsigned)value, (unsigned)pc2, (unsigned)lr2);
      palW32Logs++;
    }
  }

  // IMPORTANT: Don't implement word writes via four Write8() calls for RAM.
  // Write8() has IRQ handler clamping logic and must not run on intermediate
  // byte states.
  if (region == 0x03 && IsIwramMappedAddress(address)) {
    const uint32_t off0 = (address + 0u) & 0x7FFFu;
    const uint32_t off1 = (address + 1u) & 0x7FFFu;
    const uint32_t off2 = (address + 2u) & 0x7FFFu;
    const uint32_t off3 = (address + 3u) & 0x7FFFu;
    if (off0 < wram_chip.size())
      wram_chip[off0] = (uint8_t)(value & 0xFFu);
    if (off1 < wram_chip.size())
      wram_chip[off1] = (uint8_t)((value >> 8) & 0xFFu);
    if (off2 < wram_chip.size())
      wram_chip[off2] = (uint8_t)((value >> 16) & 0xFFu);
    if (off3 < wram_chip.size())
      wram_chip[off3] = (uint8_t)((value >> 24) & 0xFFu);

    if (isIrqHandPtrWord) {
      ClampIrqHandlerWord();
    }

    if (traceIrqHandWrites && isIrqHandPtrWord) {
      const uint32_t newIrqHandWord = Read32(0x03007FFCu);
      if (newIrqHandWord != oldIrqHandWord) {
        static int logs = 0;
        if (logs++ < 300) {
          const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
          const uint32_t lr = cpu ? (uint32_t)cpu->GetRegister(14) : 0u;
          const uint32_t sp = cpu ? (uint32_t)cpu->GetRegister(13) : 0u;
          const uint32_t cpsr = cpu ? (uint32_t)cpu->GetCPSR() : 0u;
          const uint32_t r0 = cpu ? (uint32_t)cpu->GetRegister(0) : 0u;
          const uint32_t r1 = cpu ? (uint32_t)cpu->GetRegister(1) : 0u;
          const uint32_t r2 = cpu ? (uint32_t)cpu->GetRegister(2) : 0u;
          const uint32_t r3 = cpu ? (uint32_t)cpu->GetRegister(3) : 0u;
          const uint32_t r12 = cpu ? (uint32_t)cpu->GetRegister(12) : 0u;
          AIO::Emulator::Common::Logger::Instance().LogFmt(
              AIO::Emulator::Common::LogLevel::Info, "IRQHAND",
              "IRQHAND_WRITE32 addr=0x%08x val=0x%08x old=0x%08x new=0x%08x "
              "PC=0x%08x LR=0x%08x SP=0x%08x CPSR=0x%08x R0=0x%08x R1=0x%08x "
              "R2=0x%08x R3=0x%08x R12=0x%08x",
              (unsigned)address, (unsigned)value, (unsigned)oldIrqHandWord,
              (unsigned)newIrqHandWord, (unsigned)pc, (unsigned)lr,
              (unsigned)sp, (unsigned)cpsr, (unsigned)r0, (unsigned)r1,
              (unsigned)r2, (unsigned)r3, (unsigned)r12);
        }
      }
    }
    return;
  } else if (region == 0x02) {
    const uint32_t off0 = (address + 0u) & 0x3FFFFu;
    const uint32_t off1 = (address + 1u) & 0x3FFFFu;
    const uint32_t off2 = (address + 2u) & 0x3FFFFu;
    const uint32_t off3 = (address + 3u) & 0x3FFFFu;
    if (off0 < wram_board.size())
      wram_board[off0] = (uint8_t)(value & 0xFFu);
    if (off1 < wram_board.size())
      wram_board[off1] = (uint8_t)((value >> 8) & 0xFFu);
    if (off2 < wram_board.size())
      wram_board[off2] = (uint8_t)((value >> 16) & 0xFFu);
    if (off3 < wram_board.size())
      wram_board[off3] = (uint8_t)((value >> 24) & 0xFFu);
    return;
  }

  if (region == 0x05 || region == 0x06 || region == 0x07) {
    Write8Internal(address, value & 0xFF);
    Write8Internal(address + 1, (value >> 8) & 0xFF);
    Write8Internal(address + 2, (value >> 16) & 0xFF);
    Write8Internal(address + 3, (value >> 24) & 0xFF);
  } else {
    Write8(address, value & 0xFF);
    Write8(address + 1, (value >> 8) & 0xFF);
    Write8(address + 2, (value >> 16) & 0xFF);
    Write8(address + 3, (value >> 24) & 0xFF);
  }

  // Timer control via Write32
  if ((address & 0xFF000000) == 0x04000000) {
    uint32_t offset = address & 0x3FF;

    if (offset >= 0x100 && offset <= 0x10C) {
      int timerIdx = (offset - 0x100) / 4;
      uint16_t controlVal = (value >> 16) & 0xFFFF;

      uint16_t oldControl = io_regs[offset + 2] | (io_regs[offset + 3] << 8);
      bool wasEnabled = oldControl & 0x80;
      bool nowEnabled = controlVal & 0x80;

      if (!wasEnabled && nowEnabled) {
        uint16_t reload = value & 0xFFFF;
        timerCounters[timerIdx] = reload;
        timerPrescalerCounters[timerIdx] = 0;
      }
    }
  }

  if (traceIrqHandWrites && isIrqHandPtrWord) {
    const uint32_t newIrqHandWord = Read32(0x03007FFCu);
    if (newIrqHandWord != oldIrqHandWord) {
      static int logs = 0;
      if (logs++ < 300) {
        const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
        const uint32_t lr = cpu ? (uint32_t)cpu->GetRegister(14) : 0u;
        const uint32_t sp = cpu ? (uint32_t)cpu->GetRegister(13) : 0u;
        const uint32_t cpsr = cpu ? (uint32_t)cpu->GetCPSR() : 0u;
        const uint32_t r0 = cpu ? (uint32_t)cpu->GetRegister(0) : 0u;
        const uint32_t r1 = cpu ? (uint32_t)cpu->GetRegister(1) : 0u;
        const uint32_t r2 = cpu ? (uint32_t)cpu->GetRegister(2) : 0u;
        const uint32_t r3 = cpu ? (uint32_t)cpu->GetRegister(3) : 0u;
        const uint32_t r12 = cpu ? (uint32_t)cpu->GetRegister(12) : 0u;
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "IRQHAND",
            "IRQHAND_WRITE32 addr=0x%08x val=0x%08x old=0x%08x new=0x%08x "
            "PC=0x%08x LR=0x%08x SP=0x%08x CPSR=0x%08x R0=0x%08x R1=0x%08x "
            "R2=0x%08x R3=0x%08x R12=0x%08x",
            (unsigned)address, (unsigned)value, (unsigned)oldIrqHandWord,
            (unsigned)newIrqHandWord, (unsigned)pc, (unsigned)lr, (unsigned)sp,
            (unsigned)cpsr, (unsigned)r0, (unsigned)r1, (unsigned)r2,
            (unsigned)r3, (unsigned)r12);
      }
    }
  }

  if (isIrqHandPtrWord) {
    ClampIrqHandlerWord();
  }
}

void GBAMemory::SetPpuTimingState(int scanline, int cycleCounter) {
  ppuTimingValid = true;
  ppuTimingScanline = scanline;
  ppuTimingCycle = cycleCounter;
}

uint32_t GBAMemory::ReadIrqHandlerRaw() const {
  const uint32_t base = kIrqHandlerOffset;
  if (base + 3 >= wram_chip.size())
    return 0u;
  return (uint32_t)wram_chip[base] | ((uint32_t)wram_chip[base + 1] << 8) |
         ((uint32_t)wram_chip[base + 2] << 16) |
         ((uint32_t)wram_chip[base + 3] << 24);
}

void GBAMemory::WriteIrqHandlerRaw(uint32_t value) {
  const uint32_t base = kIrqHandlerOffset;
  if (base + 3 >= wram_chip.size())
    return;
  wram_chip[base] = (uint8_t)(value & 0xFFu);
  wram_chip[base + 1] = (uint8_t)((value >> 8) & 0xFFu);
  wram_chip[base + 2] = (uint8_t)((value >> 16) & 0xFFu);
  wram_chip[base + 3] = (uint8_t)((value >> 24) & 0xFFu);
}

void GBAMemory::ClampIrqHandlerWord() {
  const uint32_t raw = ReadIrqHandlerRaw();
  const bool inEepromRange = (raw >= 0x0D000000u && raw < 0x0E000000u);
  if (raw == 0u || inEepromRange || !IsValidIrqHandlerAddress(raw)) {
    WriteIrqHandlerRaw(kIrqHandlerDefault);
  }
}

void GBAMemory::WriteIORegisterInternal(uint32_t offset, uint16_t value) {
  if (offset + 1 < io_regs.size()) {
    io_regs[offset] = value & 0xFF;
    io_regs[offset + 1] = (value >> 8) & 0xFF;
  }
}

void GBAMemory::CheckDMA(int timing) {
  if (dmaInProgress) {
    return;
  }
  for (int i = 0; i < 4; ++i) {
    uint32_t baseOffset = IORegs::DMA0SAD + (i * IORegs::DMA_CHANNEL_SIZE);
    uint16_t control =
        io_regs[baseOffset + 10] | (io_regs[baseOffset + 11] << 8);

    if (control & DMAControl::ENABLE) {
      int dmaTiming = (control >> 12) & 3;
      if (dmaTiming == timing) {
        PerformDMA(i);
      }
    }
  }
}

void GBAMemory::PerformDMA(int channel) {
  static int dmaSeq = 0;
  static bool inImmediateDMA = false; // Only guard immediate DMAs
  dmaSeq++;

  if (dmaInProgress) {
    return;
  }
  dmaInProgress = true;
  struct DMAGuard {
    bool &flag;
    ~DMAGuard() { flag = false; }
  } guard{dmaInProgress};

  // DEBUG: Log first 10 DMA calls unconditionally
  static int dmaDebugCount = 0;
  if (dmaDebugCount < 10) {
    std::ofstream df("/tmp/gba_dma_debug.txt", std::ios::app);
    df << "[DMA#" << dmaSeq << " ch" << channel << " ENTER]" << std::endl;
    df.close();
    dmaDebugCount++;
  }

  uint32_t baseOffset = IORegs::DMA0SAD + (channel * IORegs::DMA_CHANNEL_SIZE);

  // CNT_H (Control) - 16 bit
  uint16_t control = io_regs[baseOffset + 10] | (io_regs[baseOffset + 11] << 8);

  // Decode timing first
  int timing = (control & DMAControl::START_TIMING_MASK) >> 12;

  // Only guard immediate timing (timing=0) DMAs from recursion.
  // Note: dmaInProgress already prevents nested DMAs across all timings.
  if (timing == 0 && inImmediateDMA) {
    return;
  }

  bool wasInImmediate = inImmediateDMA;
  if (timing == 0) {
    inImmediateDMA = true;
  }

  // CNT_L (Count) - 16 bit
  uint32_t count = io_regs[baseOffset + 8] | (io_regs[baseOffset + 9] << 8);
  bool repeat = (control >> 9) & 1;

  // Decode Control
  bool is32Bit = (control & DMAControl::TRANSFER_32BIT) != 0;
  int destCtrl = (control & DMAControl::DEST_ADDR_CONTROL_MASK) >> 5;
  int srcCtrl = (control & DMAControl::SRC_ADDR_CONTROL_MASK) >> 7;

  uint32_t dst = dmaInternalDst[channel];
  uint32_t src = dmaInternalSrc[channel];

  // DEBUG: Trace DMA targeting IWRAM
  if (verboseLogs && (dst & 0xFF000000) == 0x03000000) {
    std::cout << "[DMA#" << std::dec << dmaSeq << " ch" << channel
              << " to IWRAM] "
              << "Dst=0x" << std::hex << dst << " Src=0x" << src
              << " Count=" << std::dec << count << " 32bit=" << is32Bit
              << " srcCtrl=" << srcCtrl << " destCtrl=" << destCtrl
              << " timing=" << timing << " PC=0x" << std::hex;
    if (cpu)
      std::cout << cpu->GetRegister(15);
    std::cout << std::dec << std::endl;
  }

  // DEBUG: Trace DMA targeting VRAM (disabled)
  // if ((dst & 0xFF000000) == 0x06000000 && (dst & 0xFFFF0000) == 0x06000000) {
  //     uint32_t srcVal = is32Bit ? Read32(src) : Read16(src);
  //     std::cout << "[DMA#" << std::dec << dmaSeq << " ch" << channel << " to
  //     BG_VRAM] Src=0x" << std::hex << src
  //               << " Dst=0x" << dst
  //               << " Count=" << std::dec << count
  //               << " 32bit=" << is32Bit
  //               << " srcCtrl=" << srcCtrl
  //               << " srcVal=0x" << std::hex << srcVal << std::dec <<
  //               std::endl;
  // }

  bool irq = (control >> 14) & 1;

  uint32_t currentSrc = dmaInternalSrc[channel];
  uint32_t currentDst = dmaInternalDst[channel];

  const bool traceDmaPalette = EnvFlagCached("AIO_TRACE_DMA_PALETTE");
  const bool dstIsPalette =
      (currentDst >= 0x05000000u && currentDst < 0x05000400u);
  if (traceDmaPalette && dstIsPalette) {
    static int dmaPalStartLogs = 0;
    if (dmaPalStartLogs < 64) {
      const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
      AIO::Emulator::Common::Logger::Instance().LogFmt(
          AIO::Emulator::Common::LogLevel::Info, "DMA/PALETTE",
          "DMA start ch=%d src=0x%08x dst=0x%08x count=%u width=%u srcCtrl=%d "
          "dstCtrl=%d timing=%d ctrl=0x%04x PC=0x%08x",
          channel, (unsigned)currentSrc, (unsigned)currentDst, (unsigned)count,
          (unsigned)(is32Bit ? 32 : 16), srcCtrl, destCtrl, timing,
          (unsigned)control, (unsigned)pc);
      dmaPalStartLogs++;
    }
  }

  // EEPROM Size Detection via DMA Count
  // 4Kbit EEPROM uses 6-bit address -> 9 bits total (2 cmd + 6 addr + 1 stop)
  // 64Kbit EEPROM uses 14-bit address -> 17 bits total (2 cmd + 14 addr + 1
  // stop) Games use DMA to bit-bang these requests.
  if (currentDst >= 0x0D000000 && currentDst <= 0x0DFFFFFF) {
    if (!saveTypeLocked) {
      if (count == 9) {
        if (eepromIs64Kbit) {
          eepromIs64Kbit = false;
          if (eepromData.size() != 512) {
            eepromData.resize(512, 0xFF);
          }
        }
      } else if (count == 17) {
        if (!eepromIs64Kbit) {
          eepromIs64Kbit = true;
          if (eepromData.size() < 8192) {
            eepromData.resize(8192, 0xFF);
          }
        }
      }
    } else {
    }
  }

  if (currentSrc >= 0x0D000000 && currentSrc < 0x0E000000) {
  }

  if (currentDst >= 0x0D000000 && currentDst < 0x0E000000) {
  }

  // EEPROM instrumentation: log typical EEPROM-related DMA3 bursts
  if (verboseLogs && channel == 3) {
    bool srcIsEEPROM = (currentSrc >= 0x0D000000 && currentSrc < 0x0E000000);
    bool dstIsEEPROM = (currentDst >= 0x0D000000 && currentDst < 0x0E000000);
    if (srcIsEEPROM || dstIsEEPROM) {
      std::cout << "[DMA3 START] Src=0x" << std::hex << currentSrc << " Dst=0x"
                << currentDst << " Count=0x" << count << " Ctrl=0x" << control
                << " timing=" << std::dec << ((control >> 12) & 3)
                << " repeat=" << ((control >> 9) & 1) << " width="
                << (((control & DMAControl::TRANSFER_32BIT) != 0) ? 32 : 16)
                << " PC=0x" << std::hex << (cpu ? cpu->GetRegister(15) : 0)
                << std::dec << std::endl;
    }
    if (srcIsEEPROM && count >= 68 &&
        (control & DMAControl::TRANSFER_32BIT) == 0) {
      // eepromAddress is already a block number (not a byte offset)
      // std::cout << " First16=";
      // for (int i = 0; i < 16 && (base + i) < (int)eepromData.size(); ++i) {
      //     std::cout << std::hex << (int)eepromData[base + i];
      // }
      // std::cout << std::dec << std::endl;
    }
  }

  // For sound DMA (timing mode 3), always transfer 4 words (16 bytes)
  if (timing == 3) {
    count = 4;
    is32Bit = true;
  }

  // DMA count register sizes differ:
  // - DMA0, DMA1, DMA2: 14-bit count (max 0x4000)
  // - DMA3: 16-bit count (max 0x10000)
  if (channel < 3) {
    count &= 0x3FFF; // Mask to 14 bits for DMA0-2
  }

  if (count == 0) {
    count = (channel == 3) ? 0x10000 : 0x4000;
  }

  // GBA DMA aligns addresses to transfer width:
  // - 16-bit DMA ignores bit0 (halfword aligned)
  // - 32-bit DMA ignores bit0-1 (word aligned)
  // If we don't do this, games that program odd DMA addresses can end up with
  // scrambled tile/font data (common symptom: corrupted glyphs).
  {
    const uint32_t mask = is32Bit ? ~3u : ~1u;
    currentSrc &= mask;
    currentDst &= mask;
    // Keep these in sync for any later debug/hack checks that compare the
    // initial dst.
    src &= mask;
    dst &= mask;
  }

  // DEBUG: Post-mask trace for DMA to 0x3001500
  if ((currentDst & 0x7FFF) == 0x1500 && (currentDst >> 24) == 0x03) {
    uint32_t currentVal = Read32(0x3001500);
    uint32_t firstVal = Read32(currentSrc);
    std::cout << "[DMA#" << std::dec << dmaSeq << " ch" << channel
              << " EXECUTE 0x3001500] "
              << "Count(masked)=" << count << " 32bit=" << is32Bit
              << " FirstSrcValue=0x" << std::hex << firstVal
              << " destCtrl=" << std::dec << destCtrl << " BEFORE=0x"
              << std::hex << currentVal << std::endl;
  }

  // WORKAROUND: DKC sound engine sets destCtrl=2 (Fixed) for DMA to IWRAM.
  // With Fixed destination, all values write to the same address repeatedly.
  // For large counts, this is audio streaming - we skip the actual writes
  // to avoid corrupting whatever value was there before.
  // The game expects the pre-existing value at the destination to remain.
  bool dstIsIWRAM = (currentDst >> 24) == 0x03;
  const bool isDKC = (gameCode == "ADKE" || gameCode == "ADKP" ||
                      gameCode == "ADKJ" || gameCode == "ADKK");
  const bool allowFixedIWRAMSkip =
      EnvFlagCached("AIO_DKC_DMA_FIXED_IWRAM_SKIP") || isDKC;
  if (allowFixedIWRAMSkip && destCtrl == 2 && dstIsIWRAM && count > 100) {
    if (TraceGbaSpam() || verboseLogs) {
      std::cout << "[DMA SKIP] destCtrl=2 IWRAM 0x" << std::hex << currentDst
                << " count=" << std::dec << count
                << " (Fixed dest - preserving existing value)" << std::endl;
    }
    // Update timing as if full DMA happened
    int step = is32Bit ? 4 : 2;
    int totalCycles = 2 + count * (is32Bit ? 4 : 2);
    if (srcCtrl == 0)
      currentSrc += count * step;
    else if (srcCtrl == 1)
      currentSrc -= count * step;
    dmaInternalSrc[channel] = currentSrc;
    lastDMACycles += totalCycles;
    UpdateTimers(totalCycles);
    if (apu)
      apu->Update(totalCycles);
    if (ppu)
      ppu->Update(totalCycles);
    inImmediateDMA = wasInImmediate;
    io_regs[baseOffset + 10] &= ~(DMAControl::ENABLE & 0xFF);
    io_regs[baseOffset + 11] &= ~(DMAControl::ENABLE >> 8);
    return;
  }

  int step = is32Bit ? 4 : 2;
  int totalCycles = 2; // DMA Setup Overhead (approx)

  // EEPROM DMA Read Support
  // Games use DMA to clock EEPROM reads bit-by-bit via the serial interface.
  // OPTIMIZATION: For EEPROM reads, instantly complete the state machine to
  // avoid slow bit-by-bit protocol (which would require 64+ DMA transfers per
  // block)
  bool srcIsEEPROM = (currentSrc >= 0x0D000000 && currentSrc < 0x0E000000);
  bool dstIsEEPROM = (currentDst >= 0x0D000000 && currentDst < 0x0E000000);

  if (EnvFlagCached("AIO_TRACE_EEPROM_DMA") && (srcIsEEPROM || dstIsEEPROM)) {
    static int eepDmaLogs = 0;
    if (eepDmaLogs < 200) {
      AIO::Emulator::Common::Logger::Instance().LogFmt(
          AIO::Emulator::Common::LogLevel::Info, "EEPROM_DMA",
          "DMA ch=%d src=0x%08x dst=0x%08x count=%u width=%u timing=%u "
          "PC=0x%08x",
          channel, (unsigned)currentSrc, (unsigned)currentDst, (unsigned)count,
          (unsigned)(is32Bit ? 32 : 16), (unsigned)timing,
          cpu ? (unsigned)cpu->GetRegister(15) : 0u);
      eepDmaLogs++;
    }
  }

  // SMA2 investigation: confirm whether the game performs an EEPROM->EWRAM DMA
  // read into its header staging buffer before validating. Enable with:
  // AIO_TRACE_SMA2_EEPROM_READ=1
  if (EnvFlagCached("AIO_TRACE_SMA2_EEPROM_READ") && srcIsEEPROM) {
    if (currentDst >= 0x020003C0 && currentDst < 0x02000400) {
      static int logs = 0;
      if (logs < 40) {
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "SMA2",
            "EEPROM DMA READ: ch=%d src=0x%08x dst=0x%08x count=%u ctrl=0x%04x "
            "PC=0x%08x",
            channel, (unsigned)currentSrc, (unsigned)currentDst,
            (unsigned)count, (unsigned)control,
            cpu ? (unsigned)cpu->GetRegister(15) : 0u);
        logs++;
      }
    }
  }

  // SMA2 investigation: correlate EEPROM DMA reads with CPU SP to detect stack
  // overlap. Enable with: AIO_TRACE_SMA2_DMA_STACK=1
  if (EnvFlagCached("AIO_TRACE_SMA2_DMA_STACK") && cpu && channel == 3 &&
      srcIsEEPROM && !is32Bit) {
    if (count == 68 || count == 64 || count == 66) {
      const uint32_t pc = (uint32_t)cpu->GetRegister(15);
      const uint32_t sp = (uint32_t)cpu->GetRegister(13);
      const uint32_t lr = (uint32_t)cpu->GetRegister(14);
      const uint32_t dmaBytes = count * 2;
      const uint32_t dstStart = currentDst;
      const uint32_t dstEnd = dstStart + dmaBytes;

      // Heuristic: treat "near stack" as within +/- 0x200 bytes of SP.
      const uint32_t spLo = (sp >= 0x200u) ? (sp - 0x200u) : 0u;
      const uint32_t spHi = sp + 0x200u;
      const bool overlaps = !(dstEnd <= spLo || dstStart >= spHi);

      static int stackLogs = 0;
      if (stackLogs < 200) {
        AIO::Emulator::Common::Logger::Instance().LogFmt(
            AIO::Emulator::Common::LogLevel::Info, "SMA2",
            "EEPROM DMA_STACK ch=%d count=%u dst=0x%08x..0x%08x SP=0x%08x "
            "LR=0x%08x overlap=%u PC=0x%08x",
            channel, (unsigned)count, (unsigned)dstStart, (unsigned)dstEnd,
            (unsigned)sp, (unsigned)lr, (unsigned)(overlaps ? 1 : 0),
            (unsigned)pc);
        stackLogs++;
      }
    }
  }

  // Debug: Log EEPROM check
  // if (srcIsEEPROM || dstIsEEPROM) {
  //     std::cout << "[EEPROM DMA CHECK] src=0x" << std::hex << currentSrc
  //               << " dst=0x" << currentDst
  //               << " count=" << std::dec << count
  //               << " srcIsEEPROM=" << srcIsEEPROM
  //               << " dstIsEEPROM=" << dstIsEEPROM
  //               << " count>=68=" << (count >= 68) << std::endl;
  // }

  // DEBUG (guarded): Log when we check fast-path condition
  if (verboseLogs && (srcIsEEPROM || dstIsEEPROM)) {
    std::ofstream debugFile("/tmp/eeprom_debug.txt", std::ios::app);
    debugFile << "[DMA CHECK] srcEEP=" << srcIsEEPROM
              << " dstEEP=" << dstIsEEPROM << " count=" << count << " >= 68? "
              << (count >= 68) << std::endl;
    debugFile.close();
  }

  // Fast-path for EEPROM reads - only if buffer already prepared AND validated
  bool startingAtDataPhase = (eepromState == EEPROMState::ReadData);
  bool inReadSequence = (eepromState == EEPROMState::ReadDummy ||
                         eepromState == EEPROMState::ReadData);
  // CRITICAL: Only fast-path if buffer is valid for THIS transaction (set after
  // address+stop bit)
  const bool disableFastPath = EnvFlagCached("AIO_EEPROM_DISABLE_FASTPATH");
  bool canFastPath = !disableFastPath && srcIsEEPROM && inReadSequence &&
                     eepromBufferValid && count >= 4;

  // Fast-path validation (silent)

  if (canFastPath) {
    const bool lsbFirst = EnvFlagCached("AIO_EEPROM_LSB_FIRST");
    const bool dummyHigh = EnvFlagCached("AIO_EEPROM_DUMMY_HIGH");
    if (verboseLogs) {
      std::cout << "[EEPROM FAST-PATH] Activating for count=" << count
                << " src=0x" << std::hex << currentSrc << std::dec << std::endl;
    }

    const EEPROMState startState = eepromState;
    const int startBitCounter = eepromBitCounter;

    // Save initial destination for logging
    uint32_t initialDst = currentDst;

    // Preserve current read-phase progress.
    // Some games consume some dummy/data bits via CPU reads before switching to
    // DMA; resetting here would shift the stream and corrupt the reconstructed
    // payload.

    // Return all bits - the EEPROM only drives D0.
    // Default behavior models a pulled-up bus (0xFFFE/0xFFFF). Some titles
    // appear to treat the sampled halfword as a literal 0/1 value; allow a
    // D0-only mode for DMA behind an env var so we can validate behavior
    // without hard-coding game hacks.
    const bool d0OnlyDMASamples = EnvFlagCached("AIO_EEPROM_DMA_D0_ONLY");
    uint64_t debugBits = 0;
    for (uint32_t i = 0; i < count; ++i) {
      uint16_t word;
      if (eepromState == EEPROMState::ReadDummy) {
        if (d0OnlyDMASamples) {
          word = 0x0000;
        } else {
          word = dummyHigh ? EEPROMConsts::READY_HIGH : EEPROMConsts::BUSY_LOW;
        }
        eepromBitCounter++;
        if (eepromBitCounter >= EEPROMConsts::DUMMY_BITS) {
          eepromState = EEPROMState::ReadData;
          eepromBitCounter = 0;
        }
      } else { // ReadData
        int bitIndex = lsbFirst
                           ? eepromBitCounter
                           : ((EEPROMConsts::DATA_BITS - 1) - eepromBitCounter);
        const uint16_t d0 = (eepromBuffer >> bitIndex) & 1;
        word = d0OnlyDMASamples ? (uint16_t)(d0 & 1)
                                : (uint16_t)(EEPROMConsts::BUSY_LOW | d0);
        if (i >= EEPROMConsts::DUMMY_BITS &&
            i < (EEPROMConsts::DUMMY_BITS + EEPROMConsts::DATA_BITS)) {
          if (lsbFirst) {
            const uint32_t dataBitPos = i - EEPROMConsts::DUMMY_BITS;
            debugBits |= ((uint64_t)(d0 & 1) << dataBitPos);
          } else {
            debugBits = (debugBits << 1) | (uint64_t)(d0 & 1);
          }
        }
        eepromBitCounter++;
        if (eepromBitCounter >= EEPROMConsts::DATA_BITS) {
          eepromState = EEPROMState::Idle;
          eepromBitCounter = 0;
          eepromBufferValid = false; // Invalidate buffer after read completes
        }
      }

      // Each DMA transfer receives a single bit value (0 or 1) as a 16-bit word
      // The game's code will shift and accumulate these bits
      // Direct write to target memory (WRAM/EWRAM) without invoking full
      // Write16 cost
      uint8_t dstRegion = currentDst >> 24;
      if (dstRegion == 0x02) {
        uint32_t off = currentDst & MemoryMap::WRAM_BOARD_MASK;
        if (off + 1 < wram_board.size()) {
          wram_board[off] = word & 0xFF;
          wram_board[off + 1] = (word >> 8) & 0xFF;
        }
      } else if (dstRegion == 0x03) {
        uint32_t off = currentDst & MemoryMap::WRAM_CHIP_MASK;
        if (off + 1 < wram_chip.size()) {
          wram_chip[off] = word & 0xFF;
          wram_chip[off + 1] = (word >> 8) & 0xFF;
        }
      } else {
        Write16(currentDst, word);
      }

      // Update destination address
      if (destCtrl == 0 || destCtrl == 3) {
        currentDst += 2;
      } else if (destCtrl == 1) {
        currentDst -= 2;
      }

      // Update source address (even for EEPROM reads, hardware updates SAD
      // based on srcCtrl)
      if (srcCtrl == 0 || srcCtrl == 3) {
        currentSrc += 2;
      } else if (srcCtrl == 1) {
        currentSrc -= 2;
      }
    }
    totalCycles += count * 2;

    // DEBUG: Log what bits were transferred
    if (verboseLogs &&
        eepromAddress == 2) { // Only log block 2 (the header block)
      std::cout << "[EEPROM FAST-PATH] Block 2 read: debugBits=0x" << std::hex
                << debugBits << std::dec << std::endl;
    }

    // SMA2 investigation: dump the *actual halfwords* written by the DMA read.
    // This is useful because games sometimes validate the raw 16-bit words (not
    // just D0). Enable with: AIO_TRACE_SMA2_EEPROM_DMAWORDS=1
    if (EnvFlagCached("AIO_TRACE_SMA2_EEPROM_DMAWORDS") && cpu) {
      if ((eepromAddress == 2 || eepromAddress == 4) && !is32Bit &&
          count == 68) {
        static int dmaWordDumps = 0;
        if (dmaWordDumps < 40) {
          const uint32_t dstStart = initialDst;
          const uint8_t dstRegion = dstStart >> 24;
          std::ostringstream oss;
          oss << "EEPROM DMAWORDS: block=" << std::dec
              << (unsigned)eepromAddress << " dst=0x" << std::hex
              << std::setw(8) << std::setfill('0') << dstStart
              << " count=" << std::dec << (unsigned)count
              << " bus0=" << std::hex << std::setw(4) << std::setfill('0')
              << (unsigned)(d0OnlyDMASamples ? 0x0000 : EEPROMConsts::BUSY_LOW)
              << " PC=0x" << std::hex << std::setw(8) << std::setfill('0')
              << (unsigned)cpu->GetRegister(15) << " words=";

          auto readWordAt = [&](uint32_t addr) -> uint16_t {
            const uint8_t region = addr >> 24;
            if (region == 0x02) {
              const uint32_t off = addr & MemoryMap::WRAM_BOARD_MASK;
              if (off + 1 < wram_board.size()) {
                return (uint16_t)(wram_board[off] | (wram_board[off + 1] << 8));
              }
              return 0;
            }
            if (region == 0x03) {
              const uint32_t off = addr & MemoryMap::WRAM_CHIP_MASK;
              if (off + 1 < wram_chip.size()) {
                return (uint16_t)(wram_chip[off] | (wram_chip[off + 1] << 8));
              }
              return 0;
            }
            // Fallback (shouldn't happen for SMA2): use bus read.
            return Read16(addr);
          };

          // Dump the first 24 halfwords (4 dummy + first 20 data bits) – enough
          // to see the word-shape without spamming logs.
          const uint32_t dumpN = (count < 24) ? count : 24;
          for (uint32_t i = 0; i < dumpN; ++i) {
            const uint16_t w = readWordAt(dstStart + i * 2);
            if (i == 0) {
              oss << std::hex;
            }
            oss << ((i == 0) ? "" : ",") << std::setw(4) << std::setfill('0')
                << (unsigned)w;
          }

          AIO::Emulator::Common::Logger::Instance().LogFmt(
              AIO::Emulator::Common::LogLevel::Info, "SMA2", "%s",
              oss.str().c_str());

          dmaWordDumps++;
        }
      }
    }

    // SMA2 investigation: always log the reconstructed 64 data bits for key
    // blocks. Enable with: AIO_TRACE_SMA2_EEPROM_READBITS=1
    if (EnvFlagCached("AIO_TRACE_SMA2_EEPROM_READBITS") && cpu) {
      if (eepromAddress == 2 || eepromAddress == 4) {
        static int readBitsLogs = 0;
        if (readBitsLogs < 80) {
          AIO::Emulator::Common::Logger::Instance().LogFmt(
              AIO::Emulator::Common::LogLevel::Info, "SMA2",
              "EEPROM READBITS: block=%u dst=0x%08x count=%u startState=%d "
              "startBit=%d bufValidStart=%u buffer=0x%016llx "
              "dataBits=0x%016llx PC=0x%08x",
              (unsigned)eepromAddress, (unsigned)initialDst, (unsigned)count,
              (int)startState, (int)startBitCounter,
              (unsigned)(eepromBufferValid ? 1 : 0),
              (unsigned long long)eepromBuffer, (unsigned long long)debugBits,
              (unsigned)cpu->GetRegister(15));
          readBitsLogs++;
        }
      }
    }

    // Targeted correctness check for SMA2 save validation.
    // Confirms that the 64 data bits the game receives via DMA match the EEPROM
    // buffer we prepared.
    if (verboseLogs && (gameCode == "AMQE" || gameCode == "AMQP" ||
                        gameCode == "AMQJ" || gameCode == "AA2E")) {
      if (eepromAddress == 2 || eepromAddress == 4 || eepromAddress == 32) {
        static int sma2FastChecksLogged = 0;
        if (sma2FastChecksLogged < 40) {
          std::cerr << "[EEPROM FASTCHK] game=" << gameCode
                    << " block=" << eepromAddress << " count=" << count
                    << " startState=" << (int)startState
                    << " startBit=" << startBitCounter
                    << " bufValid=" << (eepromBufferValid ? 1 : 0)
                    << " buffer=0x" << std::hex << std::setw(16)
                    << std::setfill('0') << eepromBuffer << " dataBits=0x"
                    << std::setw(16) << debugBits << std::dec << std::endl;
          sma2FastChecksLogged++;
        }
      }
    }

    // DEBUG (guarded): Log fast-path completion
    if (verboseLogs) {
      std::ofstream debugFile("/tmp/eeprom_debug.txt", std::ios::app);
      debugFile << "[FAST-PATH] Block=" << eepromAddress << " Count=" << count
                << " Buffer=0x" << std::hex << eepromBuffer << std::dec
                << std::endl;
      debugFile << "  Dst=0x" << std::hex << initialDst << " First 16 words:";
      uint32_t dumpOffset = initialDst - 0x03000000;
      for (int i = 0; i < 16 && i < (int)count &&
                      dumpOffset + i * 2 + 1 < wram_chip.size();
           ++i) {
        uint16_t word = wram_chip[dumpOffset + i * 2] |
                        (wram_chip[dumpOffset + i * 2 + 1] << 8);
        if (i % 8 == 0)
          debugFile << std::endl << "    ";
        debugFile << std::setw(4) << std::setfill('0') << word << " ";
      }
      debugFile << std::dec << std::endl;
      debugFile.close();
    }

    if (verboseLogs) {
      std::cout << "[EEPROM FAST-PATH] Complete - returned to Idle state"
                << std::endl;
    }
  }
  // Fast-path for EEPROM writes
  else if (dstIsEEPROM && count > 1) {
    // Process all writes instantly
    for (uint32_t i = 0; i < count; ++i) {
      uint16_t val = Read16(currentSrc);
      WriteEEPROM(val);
      if (srcCtrl == 0)
        currentSrc += 2;
      else if (srcCtrl == 1)
        currentSrc -= 2;
    }
    totalCycles += count * 2;
  }
  // Normal DMA path for non-EEPROM transfers
  else {
    // Handle EEPROM writes and reads separately
    if (srcIsEEPROM) {
      // Reading from EEPROM - call ReadEEPROM for each word
      for (uint32_t i = 0; i < count; ++i) {
        uint16_t val = ReadEEPROM();

        // Log if writing EEPROM data to critical regions
        if (currentDst >= 0x05000000 && currentDst < 0x05000400) {
          std::cerr << "[EEPROM->PALETTE] Writing 0x" << std::hex << val
                    << " to 0x" << currentDst << std::dec << std::endl;
        } else if (currentDst >= 0x06000000 && currentDst < 0x06018000) {
          std::cerr << "[EEPROM->VRAM] Writing 0x" << std::hex << val
                    << " to 0x" << currentDst << std::dec << std::endl;
        } else if (currentDst >= 0x07000000 && currentDst < 0x07000400) {
          std::cerr << "[EEPROM->OAM] Writing 0x" << std::hex << val << " to 0x"
                    << currentDst << std::dec << std::endl;
        }

        Write16(currentDst, val);
        totalCycles += 2;
        if (destCtrl == 0 || destCtrl == 3)
          currentDst += 2;
        else if (destCtrl == 1)
          currentDst -= 2;
      }
    } else if (dstIsEEPROM) {
      // Writing to EEPROM - call WriteEEPROM for each word
      for (uint32_t i = 0; i < count; ++i) {
        uint16_t val = Read16(currentSrc);
        WriteEEPROM(val);
        totalCycles += 2;
        if (srcCtrl == 0)
          currentSrc += 2;
        else if (srcCtrl == 1)
          currentSrc -= 2;
      }
    } else {
      // Normal memory-to-memory DMA
      for (uint32_t i = 0; i < count; ++i) {
        if (is32Bit) {
          uint32_t val = Read32(currentSrc);
          Write32(currentDst, val);

          if (traceDmaPalette &&
              (currentDst >= 0x05000000u && currentDst < 0x05000400u)) {
            static int dmaPalWordLogs = 0;
            if (dmaPalWordLogs < 256) {
              const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
              AIO::Emulator::Common::Logger::Instance().LogFmt(
                  AIO::Emulator::Common::LogLevel::Info, "DMA/PALETTE",
                  "W32 dst=0x%08x val=0x%08x src=0x%08x i=%u PC=0x%08x",
                  (unsigned)currentDst, (unsigned)val, (unsigned)currentSrc,
                  (unsigned)i, (unsigned)pc);
              dmaPalWordLogs++;
            }
          }

          totalCycles += 4;
        } else {
          uint16_t val = Read16(currentSrc);
          Write16(currentDst, val);

          if (traceDmaPalette &&
              (currentDst >= 0x05000000u && currentDst < 0x05000400u)) {
            static int dmaPalHalfLogs = 0;
            if (dmaPalHalfLogs < 512) {
              const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
              AIO::Emulator::Common::Logger::Instance().LogFmt(
                  AIO::Emulator::Common::LogLevel::Info, "DMA/PALETTE",
                  "W16 dst=0x%08x val=0x%04x src=0x%08x i=%u PC=0x%08x",
                  (unsigned)currentDst, (unsigned)val, (unsigned)currentSrc,
                  (unsigned)i, (unsigned)pc);
              dmaPalHalfLogs++;
            }
          }

          totalCycles += 2;
        }

        // Advance source each unit (GBATEK: 0=inc, 1=dec, 2=fixed,
        // 3=prohibited)
        if (srcCtrl == 0 || srcCtrl == 3) {
          currentSrc += step;
        } else if (srcCtrl == 1) {
          currentSrc -= step;
        }

        // Advance destination each unit unless FIFO/special timing fixes it
        if (timing != 3) {
          if (destCtrl == 0 || destCtrl == 3) {
            currentDst += step;
          } else if (destCtrl == 1) {
            currentDst -= step;
          }
          // Fixed (2) -> No change
        }
      }
    }
  }

  // Update system state to reflect DMA duration
  // This is crucial for games that check timers during DMA or expect delays
  lastDMACycles += totalCycles;
  UpdateTimers(totalCycles);
  if (apu)
    apu->Update(totalCycles);
  if (ppu)
    ppu->Update(totalCycles);

  // Save updated internal addresses
  dmaInternalSrc[channel] = currentSrc;
  // For repeat DMA with destCtrl=3 (Inc/Reload), reload destination
  if (repeat && destCtrl == 3) {
    // Reload destination from IO regs
    dmaInternalDst[channel] =
        io_regs[baseOffset + 4] | (io_regs[baseOffset + 5] << 8) |
        (io_regs[baseOffset + 6] << 16) | (io_regs[baseOffset + 7] << 24);
  } else {
    dmaInternalDst[channel] = currentDst;
  }

  // DMA completion behavior (GBATEK):
  // - Immediate DMA runs once and then clears the enable bit.
  // - VBlank/HBlank/Special timing DMAs clear enable if repeat=0; otherwise
  // they stay armed.
  if (timing == 0 || !repeat) {
    uint16_t ctrlNow =
        io_regs[baseOffset + 10] | (io_regs[baseOffset + 11] << 8);
    ctrlNow &= ~DMAControl::ENABLE;
    io_regs[baseOffset + 10] = (uint8_t)(ctrlNow & 0xFF);
    io_regs[baseOffset + 11] = (uint8_t)((ctrlNow >> 8) & 0xFF);
  }

  // Trigger IRQ
  if (irq) {
    uint16_t if_reg = io_regs[0x202] | (io_regs[0x203] << 8);
    if_reg |=
        (1 << (8 + channel)); // DMA0=Bit8, DMA1=Bit9, DMA2=Bit10, DMA3=Bit11
    io_regs[0x202] = if_reg & 0xFF;
    io_regs[0x203] = (if_reg >> 8) & 0xFF;
  }

  // Hardware-visible DMA register updates:
  // For normal memory-to-memory DMAs, reflecting final addresses can be useful.
  // However, for EEPROM transfers (0x0Dxxxxxx), some libraries assume the
  // programmed DMAxSAD/DAD remain stable and may use partial writes; writing
  // back advanced addresses can desync subsequent DMA setup.
  if (!srcIsEEPROM && !dstIsEEPROM) {
    auto writeIo32 = [&](uint32_t off, uint32_t v) {
      if (off + 3 >= io_regs.size())
        return;
      io_regs[off + 0] = (uint8_t)(v & 0xFF);
      io_regs[off + 1] = (uint8_t)((v >> 8) & 0xFF);
      io_regs[off + 2] = (uint8_t)((v >> 16) & 0xFF);
      io_regs[off + 3] = (uint8_t)((v >> 24) & 0xFF);
    };
    writeIo32(baseOffset + 0, currentSrc);
    writeIo32(baseOffset + 4, currentDst);
  }

  // Per GBA spec: Immediate timing always clears Enable bit after first
  // transfer, regardless of Repeat bit. Repeat only applies to
  // VBlank/HBlank/FIFO triggered DMAs.
  if (timing == 0 || !repeat) {
    // Immediate: always clear
    // Other timing: only clear if not repeating
    io_regs[baseOffset + 11] &= 0x7F; // Clear Bit 15 of CNT_H (High byte)

    // Also clear CNT_L to 0 on completion. This matches what many games expect
    // when polling DMA completion via the count register.
    if (baseOffset + 9 < io_regs.size()) {
      io_regs[baseOffset + 8] = 0;
      io_regs[baseOffset + 9] = 0;
    }
  }

  inImmediateDMA = wasInImmediate;
}

void GBAMemory::UpdateTimers(int cycles) {
  if (eepromWriteDelay > 0) {
    eepromWriteDelay -= cycles;
    if (eepromWriteDelay < 0)
      eepromWriteDelay = 0;
  }

  int previousOverflows = 0;

  for (int i = 0; i < 4; ++i) {
    uint32_t baseOffset = IORegs::TM0CNT_L + (i * IORegs::TIMER_CHANNEL_SIZE);
    uint16_t control = io_regs[baseOffset + 2] | (io_regs[baseOffset + 3] << 8);

    if (control & TimerControl::ENABLE) { // Timer Enabled

      int increments = 0;
      if (control & TimerControl::COUNT_UP) { // Count-Up (Cascade)
        increments = previousOverflows;
      } else {
        // Prescaler
        const int prescaler = control & TimerControl::PRESCALER_MASK;
        int threshold = 1;
        switch (prescaler) {
        case 0:
          threshold = 1;
          break; // F/1
        case 1:
          threshold = 64;
          break; // F/64
        case 2:
          threshold = 256;
          break; // F/256
        case 3:
          threshold = 1024;
          break; // F/1024
        }

        timerPrescalerCounters[i] += cycles;
        if (timerPrescalerCounters[i] >= threshold) {
          increments = timerPrescalerCounters[i] / threshold;
          timerPrescalerCounters[i] %= threshold;
        }
      }

      int overflowCount = 0;
      if (increments > 0) {
        uint16_t counter = timerCounters[i];
        const uint16_t reload =
            (uint16_t)(io_regs[baseOffset] | (io_regs[baseOffset + 1] << 8));

        while (increments > 0) {
          const int toOverflow = 0x10000 - (int)counter;
          if (increments >= toOverflow) {
            increments -= toOverflow;
            overflowCount++;
            counter = reload;

            // Notify APU of timer overflow (for sound sample consumption).
            // While a DMA is executing, GBAMemory accounts cycles by calling
            // UpdateTimers(); suppressing the APU tick during that internal
            // accounting prevents the sound FIFO from being drained during the
            // DMA itself (which otherwise causes pathological re-requests).
            if (!dmaInProgress && apu && (i == 0 || i == 1)) {
              apu->OnTimerOverflow(i);
            }

            // IRQ
            if (control & TimerControl::IRQ_ENABLE) {
              uint16_t if_reg =
                  io_regs[IORegs::IF] | (io_regs[IORegs::IF + 1] << 8);
              if_reg |= (InterruptFlags::TIMER0 << i);
              io_regs[IORegs::IF] = if_reg & 0xFF;
              io_regs[IORegs::IF + 1] = (if_reg >> 8) & 0xFF;
            }

            // Sound DMA trigger (Timer 0 and Timer 1 only)
            if (i == 0 || i == 1) {
              uint16_t soundcntH = io_regs[IORegs::SOUNDCNT_H] |
                                   (io_regs[IORegs::SOUNDCNT_H + 1] << 8);
              int fifoATimer = (soundcntH >> 10) & 1;
              int fifoBTimer = (soundcntH >> 14) & 1;

              for (int dma = 1; dma <= 2; dma++) {
                uint32_t dmaBase =
                    IORegs::DMA0SAD + (dma * IORegs::DMA_CHANNEL_SIZE);
                uint16_t dmaControl =
                    io_regs[dmaBase + 10] | (io_regs[dmaBase + 11] << 8);

                if (dmaControl & DMAControl::ENABLE) {
                  int dmaTiming =
                      (dmaControl & DMAControl::START_TIMING_MASK) >> 12;
                  if (dmaTiming == 3) { // Special timing (sound FIFO)
                    uint32_t dmaDest = io_regs[dmaBase + 4] |
                                       (io_regs[dmaBase + 5] << 8) |
                                       (io_regs[dmaBase + 6] << 16) |
                                       (io_regs[dmaBase + 7] << 24);

                    bool isFifoA = (dmaDest == 0x040000A0);
                    bool isFifoB = (dmaDest == 0x040000A4);

                    // Real hardware requests sound FIFO DMA when the FIFO
                    // level is low (roughly <= 16 samples remain).
                    bool shouldRequest = false;
                    if (apu) {
                      if (isFifoA && fifoATimer == i) {
                        shouldRequest = (apu->GetFifoACount() <= 16);
                      } else if (isFifoB && fifoBTimer == i) {
                        shouldRequest = (apu->GetFifoBCount() <= 16);
                      }
                    }

                    if (shouldRequest && !dmaInProgress) {
                      PerformDMA(dma);
                    }
                  }
                }
              }
            }
          } else {
            counter = (uint16_t)(counter + increments);
            increments = 0;
          }
        }

        timerCounters[i] = counter;
      }

      previousOverflows = overflowCount;
    } else {
      previousOverflows = 0;
    }
  }
}

void GBAMemory::AdvanceCycles(int cycles) {
  UpdateTimers(cycles);
  if (ppu)
    ppu->Update(cycles);
  if (apu)
    apu->Update(cycles);
}

uint16_t GBAMemory::GetTimerReload(int timerIdx) const {
  if (timerIdx < 0 || timerIdx >= 4)
    return 0;
  const uint32_t baseOffset =
      IORegs::TM0CNT_L + (uint32_t)timerIdx * IORegs::TIMER_CHANNEL_SIZE;
  if (baseOffset + 1 >= io_regs.size())
    return 0;
  return (uint16_t)(io_regs[baseOffset] | (io_regs[baseOffset + 1] << 8));
}

uint16_t GBAMemory::GetTimerControl(int timerIdx) const {
  if (timerIdx < 0 || timerIdx >= 4)
    return 0;
  const uint32_t baseOffset =
      IORegs::TM0CNT_L + (uint32_t)timerIdx * IORegs::TIMER_CHANNEL_SIZE;
  const uint32_t ctrlOff = baseOffset + 2u;
  if (ctrlOff + 1 >= io_regs.size())
    return 0;
  return (uint16_t)(io_regs[ctrlOff] | (io_regs[ctrlOff + 1] << 8));
}

uint16_t GBAMemory::ReadEEPROM() {
  static const bool lsbFirst = EnvFlagCached("AIO_EEPROM_LSB_FIRST");
  static const bool dummyHigh = EnvFlagCached("AIO_EEPROM_DUMMY_HIGH");
  if (EnvFlagCached("AIO_TRACE_EEPROM_IO")) {
    static int readLogs = 0;
    if (readLogs < 200) {
      const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
      AIO::Emulator::Common::Logger::Instance().LogFmt(
          AIO::Emulator::Common::LogLevel::Info, "EEPROM_IO",
          "Read state=%d bit=%d delay=%d PC=0x%08x", (int)eepromState,
          (int)eepromBitCounter, (int)eepromWriteDelay, (unsigned)pc);
      readLogs++;
    }
  }

  uint16_t ret = EEPROMConsts::READY_HIGH; // Default to Ready (high)

  if (eepromWriteDelay > 0) {
    static int busyReads = 0;
    if (verboseLogs && busyReads < 10) {
      std::cerr << "[EEPROM] Still busy, delay=" << eepromWriteDelay
                << std::endl;
      busyReads++;
    }
    return EEPROMConsts::BUSY_LOW; // Busy
  }

  if (verboseLogs) {
    static int readCount = 0;
    if (readCount < 50) {
      readCount++;
      if (readCount % 10 == 0) {
        std::cout << "[EEPROM READ] state=" << (int)eepromState
                  << " bitCounter=" << eepromBitCounter << " returning=" << ret
                  << std::endl;
      }
    }
  }
  if (eepromState == EEPROMState::ReadDummy) {
    ret = dummyHigh ? EEPROMConsts::READY_HIGH : EEPROMConsts::BUSY_LOW;
    eepromBitCounter++;
    if (eepromBitCounter >= EEPROMConsts::DUMMY_BITS) { // Standard 4 dummy bits
      eepromState = EEPROMState::ReadData;
      eepromBitCounter = 0;
    }
  } else if (eepromState == EEPROMState::ReadData) {
    // Per GBATEK: "data (conventionally MSB first)"
    int bitIndex = lsbFirst
                       ? eepromBitCounter
                       : ((EEPROMConsts::DATA_BITS - 1) - eepromBitCounter);
    ret = (EEPROMConsts::BUSY_LOW | ((eepromBuffer >> bitIndex) & 1));

    eepromBitCounter++;
    if (eepromBitCounter >= EEPROMConsts::DATA_BITS) {
      eepromState = EEPROMState::Idle;
      eepromBitCounter = 0;
      eepromBufferValid = false;
    }
  } else {
    // Active but not outputting data (e.g. receiving address) or Idle
    // The GBA data bus is pulled up (High-Z) when not driven by the EEPROM
    ret = EEPROMConsts::READY_HIGH;
  }
  return ret;
}

void GBAMemory::WriteEEPROM(uint16_t value) {
  if (EnvFlagCached("AIO_TRACE_EEPROM_IO")) {
    static int writeLogs = 0;
    if (writeLogs < 400) {
      const uint32_t pc = cpu ? (uint32_t)cpu->GetRegister(15) : 0u;
      AIO::Emulator::Common::Logger::Instance().LogFmt(
          AIO::Emulator::Common::LogLevel::Info, "EEPROM_IO",
          "Write bit=%u state=%d bit=%d delay=%d PC=0x%08x",
          (unsigned)(value & EEPROMConsts::BIT_MASK), (int)eepromState,
          (int)eepromBitCounter, (int)eepromWriteDelay, (unsigned)pc);
      writeLogs++;
    }
  }

  if (eepromWriteDelay > 0) {
    return;
  }

  uint8_t bit = value & EEPROMConsts::BIT_MASK;
  eepromLatch = bit; // Update latch

  switch (eepromState) {
  case EEPROMState::Idle:
    if (bit == 1) {
      eepromState = EEPROMState::ReadCommand;
    }
    break;

  case EEPROMState::ReadCommand:
    if (bit == 1) {
      eepromState = EEPROMState::ReadAddress; // Command 11 = READ
      eepromBitCounter = 0;
      eepromAddress = 0;
    } else {
      eepromState = EEPROMState::WriteAddress; // Command 10 = WRITE
      eepromBitCounter = 0;
      eepromAddress = 0;
    }
    break;

  case EEPROMState::ReadAddress:
    eepromAddress = (eepromAddress << 1) | bit;
    eepromBitCounter++;

    if (eepromBitCounter >= (eepromIs64Kbit ? EEPROMConsts::ADDR_BITS_64K
                                            : EEPROMConsts::ADDR_BITS_4K)) {
      // GBATEK: upper 4 address bits of 64Kbit variant are ignored (only lower
      // 10 bits matter) and 4Kbit uses lower 6 bits. Mask explicitly after the
      // full address has been received.
      eepromAddress &= eepromIs64Kbit ? 0x3FF : 0x3F;

      // Prepare data buffer immediately
      uint32_t offset = eepromAddress * EEPROMConsts::BYTES_PER_BLOCK;
      eepromBuffer = 0;

      if (verboseLogs) {
        static bool sizeLogged = false;
        if (!sizeLogged) {
          std::cerr << "[EEPROM DEBUG] eepromData.size()=" << eepromData.size()
                    << std::endl;
          sizeLogged = true;
        }
      }

      if (offset + (EEPROMConsts::BYTES_PER_BLOCK - 1) < eepromData.size()) {
        // Per GBATEK: "64 bits data (conventionally MSB first)"
        // Build big-endian buffer (MSB = byte 0)

        if (verboseLogs) {
          static int firstByteLog = 0;
          if (firstByteLog < 5) {
            std::cerr << "[EEPROM BYTE ACCESS] offset=" << offset
                      << " eepromData[" << offset << "]=" << std::hex
                      << (int)eepromData[offset] << " eepromData["
                      << (offset + 1) << "]=" << (int)eepromData[offset + 1]
                      << std::dec << std::endl;
            firstByteLog++;
          }
        }

        for (int i = 0; i < (int)EEPROMConsts::BYTES_PER_BLOCK; ++i) {
          eepromBuffer |= ((uint64_t)eepromData[offset + i] << (56 - i * 8));
        }

        // SMA2 investigation: log read transactions (block + payload) so we can
        // identify which block triggers the game's "repair/format" path.
        // Enable with: AIO_TRACE_EEPROM_READ_TXN=1
        if (cpu && EnvFlagCached("AIO_TRACE_EEPROM_READ_TXN")) {
          // Keep this low-noise: only log AA2E and only the first N reads.
          if (gameCode == "AA2E") {
            static int txnLogs = 0;
            if (txnLogs < 300) {
              const uint32_t pc = (uint32_t)cpu->GetRegister(15);
              AIO::Emulator::Common::Logger::Instance().LogFmt(
                  AIO::Emulator::Common::LogLevel::Info, "SMA2",
                  "EEPROM READ TXN: block=%u offset=0x%04x data=0x%016llx "
                  "PC=0x%08x",
                  (unsigned)eepromAddress, (unsigned)offset,
                  (unsigned long long)eepromBuffer, (unsigned)pc);
              txnLogs++;
            }
          }
        }

        if (verboseLogs) {
          static int readLogCount = 0;
          if (readLogCount < 20) {
            std::cerr << "[EEPROM READ PREP] Block=" << eepromAddress
                      << " Offset=0x" << std::hex << offset << std::dec;
            std::cerr << " Bytes[0x" << std::hex << offset << "]=";
            for (int i = 0; i < (int)EEPROMConsts::BYTES_PER_BLOCK && i < 8;
                 ++i) {
              std::cerr << std::setw(2) << std::setfill('0')
                        << (int)eepromData[offset + i];
            }
            std::cerr << " Data=0x" << std::setw(16) << eepromBuffer << std::dec
                      << std::endl;
            readLogCount++;
          }
        }
      } else {
        eepromBuffer = 0xFFFFFFFFFFFFFFFFULL;
      }

      eepromState = EEPROMState::ReadStopBit;
      eepromBitCounter = 0;
    }
    break;

  case EEPROMState::ReadStopBit:
    // Expecting a '0' bit to terminate the read request
    if (bit != 0) {
      // SMA2 sends Stop Bit 1.
      // This violates the standard, but the game still expects dummy bits (DMA
      // count is 68). If we skip dummy bits, the data is shifted and corrupted.
    }

    // CRITICAL: Mark that buffer is NOW valid for this transaction
    eepromBufferValid = true;

    // Always proceed to ReadDummy
    eepromState = EEPROMState::ReadDummy;
    eepromBitCounter = 0;
    break;

  case EEPROMState::WriteAddress:
    // Per GBATEK: "n bits eeprom address (MSB first, 6 or 14 bits)"
    eepromAddress = (eepromAddress << 1) | bit;
    eepromBitCounter++;
    if (eepromBitCounter >= (eepromIs64Kbit ? EEPROMConsts::ADDR_BITS_64K
                                            : EEPROMConsts::ADDR_BITS_4K)) {
      // GBATEK: upper 4 address bits of 64Kbit variant are ignored (only lower
      // 10 bits matter) and 4Kbit uses lower 6 bits. Mask explicitly after the
      // full address has been received.
      eepromAddress &= eepromIs64Kbit ? 0x3FF : 0x3F;

      eepromState = EEPROMState::WriteData;
      eepromBitCounter = 0;
      eepromBuffer = 0;
    }
    break;

  case EEPROMState::WriteData:
    eepromBuffer = (eepromBuffer << 1) | bit;
    eepromBitCounter++;
    if (eepromBitCounter >= EEPROMConsts::DATA_BITS) {
      eepromState = EEPROMState::WriteTermination;
      eepromBitCounter = 0;
    }
    break;

  case EEPROMState::WriteTermination:
    // Expecting a '0' bit to terminate the write command

    // Some titles (notably SMA2) appear to violate the documented termination
    // bit. Accept either 0 or 1 here so the write still commits, otherwise the
    // game can get stuck repeatedly re-validating "corrupt" save data.
    if (bit == 0 || bit == 1) {
      // Commit Write
      uint32_t offset = eepromAddress * EEPROMConsts::BYTES_PER_BLOCK;

      // Check if game is writing back what it read
      bool isMismatch = false;
      uint64_t existingData = 0;
      if (offset + (EEPROMConsts::BYTES_PER_BLOCK - 1) < eepromData.size()) {
        for (int i = 0; i < (int)EEPROMConsts::BYTES_PER_BLOCK; ++i) {
          existingData |= ((uint64_t)eepromData[offset + i] << (56 - i * 8));
        }
        if (verboseLogs && existingData != eepromBuffer && eepromAddress == 2) {
          isMismatch = true;
          std::cerr << "[EEPROM MISMATCH!] Block 2: read=0x" << std::hex
                    << existingData << " but writing=0x" << eepromBuffer
                    << std::dec << std::endl;
        }
      }

      // Root-cause tracer: capture the exact CPU context at the moment SMA2
      // decides to rewrite the EEPROM header block. Enable with:
      // AIO_TRACE_EEPROM_REWRITE=1
      const bool traceRewrite = EnvFlagCached("AIO_TRACE_EEPROM_REWRITE");
      if (traceRewrite && cpu && eepromAddress == 2) {
        // Known divergence observed vs mGBA reference.
        static bool loggedKnownRewrite = false;
        constexpr uint64_t kRef = 0xFEB801010101DA69ULL;
        constexpr uint64_t kOut = 0xFEBC00000000DA69ULL;
        if (!loggedKnownRewrite && existingData == kRef &&
            eepromBuffer == kOut) {
          loggedKnownRewrite = true;
          std::cerr << "[EEPROM REWRITE DETECTED] block=2 offset=0x" << std::hex
                    << offset << " existing=0x" << existingData << " new=0x"
                    << eepromBuffer << " termBit=" << std::dec << (int)bit
                    << "\n";

          // Snapshot DMA3 registers (common path for EEPROM transfers).
          auto io32 = [&](uint32_t ioOffset) -> uint32_t {
            if (ioOffset + 3 >= io_regs.size())
              return 0;
            return (uint32_t)io_regs[ioOffset] |
                   ((uint32_t)io_regs[ioOffset + 1] << 8) |
                   ((uint32_t)io_regs[ioOffset + 2] << 16) |
                   ((uint32_t)io_regs[ioOffset + 3] << 24);
          };
          const uint32_t dma3sad = io32(0x00D4);
          const uint32_t dma3dad = io32(0x00D8);
          const uint32_t dma3cnt = io32(0x00DC);
          std::cerr << "[DMA3] SAD=0x" << std::hex << dma3sad << " DAD=0x"
                    << dma3dad << " CNT=0x" << dma3cnt << std::dec << "\n";

          // Dump a short preview of the source buffer (typically 0x51 halfwords
          // for EEPROM write). Guarded to RAM regions to avoid re-entrancy
          // surprises.
          if ((dma3sad >= 0x02000000 && dma3sad < 0x04000000)) {
            std::cerr << "[DMA3 SRC PREVIEW]";
            for (int i = 0; i < 16; ++i) {
              const uint16_t hw = Read16(dma3sad + (uint32_t)(i * 2));
              std::cerr << " " << std::hex << std::setw(4) << std::setfill('0')
                        << hw;
            }
            std::cerr << std::dec << "\n";

            // If this looks like an EEPROM write transfer (81 halfwords),
            // decode it. Expected (GBATEK): start(1) + cmd(1) + addr(14) +
            // data(64) + term(1) = 81 bits.
            const uint32_t count = (dma3cnt & 0xFFFF);
            if (count == 0x51) {
              uint8_t bits[0x51];
              for (uint32_t i = 0; i < 0x51; ++i) {
                bits[i] = (uint8_t)(Read16(dma3sad + i * 2) & 1);
              }

              const uint8_t start = bits[0];
              const uint8_t cmd = bits[1];
              uint32_t addr14 = 0;
              for (uint32_t i = 0; i < 14; ++i) {
                addr14 = (addr14 << 1) | bits[2 + i];
              }
              uint64_t data64 = 0;
              for (uint32_t i = 0; i < 64; ++i) {
                data64 = (data64 << 1) | bits[2 + 14 + i];
              }
              const uint8_t term = bits[2 + 14 + 64];

              // For 64Kbit EEPROM, upper 4 address bits are ignored; lower 10
              // bits select the 8-byte block.
              const uint32_t block = (addr14 & 0x3FF);
              std::cerr << "[EEPROM DMA DECODE] start=" << (int)start
                        << " cmd=" << (int)cmd << " addr14=0x" << std::hex
                        << addr14 << " block=" << std::dec << block
                        << " data=0x" << std::hex << std::setw(16)
                        << std::setfill('0') << data64 << " term=" << std::dec
                        << (int)term << "\n";
            }
          }

          cpu->DumpState(std::cerr);

          // Extra caller context: LR window + stack words.
          const uint32_t lr = cpu->GetRegister(14);
          const uint32_t sp = cpu->GetRegister(13);
          std::cerr << std::hex;
          std::cerr << "LR=0x" << lr << " SP=0x" << sp << std::dec << "\n";

          const uint32_t lrAligned = (lr & ~1u);
          if (lrAligned >= 0x08000000) {
            std::cerr << "Disasm window around LR:" << "\n";
            for (int i = -16; i <= 16; i += 2) {
              const uint32_t addr = lrAligned + (uint32_t)i;
              const uint16_t op = Read16(addr);
              std::cerr << "  0x" << std::hex << addr << ": 0x" << op
                        << ((i == 0) ? " <--" : "") << std::dec << "\n";
            }
          }

          if ((sp >= 0x02000000 && sp < 0x04000000)) {
            std::cerr << "Stack words:" << "\n";
            for (int i = 0; i < 8; ++i) {
              const uint32_t addr = sp + (uint32_t)(i * 4);
              const uint32_t val = Read32(addr);
              std::cerr << "  [0x" << std::hex << addr << "] = 0x" << val
                        << std::dec << "\n";
            }
          }
        }
      }

      if (verboseLogs && (eepromAddress == 2 || isMismatch)) {
        std::cerr << "[EEPROM WRITE] Block=" << eepromAddress << " Data=0x"
                  << std::hex << std::setfill('0') << std::setw(16)
                  << eepromBuffer << std::dec << std::endl;
      }

      if (offset + (EEPROMConsts::BYTES_PER_BLOCK - 1) < eepromData.size()) {
        for (int i = 0; i < (int)EEPROMConsts::BYTES_PER_BLOCK; ++i) {
          uint8_t byteVal = (eepromBuffer >> (56 - i * 8)) & 0xFF;
          eepromData[offset + i] = byteVal;
        }
      }

      // Targeted trace for SMA2 save validation/repair loops.
      // Helps confirm whether the game is attempting to rewrite key blocks and
      // whether writes are being committed at all.
      if (verboseLogs && (gameCode == "AMQE" || gameCode == "AMQP" ||
                          gameCode == "AMQJ" || gameCode == "AA2E")) {
        static int sma2WriteCommitsLogged = 0;
        if (sma2WriteCommitsLogged < 80) {
          std::cerr << "[EEPROM COMMIT] game=" << gameCode
                    << " termBit=" << (int)bit << " block=" << eepromAddress
                    << " data=0x" << std::hex << std::setw(16)
                    << std::setfill('0') << eepromBuffer << std::dec
                    << std::endl;
          sma2WriteCommitsLogged++;
        }
      }

      FlushSave();
      // Stable timing that prevents crashes
      eepromWriteDelay = 1000;
    }

    // If the termination bit is 1, treat it as an implicit start bit for a
    // potential back-to-back transaction.
    eepromState = (bit == 1) ? EEPROMState::ReadCommand : EEPROMState::Idle;
    break;

  case EEPROMState::ReadDummy:
  case EEPROMState::ReadData: {
    // Protocol variant support: some titles emit an extra "dummy write" (or
    // otherwise clock via writes) during the read phase. On real hardware, each
    // access clocks the serial interface; ignoring these writes shifts the read
    // stream and can cause save validation to fail.
    if (eepromState == EEPROMState::ReadDummy) {
      eepromBitCounter++;
      if (eepromBitCounter >= EEPROMConsts::DUMMY_BITS) {
        eepromState = EEPROMState::ReadData;
        eepromBitCounter = 0;
      }
    } else { // ReadData
      eepromBitCounter++;
      if (eepromBitCounter >= EEPROMConsts::DATA_BITS) {
        eepromState = EEPROMState::Idle;
        eepromBitCounter = 0;
        eepromBufferValid = false;
      }
    }
    break;
  }

  default:
    eepromState = EEPROMState::Idle;
    break;
  }
}

} // namespace AIO::Emulator::GBA
