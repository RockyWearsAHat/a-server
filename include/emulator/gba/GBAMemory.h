#pragma once
#include "GameDB.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace AIO::Emulator::GBA {

class APU;      // Forward declaration
class PPU;      // Forward declaration
class ARM7TDMI; // Forward declaration

class GBAMemory {
public:
  GBAMemory();
  ~GBAMemory();

  void Reset();
  void LoadGamePak(const std::vector<uint8_t> &data);
  void LoadSave(const std::vector<uint8_t> &data);
  std::vector<uint8_t> GetSaveData() const;

  void SetSavePath(const std::string &path);
  void SetSaveType(SaveType type); // Configure save type from metadata analysis
  void FlushSave();                // Write EEPROM/SRAM to disk immediately
  void InitializeHLEBIOS();        // Initialize High-Level Emulated BIOS
  bool LoadLLEBIOS(const std::string &path); // Load user-provided BIOS image

  // Query whether a real (LLE) BIOS image is present. When true, CPU should
  // execute BIOS code directly instead of using HLE stubs.
  bool HasLLEBIOS() const { return lleBiosLoaded; }

  // APU connection for sound callbacks
  void SetAPU(APU *apuPtr) { apu = apuPtr; }
  // PPU connection for DMA updates
  void SetPPU(PPU *ppuPtr) { ppu = ppuPtr; }
  // CPU connection for debug
  void SetCPU(ARM7TDMI *cpuPtr) { cpu = cpuPtr; }
  // GBA connection (stub for compatibility with newer tests)
  void SetGBA(class GBA * /*unused*/) {}

  // Debug/Test helper
  void WriteROM(uint32_t address, uint8_t value) {
    uint32_t offset = address & 0x01FFFFFF;
    if (offset < rom.size()) {
      if (address == 0x14EC)
        printf("[WriteROM] 0x14EC = 0x%02X\n", value);
      rom[offset] = value;
    } else {
      printf("[WriteROM] Failed! Offset 0x%X >= Size 0x%zX\n", offset,
             rom.size());
    }
  }
  void WriteROM32(uint32_t address, uint32_t value) {
    WriteROM(address, value & 0xFF);
    WriteROM(address + 1, (value >> 8) & 0xFF);
    WriteROM(address + 2, (value >> 16) & 0xFF);
    WriteROM(address + 3, (value >> 24) & 0xFF);
  }

  // IO Registers
  void SetKeyInput(uint16_t value);

  uint8_t Read8(uint32_t address);
  uint16_t Read16(uint32_t address);
  uint32_t Read32(uint32_t address);

  // Instruction Fetch (Bypasses EEPROM logic for ROM mirrors)
  uint16_t ReadInstruction16(uint32_t address);
  uint32_t ReadInstruction32(uint32_t address);

  void Write8(uint32_t address, uint8_t value);
  void Write8Internal(uint32_t address,
                      uint8_t value); // Bypasses GBA 8-bit write quirks
  void Write16(uint32_t address, uint16_t value);
  void Write32(uint32_t address, uint32_t value);

  // Internal IO Write (Bypasses Read-Only checks)
  void WriteIORegisterInternal(uint32_t offset, uint16_t value);

  // Callback for IO Writes (Used by PPU to track Affine Registers)
  using IOWriteCallback = void (*)(void *context, uint32_t offset,
                                   uint16_t value);
  void SetIOWriteCallback(IOWriteCallback callback, void *context);

  // Callback for Graphics Memory Writes (palette/VRAM/OAM) - forces PPU sync
  using GraphicsWriteCallback = void (*)(void *context);
  void SetGraphicsWriteCallback(GraphicsWriteCallback callback, void *context) {
    onGraphicsWrite = callback;
    graphicsWriteContext = context;
  }

  void PerformDMA(int channel);
  void CheckDMA(int timing);
  void UpdateTimers(int cycles);
  void AdvanceCycles(int cycles); // Advance timers, PPU, and APU together
  void ApplyDeferredWrites();     // Apply queued palette/VRAM/OAM writes

  // PPU timing helper: publishes the current scanline/cycle so memory
  // access rules (VRAM/OAM/Palette visibility) can be enforced accurately.
  void SetPpuTimingState(int scanline, int cycleCounter);

  int GetLastDMACycles() {
    int c = lastDMACycles;
    lastDMACycles = 0;
    return c;
  }

