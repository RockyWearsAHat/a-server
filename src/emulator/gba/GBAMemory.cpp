#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <emulator/gba/APU.h>
#include <emulator/gba/ARM7TDMI.h>
#include <emulator/gba/GBA.h>
#include <emulator/gba/GBAMemory.h>
#include <emulator/gba/GameDB.h>
#include <emulator/gba/IORegs.h>
#include <emulator/gba/PPU.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>

namespace AIO::Emulator::GBA {

namespace {
constexpr uint32_t kIrqHandlerAddress = 0x03007FFCu;
constexpr uint32_t kIrqHandlerOffset = 0x7FFCu;
constexpr uint32_t kIrqHandlerDefault = 0x00003FF0u;

// DMA wait states per memory region (GBATEK/mGBA reference)
// Indices: 0=BIOS, 1=unused, 2=EWRAM, 3=IWRAM, 4=IO, 5=Palette, 6=VRAM, 7=OAM,
// 8+=ROM ROM wait states are configurable via WAITCNT; these are defaults.
constexpr int8_t kDmaWaitNonseq16[16] = {0, 0, 2, 0, 0, 0, 0, 0,
                                         4, 4, 4, 4, 4, 4, 4, 0};
constexpr int8_t kDmaWaitSeq16[16] = {0, 0, 2, 0, 0, 0, 0, 0,
                                      2, 2, 4, 4, 8, 8, 4, 0};
constexpr int8_t kDmaWaitNonseq32[16] = {0, 0, 5, 0, 0,  1,  1, 0,
                                         7, 7, 9, 9, 13, 13, 9, 0};
constexpr int8_t kDmaWaitSeq32[16] = {0, 0, 5, 0, 0,  1,  1, 0,
                                      5, 5, 9, 9, 17, 17, 9, 0};

inline int GetDmaCyclesPerWord(uint32_t srcRegion, uint32_t dstRegion,
                               bool is32Bit, bool isFirst) {
  srcRegion &= 0xF;
  dstRegion &= 0xF;
  if (is32Bit) {
    if (isFirst) {
      return kDmaWaitNonseq32[srcRegion] + kDmaWaitNonseq32[dstRegion];
    }
    return kDmaWaitSeq32[srcRegion] + kDmaWaitSeq32[dstRegion];
  } else {
    if (isFirst) {
      return kDmaWaitNonseq16[srcRegion] + kDmaWaitNonseq16[dstRegion];
    }
    return kDmaWaitSeq16[srcRegion] + kDmaWaitSeq16[dstRegion];
  }
}

inline bool IsIwramMappedAddress(uint32_t address) {
  // IWRAM is 32KB at 0x03000000-0x03007FFF.
  // On real hardware, the 0x03xxxxxx region aliases into IWRAM via address
  // line decoding (mirroring within 0x03000000-0x03FFFFFF).
  return (address & 0xFF000000u) == 0x03000000u;
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
  // NOTE: Each unique string literal needs a unique static.
  // The previous template-by-N approach caused collisions between
  // different env vars of the same length. Using a runtime approach instead.
  static thread_local std::unordered_map<std::string, bool> cache;
  auto it = cache.find(name);
  if (it != cache.end())
    return it->second;
  bool val = EnvTruthy(std::getenv(name));
  cache[name] = val;
  return val;
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

  // Shadow buffers for timing-gated graphics writes.
  palette_shadow = palette_ram;
  vram_shadow = vram;
  oam_shadow = oam;

  const uint32_t palBlocks =
      (uint32_t)((palette_ram.size() + kDeferredBlockSize - 1u) /
                 kDeferredBlockSize);
  const uint32_t vramBlocks =
      (uint32_t)((vram.size() + kDeferredBlockSize - 1u) / kDeferredBlockSize);
  const uint32_t oamBlocks =
      (uint32_t)((oam.size() + kDeferredBlockSize - 1u) / kDeferredBlockSize);
  palette_dirtyBlocks.assign(palBlocks, 0);
  vram_dirtyBlocks.assign(vramBlocks, 0);
  oam_dirtyBlocks.assign(oamBlocks, 0);

  palette_dirtyList.reserve(palBlocks);
  vram_dirtyList.reserve(vramBlocks);
  oam_dirtyList.reserve(oamBlocks);
  // ROM and SRAM sizes depend on the loaded game, but we can set defaults
  rom.resize(MemoryMap::ROM_MAX_SIZE);
  romSize = MemoryMap::ROM_MAX_SIZE; // Default: full 32MB accessible for tests
  romMask = MemoryMap::ROM_MIRROR_MASK; // Default: 32MB mask
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
bool GBAMemory::LoadLLEBIOS(const std::string &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    std::cerr << "[GBAMemory] Failed to open LLE BIOS file: " << path
              << std::endl;
    return false;
  }

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  if (size <= 0) {
    std::cerr << "[GBAMemory] LLE BIOS file is empty: " << path << std::endl;
    return false;
  }

  std::vector<uint8_t> buffer(static_cast<size_t>(size));
  if (!file.read(reinterpret_cast<char *>(buffer.data()), size)) {
    std::cerr << "[GBAMemory] Failed to read LLE BIOS file: " << path
              << std::endl;
    return false;
  }

  if (bios.empty()) {
    bios.resize(0x4000);
  }

  const size_t copySize = std::min(buffer.size(), bios.size());
  std::copy(buffer.begin(), buffer.begin() + copySize, bios.begin());
  if (copySize < bios.size()) {
    std::fill(bios.begin() + copySize, bios.end(), 0);
  }

  std::cout << "[GBAMemory] Loaded LLE BIOS (" << copySize
            << " bytes) from: " << path << std::endl;

  lleBiosLoaded = true;
  return true;
}

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
  // This trampoline is a minimal IRQ dispatcher suitable for DirectBoot,
  // relocated to 0x3F00 to keep 0x188/0x194 free for VBlankIntrWait/IntrWait.
  //
  // Key behaviors:
  // - Save volatile regs on SP_irq
  // - Call user handler at [0x03FFFFFC] (mirror of 0x03007FFC)
  // - Acknowledge/clear REG_IF using the triggered mask at 0x03007FF4
  // - Restore regs and exception-return via SUBS PC, LR, #4
  //
  // NOTE: Handler runs in IRQ mode (not System mode). While real BIOS switches
  // to System mode, many games work correctly (or even require) IRQ mode.

  uint32_t base = kIrqTrampolineBase;
  const uint32_t trampoline[] = {
      // 0x3F00: STMDB SP!, {R0-R3, R12, LR}
      0xE92D500F,
      // 0x3F04: MOV   R0, #0x04000000
      0xE3A00404,
      // 0x3F08: ADD   LR, PC, #0      ; set return address (0x3F10)
      0xE28FE000,
      // 0x3F0C: LDR   PC, [R0, #-4]   ; jump to [0x03FFFFFC] user handler
      0xE510F004,
      // 0x3F10: LDR   R1, [PC, #16]   ; &0x03007FF4 (literal at 0x3F28)
      0xE59F1010,
      // 0x3F14: LDRH  R1, [R1]        ; triggered mask
      0xE1D110B0,
      // 0x3F18: ADD   R0, R0, #0x200  ; point to IO+0x200 region
      0xE2800F80,
      // 0x3F1C: STRH  R1, [R0, #2]    ; REG_IF at 0x04000202 (write-1-to-clear)
      0xE1C010B2,
      // 0x3F20: LDMIA SP!, {R0-R3, R12, LR}
      0xE8BD500F,
      // 0x3F24: SUBS  PC, LR, #4      ; exception-return from IRQ
      0xE25EF004,
      // 0x3F28: literal 0x03007FF4
      0x03007FF4u};

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
  lastFetchAddr = 0xFFFFFFFFu;
  lastFetchRegionGroup = 0xFFu;
  std::fill(palette_ram.begin(), palette_ram.end(), 0);
  std::fill(vram.begin(), vram.end(), 0);
  std::fill(oam.begin(), oam.end(), 0);