  // Control verbose internal logging (default: false)
  void SetVerboseLogs(bool enabled) { verboseLogs = enabled; }

  // Cycle-accurate memory access timing
  int GetAccessCycles(uint32_t address, int accessSize) const;

  // Timer helpers for audio and other timing-sensitive systems. These read the
  // programmed reload value and control bits for TM0-3 directly from IO
  // registers without side effects.
  uint16_t GetTimerReload(int timerIdx) const;
  uint16_t GetTimerControl(int timerIdx) const;

  // Fast-path helpers for the renderer (PPU).
  // These bypass bus side-effects and are meant ONLY for reading the backing
  // storage for VRAM/Palette/OAM.
  const uint8_t *GetVRAMData() const { return vram.data(); }
  size_t GetVRAMSize() const { return vram.size(); }
  const uint8_t *GetPaletteData() const { return palette_ram.data(); }
  size_t GetPaletteSize() const { return palette_ram.size(); }
  const uint8_t *GetOAMData() const { return oam.data(); }
  size_t GetOAMSize() const { return oam.size(); }

  // Mutable accessors for frame snapshots (step-back feature)
  uint8_t *GetVRAM() { return vram.data(); }
  uint8_t *GetOAM() { return oam.data(); }
  uint8_t *GetPaletteRAM() { return palette_ram.data(); }
  uint8_t *GetIWRAM() { return wram_chip.data(); }
  uint8_t *GetEWRAM() { return wram_board.data(); }
  uint8_t *GetIORegs() { return io_regs.data(); }
  const uint8_t *GetIWRAM() const { return wram_chip.data(); }
  const uint8_t *GetEWRAM() const { return wram_board.data(); }
  const uint8_t *GetIORegs() const { return io_regs.data(); }

private:
  void EvaluateKeypadIRQ();

  // IRQ handler pointer (0x03007FFC) sanitization.
  // Some titles temporarily store invalid/intermediate values while building
  // the pointer. We clamp only after the full store has completed (Write8/16/32
  // at the slot) to avoid tearing.
  uint32_t ReadIrqHandlerRaw() const;
  void WriteIrqHandlerRaw(uint32_t value);
  void ClampIrqHandlerWord();

  // EEPROM protocol constants (self-documenting, no magic numbers)
  struct EEPROMConsts {
    static constexpr uint8_t DUMMY_BITS =
        4; // Number of dummy bits before data phase
    static constexpr uint8_t DATA_BITS =
        64; // 64-bit data payload per transaction
    static constexpr uint8_t ADDR_BITS_4K =
        6; // 4Kbit EEPROM uses 6-bit address
    static constexpr uint8_t ADDR_BITS_64K =
        14; // 64Kbit EEPROM uses 14-bit address
    // EEPROM serial reads drive only D0; the rest of the data lines are
    // pulled-up. Common practice (and what many titles implicitly rely on) is:
    // - bit=0 -> 0xFFFE
    // - bit=1 -> 0xFFFF
    static constexpr uint16_t READY_HIGH = 0xFFFF; // D0=1 (pulled-up bus)
    static constexpr uint16_t BUSY_LOW = 0xFFFE;   // D0=0 (pulled-up bus)
    static constexpr uint16_t BIT_MASK =
        0x0001; // Single-bit mask for input writes
    static constexpr uint32_t BLOCKS_4K =
        64; // Number of 8-byte blocks in 4Kbit
    static constexpr uint32_t BLOCKS_64K =
        1024; // Number of 8-byte blocks in 64Kbit
    static constexpr uint32_t BYTES_PER_BLOCK =
        8; // EEPROM transfers 8 bytes per block
  };

  int lastDMACycles = 0;
  int cycleCount = 0;
  int timerPrescalerCounters[4] = {0};
  uint16_t timerCounters[4] = {0};

  // Last published PPU timing state from the renderer. When valid, this is
  // used to gate writes to VRAM/OAM/Palette to periods where hardware allows
  // them (HBlank/VBlank) and to approximate bus visibility.
  bool ppuTimingValid = false;
  int ppuTimingScanline = 0;
  int ppuTimingCycle = 0;

  // Deferred write queue for graphics memory (palette/VRAM/OAM)
  // Writes during unsafe timing (VISIBLE period) are queued and applied at
  // HBlank/VBlank
  struct DeferredWrite {
    uint32_t address;
    uint8_t value;
    uint8_t region; // 5=palette, 6=VRAM, 7=OAM
  };
  std::vector<DeferredWrite> deferredWrites;

  // Internal DMA address registers (shadow registers for repeat DMAs)
  uint32_t dmaInternalSrc[4] = {0};
  uint32_t dmaInternalDst[4] = {0};
  bool dmaInProgress = false;

  // Flash State
  int flashState = 0;   // 0=Idle, 1=Cmd1(AA), 2=Cmd2(55), 3=ID Mode
  int flashCmd = 0;     // Current Flash Command
  int flashBank = 0;    // Flash Bank (for 1Mbit)
  bool isFlash = false; // True if Flash, False if SRAM/EEPROM
  bool hasSRAM = false; // True if SRAM/Flash is present, False if EEPROM only
  bool saveTypeLocked = false; // If true, prevent dynamic save type switching
  SaveType configuredSaveType = SaveType::Auto; // Last configured save type
  std::string gameCode; // Store the game code for game-specific hacks

  // EEPROM State
  enum class EEPROMState {
    Idle,
    ReadCommand,
    ReadAddress,
    ReadStopBit,
    ReadDummy,
    ReadData,
    WriteAddress,
    WriteData,
    WriteTermination
  };

  std::vector<uint8_t> eepromData;
  EEPROMState eepromState = EEPROMState::Idle;
  int eepromBitCounter = 0;
  uint64_t eepromBuffer = 0;
  uint32_t eepromAddress = 0;
  int eepromWriteDelay = 0;
  bool eepromIs64Kbit = true; // Default to 64Kbit for SMA2
  uint16_t eepromLatch = 0;   // Latch for open bus behavior on EEPROM line
  bool eepromBufferValid =
      false; // True when buffer prepared for current transaction

  // GBA Memory Map Regions
  // 00000000 - 00003FFF: BIOS (16KB)
  // 02000000 - 0203FFFF: On-board WRAM (256KB)
  // 03000000 - 03007FFF: On-chip WRAM (32KB)
  // 04000000 - 040003FE: I/O Registers
  // 05000000 - 050003FF: Palette RAM (1KB)
  // 06000000 - 06017FFF: VRAM (96KB)
  // 07000000 - 070003FF: OAM (1KB)
  // 08000000 - 0DFFFFFF: Game Pak ROM (Wait State 0, 1, 2)
  // 0E000000 - 0E00FFFF: Game Pak SRAM (64KB)

  std::vector<uint8_t> bios;
  std::vector<uint8_t> wram_board;
  std::vector<uint8_t> wram_chip;
  std::vector<uint8_t> io_regs;
  std::vector<uint8_t> palette_ram;
  std::vector<uint8_t> vram;
  std::vector<uint8_t> oam;

  // True when a user-provided BIOS image has been loaded into `bios`.
  // When set, the CPU should treat the BIOS region as real code/data and
  // avoid High-Level Emulation shortcuts for BIOS entry points.
  bool lleBiosLoaded = false;
  std::vector<uint8_t> rom;
  std::vector<uint8_t> sram;

  // EEPROM Helpers
  uint16_t ReadEEPROM();
  void WriteEEPROM(uint16_t value);
  bool Is4KbitEEPROM(const std::vector<uint8_t> &data);
  bool ScanForEEPROMSize(const std::vector<uint8_t> &data);

  IOWriteCallback ioWriteCallback = nullptr;
  void *ioWriteContext = nullptr;
  GraphicsWriteCallback onGraphicsWrite = nullptr;
  void *graphicsWriteContext = nullptr;

  std::string savePath;    // Path to save file for auto-flush
  APU *apu = nullptr;      // APU pointer for sound callbacks
  PPU *ppu = nullptr;      // PPU pointer for DMA updates
  ARM7TDMI *cpu = nullptr; // CPU pointer for debug

  // Track last Game Pak access to approximate sequential waitstate timing
  // (WAITCNT). This is intentionally lightweight (no full bus prefetch
  // emulation).
  mutable uint32_t lastGamePakAccessAddr = 0xFFFFFFFFu;
  mutable uint8_t lastGamePakAccessRegionGroup = 0xFFu;

  bool verboseLogs = false; // Guard for heavy std::cout traces
};

} // namespace AIO::Emulator::GBA