  // Keep deferred-write shadow state coherent after reset.
  if (!palette_shadow.empty())
    palette_shadow = palette_ram;
  if (!vram_shadow.empty())
    vram_shadow = vram;
  if (!oam_shadow.empty())
    oam_shadow = oam;

  std::fill(palette_dirtyBlocks.begin(), palette_dirtyBlocks.end(), 0);
  std::fill(vram_dirtyBlocks.begin(), vram_dirtyBlocks.end(), 0);
  std::fill(oam_dirtyBlocks.begin(), oam_dirtyBlocks.end(), 0);
  palette_dirtyList.clear();
  vram_dirtyList.clear();
  oam_dirtyList.clear();

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

  // Initialize VCOUNT (0x04000006) to 0x7E when skipping BIOS.
  // mGBA's GBASkipBIOS() does this because when the BIOS finishes executing,
  // the hardware is in VBlank (scanline ~126). Classic NES Series games and
  // others may check VCOUNT at boot expecting VBlank state.
  // Only do this for DirectBoot (HLE); real BIOS will set VCOUNT naturally.
  if (!lleBiosLoaded && io_regs.size() > IORegs::VCOUNT + 1) {
    io_regs[IORegs::VCOUNT] = 0x7E;     // Low byte = 126
    io_regs[IORegs::VCOUNT + 1] = 0x00; // High byte = 0

    // Set DISPSTAT VBlank flag to match — scanline 126 is in VBlank
    if (io_regs.size() > IORegs::DISPSTAT + 1) {
      uint16_t dispstat =
          io_regs[IORegs::DISPSTAT] | (io_regs[IORegs::DISPSTAT + 1] << 8);
      dispstat |= 1; // VBlank flag
      io_regs[IORegs::DISPSTAT] = dispstat & 0xFF;
      io_regs[IORegs::DISPSTAT + 1] = (dispstat >> 8) & 0xFF;
    }
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

  eepromState = EEPROMState::Idle;
  eepromBitCounter = 0;
  eepromBuffer = 0;
  eepromAddress = 0;
  eepromLatch = 0;      // Initialize latch
  eepromWriteDelay = 0; // Reset write delay
  // saveTypeLocked = false; // Do NOT reset this, as it's set by LoadGamePak

  // Reset BIOS prefetch to default value. This is what Classic NES Series
  // games expect to see when reading BIOS from outside BIOS.
  // 0xE3A02004 = MOV R2, #4 (the instruction that executes after SWI return)
  biosPrefetch = 0xE3A02004;

  // BIOS HLE: Initialize critical system state that real BIOS sets up.
  // Many games poll specific IWRAM addresses waiting for BIOS background
  // tasks to complete. Without full BIOS emulation, we must pre-initialize
  // these to unblock boot sequences. When a real BIOS image is loaded (LLE
  // BIOS), skip this block and let the BIOS manage these locations.

  if (!lleBiosLoaded) {
    // System-ready flags: Games check various addresses for non-zero to
    // confirm init complete. Common addresses: 0x3002b64, 0x3007ff8
    // (BIOS_IF), 0x3007ffc (IRQ handler). Strategy: Set multiple known init
    // flags to bypass common wait loops.
    if (wram_chip.size() >= 0x8000) {
      // 0x3002b64: System init flag (SMA2, Pokemon, others)
      // Keep this minimally non-zero to unblock init loops without injecting
      // a distinctive magic value that game logic might treat as meaningful.
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
  // Store ROM data as-is - no pre-mirroring needed.
  // Hardware naturally mirrors ROMs because cartridge address lines only
  // decode the actual ROM size. We emulate this by using a power-of-two mask.
  if (data.size() > rom.size()) {
    rom.resize(data.size());
  }
  std::copy(data.begin(), data.end(), rom.begin());
  romSize = data.size();

  // Calculate ROM mask for hardware-accurate mirroring.
  // Real cartridges mirror at power-of-two boundaries because they only
  // have enough address lines for their actual size.
  // Example: 1 MiB ROM uses 20 address lines, so mask = 0x0FFFFF
  size_t pot = 1;
  while (pot < romSize) {
    pot *= 2;
  }
  romMask = static_cast<uint32_t>(pot - 1);

  std::cout << "[GBAMemory] Loaded ROM: " << romSize << " bytes, mask=0x"
            << std::hex << romMask << std::dec << std::endl;

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

  const std::vector<uint8_t> existingEeprom = eepromData;
  const std::vector<uint8_t> existingSram = sram;

  switch (type) {
  case SaveType::SRAM:
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
    hasSRAM = false;
    isFlash = false;
    eepromIs64Kbit = true;
    if (eepromData.size() != 8192) {
      eepromData.resize(8192, 0xFF); // 64Kbit EEPROM
      if (!existingEeprom.empty()) {
        size_t copySize = std::min(existingEeprom.size(), eepromData.size());
        std::copy(existingEeprom.begin(), existingEeprom.begin() + copySize,
                  eepromData.begin());
      }
    } else {
    }
    break;
  case SaveType::Auto:
    break;
  default:
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
  }
}

uint32_t GBAMemory::GetOpenBusValue() const {
  // Open bus behavior for unmapped memory reads.
  // Based on mGBA's GBALoadBad: returns the CPU prefetch values.
  // mGBA uses prefetch[0] and prefetch[1] - two sequential 16-bit fetches.
  // prefetch[0] = instruction at PC-2 (previously fetched)
  // prefetch[1] = instruction at PC (being fetched)
  //
  // For Thumb mode, different regions combine these differently.
  // For ARM mode, we return the 32-bit instruction at PC.
  if (!cpu) {
    return 0;
  }

  const uint32_t pc = cpu->GetRegister(15);
  const bool thumb = cpu->IsThumbModeFlag();
  const uint8_t pcRegion = (pc >> 24);

  uint32_t value;

  // Helper lambda to read a 16-bit value directly without recursion
  auto readHalfword = [this](uint32_t addr) -> uint16_t {
    const uint8_t region = (addr >> 24);
    switch (region) {
    case 0x02: // EWRAM
      if ((addr & 0x00FFFFFF) + 1 < MemoryMap::WRAM_BOARD_SIZE) {
        uint32_t off = addr & MemoryMap::WRAM_BOARD_MASK;
        return wram_board[off] | (wram_board[off + 1] << 8);
      }
      break;
    case 0x03: // IWRAM
      if ((addr & 0x00FFFFFF) + 1 < MemoryMap::WRAM_CHIP_SIZE) {
        uint32_t off = addr & MemoryMap::WRAM_CHIP_MASK;
        return wram_chip[off] | (wram_chip[off + 1] << 8);
      }
      break;
    case 0x06: // VRAM (code can execute from here in some games)
    {
      uint32_t off = addr & 0x1FFFF; // VRAM is 128KB with mirroring
      if (off >= 0x18000) {
        off -= 0x8000; // Mirror upper 32KB back to 0x10000-0x17FFF
      }
      if (off + 1 < vram.size()) {
        return vram[off] | (vram[off + 1] << 8);
      }
      break;
    }
    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0D: // ROM
    {
      uint32_t off = addr & 0x01FFFFFF;
      if (off + 1 < rom.size()) {
        return rom[off] | (rom[off + 1] << 8);
      } else {
        // Out of bounds ROM - return address-based open bus
        return (addr >> 1) & 0xFFFF;
      }
    }
    default:
      break;
    }
    return (addr >> 1) & 0xFFFF;
  };

  if (thumb) {
    // mGBA's GBALoadBad for Thumb mode:
    // prefetch[0] = halfword at PC-2 (previous instruction)
    // prefetch[1] = halfword at PC (current fetch)
    // The combination depends on PC region.
    const uint16_t prefetch0 = readHalfword(pc - 2); // Previous halfword
    const uint16_t prefetch1 = readHalfword(pc);     // Current halfword

    // mGBA's special handling for Thumb mode open bus:
    // Different regions have different behaviors for how prefetch[0] and [1]
    // are combined into the 32-bit open bus value.
    switch (pcRegion) {
    case 0x00: // BIOS
    case 0x07: // OAM
      // "This isn't right half the time, but we don't have $+6 handy"
      // value = (prefetch[1] << 16) | prefetch[0]
      value = ((uint32_t)prefetch1 << 16) | prefetch0;
      break;
    case 0x03: // IWRAM
      // "This doesn't handle prefetch clobbering"
      // Depends on PC alignment
      if (pc & 2) {
        // Misaligned: value = (prefetch[1] << 16) | prefetch[0]
        value = ((uint32_t)prefetch1 << 16) | prefetch0;
      } else {
        // Aligned: value = prefetch[1] | (prefetch[0] << 16)
        value = prefetch1 | ((uint32_t)prefetch0 << 16);
      }
      break;
    default:
      // For most regions, duplicate prefetch[1]
      // value = prefetch[1] | (prefetch[1] << 16)
      value = prefetch1 | ((uint32_t)prefetch1 << 16);
      break;
    }
  } else {
    // ARM mode: read 32-bit instruction at PC (prefetch[1])
    // Helper lambda to read a 32-bit value directly
    auto read32 = [this](uint32_t addr) -> uint32_t {
      const uint8_t region = (addr >> 24);
      switch (region) {
      case 0x02: // EWRAM
        if ((addr & 0x00FFFFFF) + 3 < MemoryMap::WRAM_BOARD_SIZE) {
          uint32_t off = addr & MemoryMap::WRAM_BOARD_MASK;
          return wram_board[off] | (wram_board[off + 1] << 8) |
                 (wram_board[off + 2] << 16) | (wram_board[off + 3] << 24);
        }
        break;
      case 0x03: // IWRAM
        if ((addr & 0x00FFFFFF) + 3 < MemoryMap::WRAM_CHIP_SIZE) {
          uint32_t off = addr & MemoryMap::WRAM_CHIP_MASK;
          return wram_chip[off] | (wram_chip[off + 1] << 8) |
                 (wram_chip[off + 2] << 16) | (wram_chip[off + 3] << 24);
        }
        break;
      case 0x06: // VRAM (code can execute from here in some games)
      {
        uint32_t off = addr & 0x1FFFF; // VRAM is 128KB with mirroring
        if (off >= 0x18000) {
          off -= 0x8000; // Mirror upper 32KB back to 0x10000-0x17FFF
        }
        if (off + 3 < vram.size()) {
          return vram[off] | (vram[off + 1] << 8) | (vram[off + 2] << 16) |
                 (vram[off + 3] << 24);
        }
        break;
      }
      case 0x08:
      case 0x09:
      case 0x0A:
      case 0x0B:
      case 0x0C:
      case 0x0D: // ROM
      {
        uint32_t off = addr & 0x01FFFFFF;
        if (off + 3 < rom.size()) {
          return rom[off] | (rom[off + 1] << 8) | (rom[off + 2] << 16) |
                 (rom[off + 3] << 24);
        } else {
          // Out of bounds - address-based pattern
          uint32_t val = ((addr & ~3u) >> 1) & 0xFFFF;
          val |= (((addr & ~3u) + 2) >> 1) << 16;
          return val;
        }
      }
      default:
        break;
      }
      return 0;
    };

    value = read32(pc);
  }

  return value;
}

int GBAMemory::GetAccessCycles(uint32_t address, int accessSize,
                               bool isInstructionFetch) const {
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

  // WAITCNT bit 14: Game Pak Prefetch Buffer enable.
  // When enabled, the prefetch buffer pre-fetches sequential ROM instructions
  // during idle bus cycles, allowing sequential instruction fetches at 1
  // internal cycle instead of 1+S.
  const bool prefetchEnabled = (waitcnt & 0x4000) != 0;

  bool sequential = false;
  if (isGamePak || isSram) {
    if (isInstructionFetch) {
      sequential = (lastFetchRegionGroup == group) &&
                   (lastFetchAddr + (uint32_t)accessSize == address);
      lastFetchAddr = address;
      lastFetchRegionGroup = group;
    } else {
      sequential = (lastGamePakAccessRegionGroup == group) &&
                   (lastGamePakAccessAddr + (uint32_t)accessSize == address);
      lastGamePakAccessAddr = address;
      lastGamePakAccessRegionGroup = group;
    }
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

    // Prefetch buffer optimization: when enabled and this is a sequential
    // instruction fetch, the prefetch buffer has already loaded the opcode
    // during prior idle bus cycles, costing only 1 internal cycle.
    if (prefetchEnabled && isInstructionFetch && sequential) {
      if (accessSize == 4) {
        return 2; // Two 16-bit prefetched halfwords = 2 internal cycles
      }
      return 1; // Single prefetched halfword = 1 internal cycle
    }

    // Non-prefetched access: base 1 cycle + configured wait.
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
  if (trackCpuDataAccess && dataAccessNestDepth == 0) {
    const uint8_t rgn = (uint8_t)(address >> 24);
    if (rgn == 0x02 || (rgn >= 0x08 && rgn <= 0x0E)) {
      int c = GetAccessCycles(address, 1);
      cpuDataAccessCycles += c;
    }
  }

  uint8_t region = (address >> 24);
  switch (region) {
  case 0x00: // BIOS (GBATEK: 0x00000000-0x00003FFF)
    // BIOS read protection / open-bus behavior:
    // Only addresses < 0x4000 are in actual BIOS region. Above that is unused.
    if (address < 0x4000) {
      // When the CPU is executing outside of BIOS, reads from BIOS return
      // biosPrefetch (typically 0xE3A02004 = "MOV R2, #4" after SWI).
      // Classic NES Series games check this value as anti-emulation protection.
      if (cpu && cpu->GetRegister(15) >= 0x00004000) {
        uint8_t result = (biosPrefetch >> ((address & 3u) * 8u)) & 0xFFu;
        return result;
      }
      // CPU is inside BIOS - return actual BIOS data
      if (address < bios.size()) {
        return bios[address];
      }
    }
    // Address >= 0x4000 in region 0: return open bus (CPU prefetch)
    {
      uint32_t openBus = GetOpenBusValue();
      uint8_t result = (openBus >> ((address & 3u) * 8u)) & 0xFFu;
      return result;
    }
  case 0x02: // WRAM (Board) (GBATEK: 0x02000000-0x0203FFFF)
  {
    const uint32_t off = address & MemoryMap::WRAM_BOARD_MASK;
    const uint8_t v = wram_board[off];
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

    // Write-only PPU registers return open bus on read (GBATEK compliance).
    // Classic NES Series games depend on this behavior - they read from these
    // registers expecting open bus, not the last written value.
    // Note: We check 16-bit register boundaries, returning open bus for both
    // bytes of a write-only halfword register.
    {
      // BG scroll registers (0x10-0x1F) - write-only
      if (offset >= 0x10 && offset <= 0x1F) {
        uint32_t openBus = GetOpenBusValue();
        return (openBus >> ((address & 3u) * 8u)) & 0xFFu;
      }
      // BG rotation/scaling registers (0x20-0x3F) - write-only
      if (offset >= 0x20 && offset <= 0x3F) {
        uint32_t openBus = GetOpenBusValue();
        return (openBus >> ((address & 3u) * 8u)) & 0xFFu;
      }
      // Window dimension registers (0x40-0x47) - write-only
      if (offset >= 0x40 && offset <= 0x47) {
        uint32_t openBus = GetOpenBusValue();
        return (openBus >> ((address & 3u) * 8u)) & 0xFFu;
      }
      // MOSAIC (0x4C-0x4D) - write-only
      if (offset >= 0x4C && offset <= 0x4D) {
        uint32_t openBus = GetOpenBusValue();
        return (openBus >> ((address & 3u) * 8u)) & 0xFFu;
      }
      // BLDY (0x54-0x55) - write-only
      if (offset >= 0x54 && offset <= 0x55) {
        uint32_t openBus = GetOpenBusValue();
        return (openBus >> ((address & 3u) * 8u)) & 0xFFu;
      }
      // FIFO registers (0xA0-0xA7) - write-only
      if (offset >= 0xA0 && offset <= 0xA7) {
        uint32_t openBus = GetOpenBusValue();
        return (openBus >> ((address & 3u) * 8u)) & 0xFFu;
      }
      // DMA source addresses - write-only, return open bus
      // DMA0SAD (0xB0-0xB3), DMA1SAD (0xBC-0xBF), DMA2SAD (0xC8-0xCB),
      // DMA3SAD (0xD4-0xD7)
      if ((offset >= 0xB0 && offset <= 0xB3) ||
          (offset >= 0xBC && offset <= 0xBF) ||
          (offset >= 0xC8 && offset <= 0xCB) ||
          (offset >= 0xD4 && offset <= 0xD7)) {
        uint32_t openBus = GetOpenBusValue();
        return (openBus >> ((address & 3u) * 8u)) & 0xFFu;
      }
      // DMA destination addresses - write-only, return open bus
      // DMA0DAD (0xB4-0xB7), DMA1DAD (0xC0-0xC3), DMA2DAD (0xCC-0xCF),
      // DMA3DAD (0xD8-0xDB)
      if ((offset >= 0xB4 && offset <= 0xB7) ||
          (offset >= 0xC0 && offset <= 0xC3) ||
          (offset >= 0xCC && offset <= 0xCF) ||
          (offset >= 0xD8 && offset <= 0xDB)) {
        uint32_t openBus = GetOpenBusValue();
        return (openBus >> ((address & 3u) * 8u)) & 0xFFu;
      }
      // DMA count registers - write-only, return 0 (mGBA behavior)
      // DMA0CNT_L (0xB8-0xB9), DMA1CNT_L (0xC4-0xC5),
      // DMA2CNT_L (0xD0-0xD1), DMA3CNT_L (0xDC-0xDD)
      if ((offset >= 0xB8 && offset <= 0xB9) ||
          (offset >= 0xC4 && offset <= 0xC5) ||
          (offset >= 0xD0 && offset <= 0xD1) ||
          (offset >= 0xDC && offset <= 0xDD)) {
        return 0;
      }
    }

    // Timer counter bytes: return live value from timerCounters[]
    // TMxCNT_L offsets: 0x100, 0x104, 0x108, 0x10C (low/high bytes)
    if (offset >= IORegs::TM0CNT_L && offset <= IORegs::TM3CNT_H) {
      int timerIdx = (offset - IORegs::TM0CNT_L) / IORegs::TIMER_CHANNEL_SIZE;
      int byteInChannel =
          (offset - IORegs::TM0CNT_L) % IORegs::TIMER_CHANNEL_SIZE;

      // Trace ALL timer byte reads to find NES emulator timing mechanism

      if (byteInChannel < 2) {
        if (gba)
          gba->FlushPendingPeripheralCycles();
        uint16_t counter = timerCounters[timerIdx];
        return (byteInChannel == 0) ? (counter & 0xFF)
                                    : ((counter >> 8) & 0xFF);
      }
    }

    uint8_t val = 0;
    if (offset < io_regs.size())
      val = io_regs[offset];

    // SOUNDCNT_X (0x84) - Return master enable + active channel status
    if (offset == IORegs::SOUNDCNT_X) {
      uint8_t masterEnable = io_regs[IORegs::SOUNDCNT_X] & 0x80;
      uint8_t chStatus = apu ? apu->GetChannelStatus() : 0;
      val = masterEnable | (chStatus & 0x0F);
    }

    // DMA3 read trace (useful for save validation loops that poll DMA regs)

    // Generic IO polling trace (8-bit). Useful for diagnosing games stuck in
    // init loops while DISPCNT remains forced blank.

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
    // Hardware-accurate ROM mirroring: Real cartridges only have address lines
    // for their actual ROM size, so addresses naturally wrap at power-of-two
    // boundaries. A 1 MiB ROM mirrors every 1 MiB throughout the 32 MB space.
    if (romSize > 0) {
      uint32_t offset = address & romMask;
      if (offset < romSize) {
        return rom[offset];
      }
      // Past actual ROM but within power-of-two boundary: open bus
      // Returns (address / 2) pattern per GBATEK
    }
    return ((address >> 1) >> ((address & 1) * 8)) & 0xFF;
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
    if (romSize > 0) {
      uint32_t offset = address & romMask;
      if (offset < romSize) {
        return rom[offset];
      }
    }
    return ((address >> 1) >> ((address & 1) * 8)) & 0xFF;
  }
  case 0x0E: // SRAM/Flash (GBATEK: 0x0E000000-0x0E00FFFF)
  }
  return 0;
}

uint16_t GBAMemory::Read16(uint32_t address) {
  if (trackCpuDataAccess && dataAccessNestDepth == 0) {
    const uint8_t rgn = (uint8_t)(address >> 24);
    if (rgn == 0x02 || (rgn >= 0x08 && rgn <= 0x0E)) {
      int c = GetAccessCycles(address, 2);
      cpuDataAccessCycles += c;
    }
  }
  ++dataAccessNestDepth;
  struct NestGuard {
    int &d;
    ~NestGuard() { --d; }
  } nestGuard{dataAccessNestDepth};

  // EEPROM Handling: only for EEPROM-save cartridges.
  uint8_t region = (address >> 24);
  if (region == 0x0D && (!hasSRAM && !isFlash)) {
    return ReadEEPROM();
  }

  // GBA IO registers are fundamentally 16-bit; halfword accesses are aligned.
  // Some titles issue unaligned halfword loads/stores into IO space; on
  // hardware these behave like aligned accesses.
  if (region == 0x04 && (address & 1u)) {
    address &= ~1u;
  }

  // Flush pending peripheral cycles for timing-sensitive reads (GBATEK
  // compliance)
  if (region == 0x04) {
    const uint32_t offset = address & 0x3FFu;
    // DISPSTAT/VCOUNT require up-to-date PPU state; timer counters require
    // up-to-date timer state
    if (gba) {
      if (offset == IORegs::DISPSTAT || offset == IORegs::VCOUNT) {
        gba->FlushPendingPeripheralCycles();
      } else if (offset >= IORegs::TM0CNT_L && offset <= IORegs::TM3CNT_H) {
        gba->FlushPendingPeripheralCycles();
      }
    }

    // Trace DISPSTAT/VCOUNT reads to understand NES emulator pacing
  }

  uint16_t val = Read8(address) | (Read8(address + 1) << 8);

  // IWRAM halfword 0x03000064. This trace shows whether the value ever changes
  // and where reads come from.

  // Trace tight IO polling loops (diagnostic for "stuck in forced blank").
  // This is intentionally very low-volume (change-triggered + periodic).

  // whether the game is actually consuming the expected 0xFFFE/0xFFFF bitstream
  // halfwords after DMA completes.

  // Timer Counters (Read from internal state)
  if ((address & 0xFF000000) == IORegs::BASE) {
    uint32_t offset = address & MemoryMap::IO_REG_MASK;

    if (offset >= IORegs::TM0CNT_L && offset <= IORegs::TM3CNT_H) {
      int timerIdx = (offset - IORegs::TM0CNT_L) / IORegs::TIMER_CHANNEL_SIZE;
      if ((offset % IORegs::TIMER_CHANNEL_SIZE) == 0) {
        // Trace ALL timer 16-bit reads

        // Trace Timer0 reads to understand NES emulator timing behavior
        if (timerIdx == 0) {
        }
        return timerCounters[timerIdx];
      }
    }
  }

  return val;
}

uint16_t GBAMemory::ReadInstruction16(uint32_t address) {
  // Direct ROM access for instruction fetch - bypass EEPROM and other checks
  if ((address >> 24) >= 0x08 && (address >> 24) <= 0x0C) {
    // Hardware-accurate ROM mirroring using power-of-two mask
    uint32_t offset = address & romMask;
    if (offset + 1 < romSize) {
      uint8_t b0 = rom[offset];
      uint8_t b1 = rom[offset + 1u];
      return b0 | (b1 << 8);
    }
    // Open bus: return address-based value (mGBA behavior)
    return (address >> 1) & 0xFFFF;
  }
  // Handle IWRAM instruction fetch with proper mirroring
  else if ((address & 0xFF000000u) == 0x03000000u) {
    uint32_t offset = address & MemoryMap::WRAM_CHIP_MASK;
    // ALWAYS access IWRAM - it's guaranteed 32KB (0x8000 = 32768 bytes)
    if (offset < wram_chip.size() && offset + 1 < wram_chip.size()) {
      uint8_t b0 = wram_chip[offset];
      uint8_t b1 = wram_chip[offset + 1];
      uint16_t result = b0 | (b1 << 8);
      // Debug: log fetches from suspect address (disabled by default)
      return result;
    }
    // If we get here, something is very wrong
    // Keep this behind a flag to avoid I/O stalls if it triggers frequently.
  }
  // Handle EWRAM instruction fetch
  else if ((address & 0xFF000000u) == 0x02000000u) {
    uint32_t offset = address & MemoryMap::WRAM_BOARD_MASK;
    if (offset + 1 < wram_board.size()) {
      uint8_t b0 = wram_board[offset];
      uint8_t b1 = wram_board[offset + 1];
      return b0 | (b1 << 8);
    }
  }
  // Fallback for other memory regions
  return Read8(address) | (Read8(address + 1) << 8);
}

uint32_t GBAMemory::Read32(uint32_t address) {
  if (trackCpuDataAccess && dataAccessNestDepth == 0) {
    const uint8_t rgn = (uint8_t)(address >> 24);
    if (rgn == 0x02 || (rgn >= 0x08 && rgn <= 0x0E)) {
      int c = GetAccessCycles(address, 4);
      cpuDataAccessCycles += c;
    }
  }
  ++dataAccessNestDepth;
  struct NestGuard {
    int &d;
    ~NestGuard() { --d; }
  } nestGuard{dataAccessNestDepth};

  // EEPROM Handling - 32-bit read performs two 16-bit reads for EEPROM-save
  // cartridges only.
  uint8_t region = (address >> 24);
  if (region == 0x0D && (!hasSRAM && !isFlash)) {
    uint16_t low = ReadEEPROM();
    uint16_t high = ReadEEPROM();
    return low | (high << 16);
  }

  // IO space word accesses are aligned on hardware.
  if (region == 0x04 && (address & 3u)) {
    address &= ~3u;
  }

  uint32_t val = Read8(address) | (Read8(address + 1) << 8) |
                 (Read8(address + 2) << 16) | (Read8(address + 3) << 24);

  // Trace tight IO polling loops via 32-bit reads.

  return val;
}

uint32_t GBAMemory::ReadInstruction32(uint32_t address) {
  // Direct ROM access for instruction fetch - bypass EEPROM and other checks
  if ((address >> 24) >= 0x08 && (address >> 24) <= 0x0C) {
    // Hardware-accurate ROM mirroring using power-of-two mask
    uint32_t offset = address & romMask;
    if (offset + 3 < romSize) {
      uint8_t b0 = rom[offset];
      uint8_t b1 = rom[offset + 1u];
      uint8_t b2 = rom[offset + 2u];
      uint8_t b3 = rom[offset + 3u];
      return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }
    // Open bus: return address-based value (mGBA LOAD_CART behavior)
    uint32_t aligned = address & ~3u;
    return ((aligned >> 1) & 0xFFFF) | (((aligned + 2) >> 1) << 16);
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

    // observed problem window.
    const int frame = ppu ? (int)ppu->GetFrameCount() : -1;

    if (offset < palette_ram.size()) {
      palette_ram[offset] = value;
      // Keep shadow coherent for later deferred apply.
      if (offset < palette_shadow.size())
        palette_shadow[offset] = value;
    }
    break;
  }
  case 0x06: // VRAM (GBATEK: 0x06000000-0x06017FFF)
  {
    uint32_t offset = address & 0x1FFFFu;
    if (offset >= MemoryMap::VRAM_ACTUAL_SIZE) {
      offset -= 0x8000u;
    }

    const int frame = ppu ? ppu->GetFrameCount() : -1;

    if (offset < vram.size()) {
      vram[offset] = value;
      // Keep shadow coherent for later deferred apply.
      if (offset < vram_shadow.size())
        vram_shadow[offset] = value;
    }
    break;
  }
  case 0x07: // OAM (GBATEK: 0x07000000-0x070003FF)
  {
    uint32_t offset = address & MemoryMap::OAM_MASK;
    if (offset < oam.size()) {
      // Enforce visibility rules: writes to OAM during the visible period are
      // blocked. HBlank writes are only allowed when H-Blank Interval Free is
      // enabled (DISPCNT bit 5). Forced blank allows all writes.
      const uint16_t dispcnt = (uint16_t)(io_regs[IORegs::DISPCNT] |
                                          (io_regs[IORegs::DISPCNT + 1] << 8));
      const bool forcedBlank = (dispcnt & 0x0080u) != 0;
      const bool inVisible =
          ppuTimingValid && (ppuTimingScanline < 160) && (ppuTimingCycle < 960);
      const bool inHBlank = ppuTimingValid && (ppuTimingCycle >= 960) &&
                            (ppuTimingScanline < 160);
      const bool hBlankIntervalFree = (dispcnt & 0x0020u) != 0;

      if (!forcedBlank) {
        if (inVisible) {
          // Block write silently.
          break;
        }
        if (inHBlank && !hBlankIntervalFree) {
          // Block write during HBlank when H-Blank Interval Free is disabled.
          break;
        }
      }

      oam[offset] = value;
      // Keep shadow coherent for later deferred apply.
      if (offset < oam_shadow.size())
        oam_shadow[offset] = value;
    }
    break;
  }
  }
}

void GBAMemory::Write8(uint32_t address, uint8_t value) {
  if (trackCpuDataAccess && dataAccessNestDepth == 0) {
    const uint8_t rgn = (uint8_t)(address >> 24);
    if (rgn == 0x02 || (rgn >= 0x08 && rgn <= 0x0E)) {
      int c = GetAccessCycles(address, 1);
      cpuDataAccessCycles += c;
    }
  }

  const bool isIwram = IsIwramMappedAddress(address);
  const uint32_t iwramOff = address & 0x7FFFu;
  const bool isIrqHandPtrByte =
      isIwram && (iwramOff >= 0x7FFCu) && (iwramOff <= 0x7FFFu);

  switch (address >> 24) {
  case 0x02: // WRAM (Board)
  {
    const uint32_t offset = address & 0x3FFFF;
    wram_board[offset] = value;
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

      // Also handle writes to DMA SAD while DMA is enabled (for sound DMA
      // buffer swapping) Games like MMBN update DMA SAD during VBlank without
      // disabling DMA
      int dmaSadChannel = -1;
      if (offset >= IORegs::DMA0SAD &&
          offset < IORegs::DMA0SAD + IORegs::DMA_CHANNEL_SIZE * 4) {
        // Calculate which channel and which byte of SAD
        int channelOffset = (offset - IORegs::DMA0SAD);
        int channel = channelOffset / IORegs::DMA_CHANNEL_SIZE;
        int byteInChannel = channelOffset % IORegs::DMA_CHANNEL_SIZE;

        // SAD occupies bytes 0-3, DAD bytes 4-7, CNT_L bytes 8-9, CNT_H bytes
        // 10-11
        if (channel >= 0 && channel < 4) {
          uint32_t dmaBase =
              IORegs::DMA0SAD + (channel * IORegs::DMA_CHANNEL_SIZE);
          uint16_t ctrl = io_regs[dmaBase + 10] | (io_regs[dmaBase + 11] << 8);
          bool isEnabled = (ctrl & DMAControl::ENABLE) != 0;
          int timing = (ctrl >> 12) & 3;

          // For sound DMA (timing=3) that's already enabled, mark for relatch.
          // Relatch on the last byte of either halfword (1 or 3) so that both
          // 16-bit STRH and 32-bit STR writes to SAD trigger a buffer swap.
          // The Sappy/m4a engine often only writes the low halfword when the
          // high half (0x0300) doesn't change between buffer swaps.
          if (isEnabled && timing == 3 &&
              (byteInChannel == 1 || byteInChannel == 3)) {
            dmaSadChannel = channel;
          }
        }
      }

      // SOUNDCNT_X (0x84): only bit 7 (master enable) is writable; bits 0-3
      // are read-only channel-active status. Byte 0x85 is entirely read-only.
      if (offset == IORegs::SOUNDCNT_X) {
        value = (value & 0x80) | (io_regs[IORegs::SOUNDCNT_X] & 0x7F);
      } else if (offset == IORegs::SOUNDCNT_X + 1) {
        return; // high byte of SOUNDCNT_X is read-only
      }

      if (offset < io_regs.size()) {
        // regs

        io_regs[offset] = value;
      }

      // KEYCNT changes can make the keypad IRQ condition become true without
      // any immediate KEYINPUT transition, so re-evaluate after writes.
      if (offset == IORegs::KEYCNT || offset == IORegs::KEYCNT + 1) {
        EvaluateKeypadIRQ();
      }

      // Handle sound DMA SAD relatch (for buffer swapping during VBlank)
      // This must be BEFORE the normal DMA latch - games update SAD while DMA
      // stays enabled
      if (dmaSadChannel >= 0) {
        uint32_t sadDmaBase =
            IORegs::DMA0SAD + (dmaSadChannel * IORegs::DMA_CHANNEL_SIZE);
        uint32_t newSrc = io_regs[sadDmaBase] | (io_regs[sadDmaBase + 1] << 8) |
                          (io_regs[sadDmaBase + 2] << 16) |
                          (io_regs[sadDmaBase + 3] << 24);

        dmaInternalSrc[dmaSadChannel] = newSrc;
      }

      // Handle DMA latch after io_regs is updated
      if (dmaLatchNeeded && dmaChannel >= 0) {
        uint32_t dmaBase =
            IORegs::DMA0SAD + (dmaChannel * IORegs::DMA_CHANNEL_SIZE);
        uint32_t oldInternalSrc = dmaInternalSrc[dmaChannel];
        dmaInternalSrc[dmaChannel] =
            io_regs[dmaBase] | (io_regs[dmaBase + 1] << 8) |
            (io_regs[dmaBase + 2] << 16) | (io_regs[dmaBase + 3] << 24);
        dmaInternalDst[dmaChannel] =
            io_regs[dmaBase + 4] | (io_regs[dmaBase + 5] << 8) |
            (io_regs[dmaBase + 6] << 16) | (io_regs[dmaBase + 7] << 24);

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
    uint32_t offset = address & 0x3FF;
    uint32_t alignedOffset = offset & ~1;
    if (alignedOffset + 1 < palette_ram.size()) {
      palette_ram[alignedOffset] = value;
      palette_ram[alignedOffset + 1] = value;
      // Keep shadow coherent for later deferred apply.
      if (alignedOffset + 1 < palette_shadow.size()) {
        palette_shadow[alignedOffset] = value;
        palette_shadow[alignedOffset + 1] = value;
      }
    }
    break;
  }
  case 0x06: // VRAM - 8-bit writes duplicate on the 16-bit bus
  {
    uint32_t offset = address & 0x1FFFFu;
    if (offset >= 0x18000u) {
      offset -= 0x8000u;
    }

    // GBA VRAM byte-write quirks:
    // - BG VRAM (0x06000000-0x0600FFFF): 8-bit writes duplicate to both bytes
    //   of the addressed halfword on the 16-bit bus.
    // - OBJ VRAM: 8-bit writes are ignored.
    //   * Modes 0-2: OBJ VRAM is 0x06010000-0x06017FFF.
    //   * Bitmap modes 3-5: 0x06010000-0x06013FFF behaves as BG VRAM, while
    //     OBJ VRAM is 0x06014000-0x06017FFF.
    const uint16_t dispcnt = (uint16_t)(io_regs[IORegs::DISPCNT] |
                                        (io_regs[IORegs::DISPCNT + 1] << 8));
    const uint8_t bgMode = (uint8_t)(dispcnt & 0x7u);
    const uint32_t objVramStart = (bgMode <= 2) ? 0x10000u : 0x14000u;
    if (offset >= objVramStart) {
      break;
    }

    // Standard GBA behavior: 8-bit writes to BG VRAM duplicate the byte
    // to both bytes of the aligned halfword (16-bit bus behavior).
    // Note: Previous Classic NES special handling was removed as it may
    // have been causing corruption - the games may actually rely on
    // standard GBA behavior.
    uint32_t alignedOffset = offset & ~1;
    if (alignedOffset + 1 < vram.size()) {
      vram[alignedOffset] = value;
      vram[alignedOffset + 1] = value;
      if (alignedOffset + 1 < vram_shadow.size()) {
        vram_shadow[alignedOffset] = value;
        vram_shadow[alignedOffset + 1] = value;
      }
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
  case 0x0D: // EEPROM - ignore 8-bit writes
    // Some titles may clock the EEPROM serial interface via byte writes.
    // Treat this as a normal EEPROM bit write (D0).
    WriteEEPROM(value);
    break;
  }

  if (isIrqHandPtrByte) {
    ClampIrqHandlerWord();
  }
}

void GBAMemory::Write16(uint32_t address, uint16_t value) {
  if (trackCpuDataAccess && dataAccessNestDepth == 0) {
    const uint8_t rgn = (uint8_t)(address >> 24);
    if (rgn == 0x02 || (rgn >= 0x08 && rgn <= 0x0E)) {
      int c = GetAccessCycles(address, 2);
      cpuDataAccessCycles += c;
    }
  }
  ++dataAccessNestDepth;
  struct NestGuard {
    int &d;
    ~NestGuard() { --d; }
  } nestGuard{dataAccessNestDepth};

  const bool isIwram = IsIwramMappedAddress(address);
  const uint32_t iwramOff = address & 0x7FFFu;
  const bool isIrqHandPtrHalf = isIwram && ((iwramOff & ~1u) == 0x7FFCu);

  // EEPROM Handling: only for EEPROM-save cartridges.
  uint8_t region = (address >> 24);

  if (region == 0x0D && (!hasSRAM && !isFlash)) {
    WriteEEPROM(value);
    return;
  }

  // GBA IO registers are fundamentally 16-bit; halfword accesses are aligned.
  // Some titles issue unaligned halfword stores into IO space; on hardware
  // these behave like aligned accesses.
  if (region == 0x04 && (address & 1u)) {
    address &= ~1u;
  }

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

    // PSG channel register writes — notify APU for immediate trigger handling.
    // Trigger bit (bit 15) is write-only, cleared after processing.
    if (apu && offset >= IORegs::SOUND1CNT_L && offset <= IORegs::SOUND4CNT_H) {
      apu->OnSoundRegisterWrite(offset, value);
      // Clear trigger bit before storing — it's write-only on hardware
      if ((offset == IORegs::SOUND1CNT_X || offset == IORegs::SOUND2CNT_H ||
           offset == IORegs::SOUND3CNT_X || offset == IORegs::SOUND4CNT_H) &&
          (value & 0x8000)) {
        value &= ~0x8000;
      }
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

  // NOTE: VRAM/Palette/OAM timing restrictions are not enforced here.
  // The current emulator relies on the PPU tests and game behavior as a
  // practical guide; stricter timing (based on
  // ppuTimingScanline/ppuTimingCycle) previously caused valid game writes to be
  // dropped, corrupting VRAM.

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
    // Optimized & Correct: Write directly to memory vectors, bypassing Write8
    // smearing/ignoring.
    uint8_t b0 = value & 0xFF;
    uint8_t b1 = (value >> 8) & 0xFF;

    if (region == 0x05) { // Palette
      uint32_t offset = address & MemoryMap::PALETTE_MASK;
      if (offset + 1 < palette_ram.size()) {
        palette_ram[offset] = b0;
        palette_ram[offset + 1] = b1;
        if (offset + 1 < palette_shadow.size()) {
          palette_shadow[offset] = b0;
          palette_shadow[offset + 1] = b1;
        }
      }
    } else if (region == 0x06) { // VRAM
      uint32_t offset = address & 0x1FFFFu;
      if (offset >= MemoryMap::VRAM_ACTUAL_SIZE)
        offset -= 0x8000u;

      if (offset + 1 < vram.size()) {
        vram[offset] = b0;
        vram[offset + 1] = b1;
        if (offset + 1 < vram_shadow.size()) {
          vram_shadow[offset] = b0;
          vram_shadow[offset + 1] = b1;
        }
      }
    } else if (region == 0x07) { // OAM
      uint32_t offset = address & MemoryMap::OAM_MASK;
      if (offset + 1 < oam.size()) {
        oam[offset] = b0;
        oam[offset + 1] = b1;
        if (offset + 1 < oam_shadow.size()) {
          oam_shadow[offset] = b0;
          oam_shadow[offset + 1] = b1;
        }
      }
    }
  } else {
    Write8(address, value & 0xFF);
    Write8(address + 1, (value >> 8) & 0xFF);
  }

  if (isIrqHandPtrHalf) {
    ClampIrqHandlerWord();
  }
}

void GBAMemory::Write32(uint32_t address, uint32_t value) {
  if (trackCpuDataAccess && dataAccessNestDepth == 0) {
    const uint8_t rgn = (uint8_t)(address >> 24);
    if (rgn == 0x02 || (rgn >= 0x08 && rgn <= 0x0E)) {
      int c = GetAccessCycles(address, 4);
      cpuDataAccessCycles += c;
    }
  }
  ++dataAccessNestDepth;
  struct NestGuard {
    int &d;
    ~NestGuard() { --d; }
  } nestGuard{dataAccessNestDepth};

  const bool isIrqHandPtrWord =
      IsIwramMappedAddress(address) && ((address & 0x7FFFu) == 0x7FFCu);

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

  // EEPROM Handling - only for EEPROM-save cartridges.
  uint8_t region = (address >> 24);
  if (region == 0x0D && (!hasSRAM && !isFlash)) {
    WriteEEPROM(value & 0xFFFF);
    WriteEEPROM(value >> 16);
    return;
  }

  // Sound FIFO writes (FIFO_A = 0x40000A0, FIFO_B = 0x40000A4)
  if (address == 0x040000A0) {
    if (apu)
      apu->WriteFIFO_A(value);
    return;
  }
  if (address == 0x040000A4) {
    if (apu)
      apu->WriteFIFO_B(value);
    return;
  }

  // For VRAM, Palette, OAM - write directly without 8-bit quirk.
  // Optionally align unaligned word stores to match HW behavior.
  if (region == 0x05 || region == 0x06 || region == 0x07) {
    address &= ~3u;
  }

  // NOTE: VRAM/Palette/OAM timing restrictions are not enforced here.
  // (Same as Write16 - see comment there.)

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
    uint8_t b0 = value & 0xFF;
    uint8_t b1 = (value >> 8) & 0xFF;
    uint8_t b2 = (value >> 16) & 0xFF;
    uint8_t b3 = (value >> 24) & 0xFF;

    if (region == 0x05) { // Palette
      uint32_t offset = address & MemoryMap::PALETTE_MASK;
      if (offset + 3 < palette_ram.size()) {
        palette_ram[offset] = b0;
        palette_ram[offset + 1] = b1;
        palette_ram[offset + 2] = b2;
        palette_ram[offset + 3] = b3;
        if (offset + 3 < palette_shadow.size()) {
          palette_shadow[offset] = b0;
          palette_shadow[offset + 1] = b1;
          palette_shadow[offset + 2] = b2;
          palette_shadow[offset + 3] = b3;
        }
      }
    } else if (region == 0x06) { // VRAM
      uint32_t offset = address & 0x1FFFFu;
      if (offset >= MemoryMap::VRAM_ACTUAL_SIZE)
        offset -= 0x8000u;

      // mysteriously changes

      if (offset + 3 < vram.size()) {
        vram[offset] = b0;
        vram[offset + 1] = b1;
        vram[offset + 2] = b2;
        vram[offset + 3] = b3;

        if (offset + 3 < vram_shadow.size()) {
          vram_shadow[offset] = b0;
          vram_shadow[offset + 1] = b1;
          vram_shadow[offset + 2] = b2;
          vram_shadow[offset + 3] = b3;
        }
      }
    } else if (region == 0x07) { // OAM
      uint32_t offset = address & MemoryMap::OAM_MASK;
      if (offset + 3 < oam.size()) {
        oam[offset] = b0;
        oam[offset + 1] = b1;
        oam[offset + 2] = b2;
        oam[offset + 3] = b3;
        if (offset + 3 < oam_shadow.size()) {
          oam_shadow[offset] = b0;
          oam_shadow[offset + 1] = b1;
          oam_shadow[offset + 2] = b2;
          oam_shadow[offset + 3] = b3;
        }
      }
    }
  } else if (region == 0x04) {
    // IO space: use 2x Write16 to preserve register-specific masking
    Write16(address, (uint16_t)(value & 0xFFFF));
    Write16(address + 2, (uint16_t)((value >> 16) & 0xFFFF));
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

uint16_t GBAMemory::ReadIORegister16Internal(uint32_t offset) const {
  // Direct read from io_regs without triggering flush (for internal PPU use).
  // This avoids infinite recursion when PPU::Update() reads DISPSTAT/VCOUNT.
  if (offset + 1 < io_regs.size()) {
    return io_regs[offset] | (io_regs[offset + 1] << 8);
  }
  return 0;
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
  static bool inImmediateDMA = false;

  if (dmaInProgress) {
    return;
  }

  dmaInProgress = true;
  struct DMAGuard {
    bool &flag;
    ~DMAGuard() { flag = false; }
  } guard{dmaInProgress};

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

  bool irq = (control >> 14) & 1;

  uint32_t currentSrc = dmaInternalSrc[channel];
  uint32_t currentDst = dmaInternalDst[channel];

  const bool dstIsPalette =
      (currentDst >= 0x05000000u && currentDst < 0x05000400u);

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
    // Update timing as if full DMA happened using proper wait states
    int step = is32Bit ? 4 : 2;
    const uint32_t srcRegion = (src >> 24) & 0xF;
    const uint32_t dstRegion = (dst >> 24) & 0xF;
    int firstCycles = GetDmaCyclesPerWord(srcRegion, dstRegion, is32Bit, true);
    int seqCycles = GetDmaCyclesPerWord(srcRegion, dstRegion, is32Bit, false);
    int totalCycles =
        2 + firstCycles + (count > 1 ? (count - 1) * seqCycles : 0);
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

  // Fast-path for EEPROM reads - only if buffer already prepared AND validated
  bool startingAtDataPhase = (eepromState == EEPROMState::ReadData);
  bool inReadSequence = (eepromState == EEPROMState::ReadDummy ||
                         eepromState == EEPROMState::ReadData);
  // CRITICAL: Only fast-path if buffer is valid for THIS transaction (set after
  // address+stop bit)
  const bool disableFastPath = EnvFlagCached("AIO_EEPROM_DISABLE_FASTPATH");
  bool canFastPath = !disableFastPath && srcIsEEPROM && inReadSequence &&
                     eepromBufferValid && count >= 4;

  if (canFastPath) {
    const bool lsbFirst = EnvFlagCached("AIO_EEPROM_LSB_FIRST");
    const bool dummyHigh = EnvFlagCached("AIO_EEPROM_DUMMY_HIGH");

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

    // This is useful because games sometimes validate the raw 16-bit words (not

    // Targeted correctness check for SMA2 save validation.
    // Confirms that the 64 data bits the game receives via DMA match the EEPROM
    // buffer we prepared.

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
      const uint32_t srcRegion = (src >> 24) & 0xF;
      const uint32_t dstRegion = (dst >> 24) & 0xF;

      for (uint32_t i = 0; i < count; ++i) {
        if (is32Bit) {
          uint32_t val = Read32(currentSrc);
          Write32(currentDst, val);

          totalCycles +=
              GetDmaCyclesPerWord(srcRegion, dstRegion, true, i == 0);
        } else {
          uint16_t val = Read16(currentSrc);
          Write16(currentDst, val);

          totalCycles +=
              GetDmaCyclesPerWord(srcRegion, dstRegion, false, i == 0);
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
  // Also, for sound FIFO DMAs (timing=3), games rely on the SAD register
  // retaining its original value so that re-enabling DMA restarts from
  // the audio buffer start, not from the advanced position.
  // CRITICAL: For repeat DMA with destCtrl=3 (Inc/Reload), do NOT write back
  // the advanced destination to io_regs. The reload reads DAD from io_regs,
  // so corrupting it causes destination drift on subsequent HBlank triggers.
  if (!srcIsEEPROM && !dstIsEEPROM && timing != 3) {
    auto writeIo32 = [&](uint32_t off, uint32_t v) {
      if (off + 3 >= io_regs.size())
        return;
      io_regs[off + 0] = (uint8_t)(v & 0xFF);
      io_regs[off + 1] = (uint8_t)((v >> 8) & 0xFF);
      io_regs[off + 2] = (uint8_t)((v >> 16) & 0xFF);
      io_regs[off + 3] = (uint8_t)((v >> 24) & 0xFF);
    };
    writeIo32(baseOffset + 0, currentSrc);
    if (!(repeat && destCtrl == 3)) {
      writeIo32(baseOffset + 4, currentDst);
    }
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

  // One-time timer state dump at frame 30

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

            // Notify APU of timer overflow (always — sample consumption must
            // not be suppressed during DMA, only the DMA re-trigger is
            // guarded by dmaInProgress below).
            if (apu && (i == 0 || i == 1)) {
              apu->OnTimerOverflow(i);
            }

            // Trace Timer0 overflow rate per frame
            if (i == 0) {
            }

            // IRQ
            if (control & TimerControl::IRQ_ENABLE) {
              uint16_t if_reg =
                  io_regs[IORegs::IF] | (io_regs[IORegs::IF + 1] << 8);
              if_reg |= (InterruptFlags::TIMER0 << i);
              io_regs[IORegs::IF] = if_reg & 0xFF;
              io_regs[IORegs::IF + 1] = (if_reg >> 8) & 0xFF;

              // Trace Timer1 IRQ for NES scroll table debugging
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
  // DIAGNOSTIC: Trace AdvanceCycles
  static int advanceTraces = 0;

  UpdateTimers(cycles);
  if (ppu)
    ppu->Update(cycles);
  if (apu)
    apu->Update(cycles);
}

void GBAMemory::ApplyDeferredWrites() {
  if (palette_dirtyList.empty() && vram_dirtyList.empty() &&
      oam_dirtyList.empty()) {
    return;
  }

  auto applyBlocks =
      [&](std::vector<uint8_t> &dst, const std::vector<uint8_t> &src,
          std::vector<uint8_t> &dirtyFlags, std::vector<uint32_t> &dirtyList) {
        for (uint32_t block : dirtyList) {
          const uint32_t start = block * kDeferredBlockSize;
          if (start >= dst.size())
            continue;
          const uint32_t len = (uint32_t)std::min<size_t>(
              (size_t)kDeferredBlockSize, dst.size() - (size_t)start);
          std::memcpy(&dst[start], &src[start], (size_t)len);
          if (block < dirtyFlags.size())
            dirtyFlags[block] = 0;
        }
        dirtyList.clear();
      };

  applyBlocks(palette_ram, palette_shadow, palette_dirtyBlocks,
              palette_dirtyList);
  applyBlocks(vram, vram_shadow, vram_dirtyBlocks, vram_dirtyList);
  applyBlocks(oam, oam_shadow, oam_dirtyBlocks, oam_dirtyList);
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

  uint16_t ret = EEPROMConsts::READY_HIGH; // Default to Ready (high)

  if (eepromWriteDelay > 0) {
    static int busyReads = 0;
    return EEPROMConsts::BUSY_LOW; // Busy
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

      if (offset + (EEPROMConsts::BYTES_PER_BLOCK - 1) < eepromData.size()) {
        // Per GBATEK: "64 bits data (conventionally MSB first)"
        // Build big-endian buffer (MSB = byte 0)

        for (int i = 0; i < (int)EEPROMConsts::BYTES_PER_BLOCK; ++i) {
          eepromBuffer |= ((uint64_t)eepromData[offset + i] << (56 - i * 8));
        }

        // identify which block triggers the game's "repair/format" path.

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
      }

      // Root-cause tracer: capture the exact CPU context at the moment SMA2

      if (offset + (EEPROMConsts::BYTES_PER_BLOCK - 1) < eepromData.size()) {
        for (int i = 0; i < (int)EEPROMConsts::BYTES_PER_BLOCK; ++i) {
          uint8_t byteVal = (eepromBuffer >> (56 - i * 8)) & 0xFF;
          eepromData[offset + i] = byteVal;
        }
      }

      // Targeted trace for SMA2 save validation/repair loops.
      // Helps confirm whether the game is attempting to rewrite key blocks and
      // whether writes are being committed at all.

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
