# AIO Server Emulator Codebase Complete Inventory

**Generated:** April 15, 2026  
**Workspace:** `/Users/alexwaldmann/Desktop/AIO Server`

---

## Executive Summary

The AIO Server emulator subsystem is a **modular, multi-console emulation framework** implementing four major platforms:
- **Game Boy Advance (GBA)** — Full ARM7TDMI CPU, PPU, APU
- **PlayStation 1 (PS1)** — R3000A CPU, GPU, SPU, DMA, GTE, CD-ROM
- **Nintendo Switch** — Preliminary CPU/GPU/service cores
- **Windows x86-64** — WinAPI layer and x86-64 emulation kernel

The codebase emphasizes **factory-accurate emulation with comprehensive testing**, shared infrastructure for timing/interrupts/memory mapping, and detailed logging/fuzzing capabilities.

---

## 1. DIRECTORY STRUCTURE

### Root Emulator Hierarchy
```
src/emulator/              (Implementation source files)
├── common/                (Shared infrastructure)
├── gba/                   (Game Boy Advance)
├── ps1/                   (PlayStation 1)
├── switch/                (Nintendo Switch)
└── windows/               (Windows x86-64)

include/emulator/          (Public headers)
├── common/
├── gba/
├── ps1/
├── switch/
└── windows/

tests/                     (Test suite)
├── *Tests.cpp             (Google Test fixtures)
├── support/               (Test utilities)
└── ...                    (23 test files total)
```

---

## 2. EMULATOR IMPLEMENTATIONS & CONSOLES

### 2.1 GAME BOY ADVANCE (GBA)

**Status:** Full implementation  
**CPU:** ARM7TDMI (32-bit ARM ISA with Thumb mode)  
**Core Clock:** 16.78 MHz

#### Source Files (src/emulator/gba/)
| File | Purpose |
|------|---------|
| `ARM7TDMI.cpp` | ARM/Thumb CPU core with full instruction set, condition codes, modes |
| `GBA.cpp` | Main emulator orchestrator, lifecycle, step loop |
| `GBAMemory.cpp` | Address space management (BIOS, ROM, EWRAM, IWRAM, I/O, VRAM, OAM, Palette) |
| `PPU.cpp` | Graphics processor: scanline rendering, Mode 0-5, sprites, affine transforms |
| `APU.cpp` | Audio processor: 4 channels, DMA audio, mixing |
| `CheatManager.cpp` | Game Genie/Action Replay cheat code support |
| `ROMMetadataAnalyzer.cpp` | Game detection, save type inference, boot state configuration |
| `M4AEngine.cpp` | Sappy's Music Engine (M4A) for soundtrack playback |
| `Fuzzer.cpp` | Fault injection for robustness testing |
| `Logger.cpp` | Diagnostic logging (shared with all subsystems) |

#### Header Files (include/emulator/gba/)
| File | Purpose |
|------|---------|
| `ARM7TDMI.h` | CPU public interface, instruction decoding, register access |
| `ARM7TDMIConstants.h` | CPU mode bits, exception vectors, PSR flag definitions |
| `ARM7TDMIHelpers.h` | Bit manipulation, shift rotate, ALU helper functions |
| `GBA.h` | Main console class, LoadROM, Step, SaveGame, peripheral access |
| `GBAMemory.h` | Memory class with Read32/Write32, MMIO routing to peripherals |
| `IORegs.h` | I/O register definitions (DISPCNT, VCOUNT, etc.) |
| `PPU.h` | Graphics subsystem, framebuffer access, render state |
| `APU.h` | Audio subsystem, channel mixing, DMA audio path |
| `SaveType.h` | Enum: EEPROM, SRAM for game save detection |
| `CheatManager.h` | Cheat code parsing and injection |
| `ROMMetadataAnalyzer.h` | Game detection database, metadata struct |
| `M4AEngine.h` | Music engine interpreter for game soundtracks |
| `Fuzzer.h` | Fuzzing harness interface |

#### Key Architectural Patterns
- **Modular subsystems:** CPU, PPU, APU, Memory are decoupled via member pointers
- **Step-based execution:** `GBA::Step()` returns cycles, used by host timing loop
- **MMIO routing:** Memory writes at I/O addresses dispatch to PPU/APU/Timers
- **Debug support:** Breakpoints, single-step, register inspection for tooling
- **HLE BIOS:** Auto-detection enables execution without real BIOS image

---

### 2.2 PLAYSTATION 1 (PS1)

**Status:** Full implementation  
**CPU:** R3000A (32-bit MIPS ISA with delay slots)  
**Core Clock:** 33.87 MHz

#### Source Files (src/emulator/ps1/)
| File | Purpose |
|------|---------|
| `R3000A.cpp` | MIPS CPU core with all instruction types, delay slot emulation, load delay |
| `PS1.cpp` | Main emulator orchestrator, subsystem initialization, lifecycle |
| `PS1Memory.cpp` | Physical address space (BIOS, DRAM, SRAM, I/O, cache control) |
| `PS1GPU.cpp` | Graphics processor: primitive rasterization, VRAM, command queue |
| `PS1SPU.cpp` | Sound processor: 24 ADPCM channels, reverb, CDDA mixing |
| `PS1DMA.cpp` | DMA controller: 7 channels, VRAM/CDROM/SPU transfers, linked lists |
| `PS1Timer.cpp` | Count registers: H-blank, V-blank, clock dividers |
| `InterruptController.cpp` | IRQ arbiter: line/edge triggers, enable/pending separation |
| `GTE.cpp` | Geometry Transform Engine (COP2): matrix/vector math hardware |
| `PS1Controller.cpp` | Pad interface, button states, rumble protocol |
| `CDROM.cpp` | CD-ROM controller: sector reads, mode control, error codes |
| `PS1MDEC.cpp` | MPEG decoder hardware for FMV playback |
| `PS1HleBios.cpp` | High-level BIOS stubs for COP2 context, exception handlers |
| `Fuzzer.cpp` | Fault injection (shared with GBA) |
| `Logger.cpp` | Diagnostic logging |

#### Header Files (include/emulator/ps1/)
| File | Purpose |
|------|---------|
| `R3000A.h` | MIPS CPU public interface, 32 registers, load/branch delay state |
| `PS1Constants.h` | Memory layout, IRQ numbers, coprocessor opcodes, magic values |
| `PS1Memory.h` | Physical memory class, Read32/Write32, MMIO routing, cache control |
| `PS1GPU.h` | Graphics subsystem, VRAM, framebuffer access, command queue |
| `PS1SPU.h` | Sound subsystem, 24 channels, mixing, sample rate conversion |
| `PS1DMA.h` | DMA controller, 7 channels (MDec, CDROM, Audio, GPU, OTC, SPU, GPU2) |
| `PS1Timer.h` | Count, Div, Mode registers for H-sync/V-sync/prescaler |
| `PS1Controller.h` | Pad reading, button state machine, rumble support |
| `InterruptController.h` | IRQ status/mask, edge/level detection, priority ordering |
| `GTE.h` | Geometry Transform Engine registers and vector/matrix operations |
| `CDROM.h` | CD-ROM controller, sectors, error codes, modes |
| `PS1MDEC.h` | MPEG decoder, DCT, quantization, frame buffer |
| `PS1HleBios.h` | System calls (SYS_PRINT, SYS_LOAD, etc.) |

#### Key Architectural Patterns
- **Subsystem interconnection:** CPU calls Memory, which dispatches MMIO to GPU/SPU/DMA
- **Delay slot emulation:** R3000A tracks branch/load delay state across instruction boundaries
- **DMA arbitration:** InterruptController and DMA share bus for priority resolution
- **Timing foundation:** PS1Timer delivers H/V-sync IRQs, drives audio/GPU cadence
- **HLE BIOS:** System call dispatch for boot sequencing (load, CPU type, etc.)

---

### 2.3 NINTENDO SWITCH (Preliminary)

**Status:** Skeleton/framework (not fully implemented)  
**CPU Cores:** ARM A57/A53 heterogeneous cluster  
**GPU:** NVIDIA Maxwell (Tegra X1)

#### Source Files (src/emulator/switch/)
| File | Purpose |
|------|---------|
| `SwitchEmulator.cpp` | Main emulator class, lifecycle, reset |
| `CpuCore.cpp` | ARM CPU core abstraction for heterogeneous cores |
| `GpuCore.cpp` | NVIDIA Maxwell GPU core abstraction |
| `MemoryManager.cpp` | Virtual address translation, TLB simulation, MMU |
| `ServiceManager.cpp` | HAL service dispatch (audio, display, filesystem, etc.) |

#### Header Files (include/emulator/switch/)
| File | Purpose |
|------|---------|
| `SwitchEmulator.h` | Main Switch emulator class interface |
| `CpuCore.h` | ARM CPU public interface for big/little cores |
| `GpuCore.h` | Maxwell GPU public interface |
| `MemoryManager.h` | Virtual→physical address mapping, page tables |
| `ServiceManager.h` | HAL service registration and dispatch |

#### Status Note
Switch emulation is in **architecture phase**. No full CPU instruction set implementation yet.

---

### 2.4 WINDOWS x86-64 (Experimental)

**Status:** Skeleton/framework (for testing x86 emulation patterns)

#### Source Files (src/emulator/windows/)
| File | Purpose |
|------|---------|
| `WindowsEmulator.cpp` | Main class, lifecycle, process loading |
| `X86_64Core.cpp` | x86-64 instruction decoder/executor (limited) |
| `WinMemory.cpp` | Windows virtual memory emulation, page protection |
| `WinProcess.cpp` | Process creation, DLL loading, thread management |
| `WinAPILayer.cpp` | Win32 API stubs (CreateFileA, ReadFile, etc.) |

#### Header Files (include/emulator/windows/)
| File | Purpose |
|------|---------|
| `WindowsEmulator.h` | Main Windows emulator class |
| `X86_64Core.h` | x86-64 CPU public interface |
| `WinMemory.h` | Virtual memory manager |
| `WinProcess.h` | Process and DLL management |
| `WinAPILayer.h` | Win32 API dispatch layer |

#### Status Note
Windows emulation is **experimental infrastructure**. Focus is architectural patterns, not feature completeness.

---

## 3. SHARED INFRASTRUCTURE (src/emulator/common/ & include/emulator/common/)

### 3.1 Core Shared Components

#### Logger (Logging & Diagnostics)
| File | Purpose |
|------|---------|
| `Logger.h` | Singleton logger with category-based filtering, callback dispatch |
| `Logger.cpp` | LogFmt implementation, thread-safe entry buffering |
| `Logger_old.cpp` | Legacy implementation (retained for compatibility) |

**Features:**
- Macros: `AIO_LOG()`, `AIO_LOG_INFO()`, `AIO_LOG_DEBUG()`, `AIO_LOG_WARN()`, `AIO_LOG_ERROR()`, `AIO_LOG_FATAL()`
- Categories: GBA, PS1, CPU, Memory, GPU, Audio, DMA, Interrupt, etc.
- Coverage exclusion: All logger calls auto-excluded from lcov coverage reports
- Thread-safe: Uses `std::mutex` for multi-threaded test harnesses

#### Fuzzer (Fault Injection & Testing)
| File | Purpose |
|------|---------|
| `Fuzzer.h` | Fuzzing harness for targeted fault injection |
| `Fuzzer.cpp` | Loop detection, PC history, iteration tracking |
| `Fuzzable.h` | Interface for components that can be fuzzed |
| `Loggable.h` | Mixin for subsystems with logging capability |

**Features:**
- Detects infinite loops (stall detection in GBA, PS1)
- Tracks PC history (1024 entries) for pattern detection
- Supports iterative fault injection campaigns

#### 3.2 Architectural Abstractions (Planned, Track A)
From end-to-end-emulator-program-roadmap.md:
- **Shared scheduler & event queue model** — not yet implemented
- **Standardized memory map + MMIO register abstraction** — being implemented per-console
- **Unified interrupt controller** — partially shared (different by console)
- **Save-state framework** — per-console, not yet unified
- **Trace and replay tooling** — in progress (Logger foundation is in place)

---

## 4. TEST SUITE ORGANIZATION

### 4.1 Test Files (tests/)

#### GBA Tests (7 files)
| Test File | Coverage |
|-----------|----------|
| `CPUTests.cpp` | ARM7TDMI instruction set: data processing, loads, stores, branches, calls |
| `PPUTests.cpp` | Graphics processor: modes, scanline rendering, affine transforms |
| `APUTests.cpp` | Audio processor: channels, DMA, mixing |
| `MemoryMapTests.cpp` | Address space: ROM, EWRAM, I/O routing, open bus |
| `EEPROMTests.cpp` | Save type detection and EEPROM emulation |
| `DMATests.cpp` | DMA controller functionality |
| `GbaTimingTests.cpp` | Cycle accuracy, interrupt timing, PPU/APU sync |

#### PS1 Tests (14 files)
| Test File | Coverage |
|-----------|----------|
| `PS1CPUTests.cpp` | R3000A instruction set: ALU, branches, delay slots, load delay |
| `PS1GPUTests.cpp` | GPU primitive rasterization, VRAM addressing |
| `PS1SPUTests.cpp` | SPU ADPCM decoding, mixing, envelope |
| `PS1DMATests.cpp` | DMA 7 channels, transfers, linked lists |
| `PS1ControllerTests.cpp` | Pad protocol, button states |
| `PS1TimerTests.cpp` | Count/Div/Mode, H-sync/V-sync, IRQ generation |
| `PS1InterruptTests.cpp` | IRQ status/enable, edge/level, priorities |
| `PS1GTE Tests.cpp` | Geometry Transform Engine, matrix/vector ops |
| `PS1MemoryTests.cpp` | Physical memory layout, MMIO routing |
| `PS1IntegrationTests.cpp` | Multi-subsystem integration (boot/gameplay) |
| `BIOSTests.cpp` | HLE BIOS stubs, system calls |
| `DMATimingTests.cpp` | DMA timing relative to other subsystems |
| `AudioCorruptionTests.cpp` | Audio edge cases, CDDA handling |
| `GraphicsCorruptionTests.cpp` | GPU edge cases, vertex overflow |

#### Platform-Agnostic Tests (6 files)
| Test File | Coverage |
|-----------|----------|
| `LoggerTests.cpp` | Logger categories, callbacks, thread safety |
| `InputLogicTests.cpp` | Pad input logic, rumble protocol |
| `BootTest.cpp` | Multi-console boot sequence |
| `ROMMetadataTests.cpp` | Game detection, save type inference |
| `ScreenMirrorTests.cpp` | AirPlay/Miracast integration |
| `QssValidator.cpp` | QSS stylesheet validation (GUI, not emulator) |

#### Test Framework
- **Testing Library:** Google Test (gtest) with CMake integration
- **Organization:** Each test class inherits `::testing::Test`, uses SetUp/TearDown
- **Helpers:** PPUTestHelper.h in tests/support/ provides rendering utilities
- **Coverage:** Full lcov integration with Makefile targets

### 4.2 Test Infrastructure (tests/support/)
| File | Purpose |
|------|---------|
| `PPUTestHelper.h` | GPU rendering test utilities, golden frame comparison |

---

## 5. DOCUMENTATION (docs/emulation/)

### 5.1 Design Documentation

| Document | Purpose |
|----------|---------|
| `end-to-end-emulator-program-roadmap.md` | Phased program for factory-accurate emulation across 4 waves (8-bit through modern systems) |
| `factory-accuracy-emulator-playbook.md` | Methodology: source tiers, citation standards, confidence labels, deviation tracking |
| `major-console-spec-sheets.md` | Technical specs for each implemented console (clocks, bus widths, memory layout) |

### 5.2 Key Roadmap Highlights

**Phase 0 (Setup):** Repository structure, source-tier policy, determinism baseline  
**Phase 1 (Shared Infrastructure):** Scheduler, memory map, interrupt controller, common test harness  
**Phase 2 (Wave 1):** NES, GB/C, SNES, Genesis, Master System, Game Gear, Atari, PC Engine, etc.  
**Phase 3 (Wave 2):** N64, PlayStation 1, Dreamcast, etc.  
**Phase 4 (Wave 3):** Xbox, GameCube, PS2, etc.  

---

## 6. BUILD SYSTEM (cmake/CMakeLists.txt)

### 6.1 Build Configuration

```cmake
cmake_minimum_required(VERSION 3.16)
project(AIOServer VERSION 0.1.0 LANGUAGES CXX)

# Settings
- C++ standard: C++20
- Output: build/bin/ (binaries), build/lib/ (libraries)
- Autogen: build/generated/autogen/
- ccache: Enabled if available for incremental builds
```

### 6.2 Dependencies

| Library | Purpose |
|---------|---------|
| Qt6 | Main GUI (Widgets, WebEngineWidgets, Multimedia) |
| SDL2 | Input handling and display buffer management |
| OpenSSL | AirPlay HAP pairing and encryption |
| libcurl | Web requests (YouTube, metadata lookup) |
| Google Test | Unit and integration testing |
| VideoToolbox | H.264 hardware decode (macOS only) |

### 6.3 Emulator Build Targets

Emulator core libraries are compiled as **part of the main executable**, not separate libraries (as of current CMakeLists.txt structure).

**Compilation flow:**
1. Source files in `src/emulator/{gba,ps1,common}/` → compiled with -std=c++20
2. Headers from `include/emulator/` included (recursive directories)
3. Linked with Qt6, SDL2, OpenSSL, libcurl
4. Final executable: `build/bin/AIOServer`

### 6.4 Test Targets

Tests are configured via CMake's `enable_testing()` and CTest:

```bash
cd build/generated/cmake && ctest --output-on-failure
```

Each test file becomes a target:
- `CPUTests` → `build/bin/CPUTests`
- `PS1CPUTests` → `build/bin/PS1CPUTests`
- etc.

See Makefile for convenience wrappers (`make build`, `make test`, `make coverage`).

---

## 7. CODE ARCHITECTURE PATTERNS

### 7.1 Emulator Class Design

All emulator main classes follow a **composition pattern**:

```cpp
class GBA {
  std::unique_ptr<ARM7TDMI> cpu;
  std::unique_ptr<GBAMemory> memory;
  std::unique_ptr<PPU> ppu;
  std::unique_ptr<APU> apu;
  
  int Step();           // Execute one instruction/cycle
  bool LoadROM(path);   // Load game cartridge
  void Reset();         // Cold boot
};

class PS1 {
  std::unique_ptr<R3000A> cpu;
  std::unique_ptr<PS1Memory> memory;
  std::unique_ptr<PS1GPU> gpu;
  std::unique_ptr<PS1SPU> spu;
  std::unique_ptr<PS1DMA> dma;
  // ... more peripherals
  
  int Step();           // Execute one instruction/cycle
  bool LoadBIOS(path);
  bool LoadDisc(path);
  void Reset();
};
```

### 7.2 Memory Class Design

All emulators use a **centralized memory class** that:
1. Holds physical address space
2. Routes MMIO writes to peripherals
3. Provides Read32/Write32 to CPU
4. Tracks open-bus values for unmapped reads

```cpp
class GBAMemory {
  uint32_t Read32(uint32_t addr);
  void Write32(uint32_t addr, uint32_t val);
  // MMIO dispatch to PPU/APU on writes
};

class PS1Memory {
  uint32_t Read32(uint32_t addr);
  void Write32(uint32_t addr, uint32_t val);
  // MMIO dispatch to GPU/SPU/DMA on writes
};
```

### 7.3 CPU Class Design

CPU classes expose:
- Register getters/setters
- PC management
- Instruction execution (`int Step()`)
- Debug helpers (breakpoints, single-step)

```cpp
class ARM7TDMI {
  int Step();                        // Execute one instruction
  uint32_t GetRegister(int reg);
  void SetRegister(int reg, uint32_t val);
  uint32_t GetPC() const;
  bool IsThumbMode() const;
};

class R3000A {
  int Step();                        // Execute one instruction
  uint32_t GetRegister(uint32_t idx);
  void SetRegister(uint32_t idx, uint32_t val);
  uint32_t GetPC() const;
};
```

### 7.4 Peripheral Class Design

Peripherals (PPU, APU, GPU, SPU, DMA, etc.) expose:
- Init/Reset methods
- Register read/write handlers (called by Memory class)
- Public accessors for high-level queries (framebuffer, sound data)

```cpp
class PPU {
  uint16_t Read16(uint32_t addr_in_io_space);
  void Write16(uint32_t addr_in_io_space, uint16_t val);
  const uint16_t* GetFramebuffer() const;
  uint32_t GetDisplayWidth() const;
};

class PS1GPU {
  uint32_t Read32(uint32_t addr);
  void Write32(uint32_t addr, uint32_t val);
  const uint16_t* GetFramebuffer() const;
};
```

### 7.5 Logging Integration

Logging is **opt-in per subsystem**. Some classes inherit `Common::Loggable`:

```cpp
class PS1Memory : public Common::Loggable { };
class R3000A : public Common::Loggable { };
class PS1GPU : public Common::Loggable { };
```

Usage in implementation:
```cpp
AIO_LOG_DEBUG("CPU", "Executing instruction 0x%08X at PC 0x%08X", instr, pc);
AIO_LOG_WARN("Memory", "Invalid access at 0x%08X", addr);
```

### 7.6 Fuzzing Integration

Subsystems that support fuzzing inherit `Common::Fuzzable` and implement the fuzzing interface. Fuzzer component runs iterative fault injection.

---

## 8. EMULATOR LIFECYCLE

All emulators follow this step-based execution model:

```cpp
// Initialization
GBA gba;
gba.LoadROM("game.gba");
gba.Reset();

// Execution loop (in GUI)
while (emulation_running) {
  int cycles = gba.Step();  // Execute one instruction
  total_cycles += cycles;
  
  // Handle timing/sync
  if (total_cycles >= sync_point) {
    render_frame();
    handle_audio();
  }
}

// Cleanup
gba.SaveGame();
```

---

## 9. FILE INVENTORY BY SUBSYSTEM

### GBA Implementation (11 files)
```
src/emulator/gba/
├── ARM7TDMI.cpp       (CPU core)
├── GBA.cpp            (Main class)
├── GBAMemory.cpp      (Address space)
├── PPU.cpp            (Graphics)
├── APU.cpp            (Audio)
├── CheatManager.cpp   (Cheats)
├── ROMMetadataAnalyzer.cpp (Game detection)
├── M4AEngine.cpp      (Music engine)
├── Fuzzer.cpp         (Fault injection)
└── Logger.cpp         (Diagnostics)

include/emulator/gba/
├── ARM7TDMI.h
├── ARM7TDMIConstants.h
├── ARM7TDMIHelpers.h
├── GBA.h
├── GBAMemory.h
├── IORegs.h
├── PPU.h
├── APU.h
├── SaveType.h
├── CheatManager.h
├── ROMMetadataAnalyzer.h
├── M4AEngine.h
└── Fuzzer.h
```

### PS1 Implementation (14 files)
```
src/emulator/ps1/
├── R3000A.cpp         (CPU core)
├── PS1.cpp            (Main class)
├── PS1Memory.cpp      (Address space)
├── PS1GPU.cpp         (Graphics)
├── PS1SPU.cpp         (Audio)
├── PS1DMA.cpp         (DMA controller)
├── PS1Timer.cpp       (Timers)
├── InterruptController.cpp (IRQ arbiter)
├── GTE.cpp            (Geometry Transform Engine)
├── PS1Controller.cpp  (Pad interface)
├── CDROM.cpp          (CD-ROM controller)
├── PS1MDEC.cpp        (MPEG decoder)
├── PS1HleBios.cpp     (HLE BIOS)
└── Logger.cpp         (Diagnostics)

include/emulator/ps1/
├── R3000A.h
├── PS1Constants.h
├── PS1Memory.h
├── PS1GPU.h
├── PS1SPU.h
├── PS1DMA.h
├── PS1Timer.h
├── PS1Controller.h
├── InterruptController.h
├── GTE.h
├── CDROM.h
├── PS1MDEC.h
└── PS1HleBios.h
```

### Common Infrastructure (3 files)
```
src/emulator/common/
├── Logger.cpp
├── Logger_old.cpp
└── Fuzzer.cpp

include/emulator/common/
├── Logger.h
├── Fuzzer.h
├── Fuzzable.h
└── Loggable.h
```

### Switch Framework (5 files)
```
src/emulator/switch/
├── SwitchEmulator.cpp
├── CpuCore.cpp
├── GpuCore.cpp
├── MemoryManager.cpp
└── ServiceManager.cpp

include/emulator/switch/
├── SwitchEmulator.h
├── CpuCore.h
├── GpuCore.h
├── MemoryManager.h
└── ServiceManager.h
```

### Windows Framework (5 files)
```
src/emulator/windows/
├── WindowsEmulator.cpp
├── X86_64Core.cpp
├── WinMemory.cpp
├── WinProcess.cpp
└── WinAPILayer.cpp

include/emulator/windows/
├── WindowsEmulator.h
├── X86_64Core.h
├── WinMemory.h
├── WinProcess.h
└── WinAPILayer.h
```

---

## 10. COMPLETE FILE DESCRIPTIONS

### GBA CPU & Core
| File | Type | LOC | Description |
|------|------|-----|-------------|
| `ARM7TDMI.h` | Header | ~150 | 16 general-purpose registers (R0-R15), CPSR/SPSR, PSR flags, condition codes, instruction pipeline |
| `ARM7TDMI.cpp` | Source | ~1500 | Full 32-bit ARM ISA (MOV, ADD, SUB, AND, OR, XOR, LSL, LSR, ASR, ROR, LDR, STR, BL, BX, SWI) + Thumb (16-bit) mode |
| `ARM7TDMIConstants.h` | Header | ~80 | CPU mode bits (User, FIQ, IRQ, Supervisor, Abort, Undefined, System), exception vectors, PSR bits |
| `ARM7TDMIHelpers.h` | Header | ~100 | Bit manipulation helpers: rotation, sign extension, condition evaluation |

### GBA Memory & Peripherals
| File | Type | LOC | Description |
|------|------|-----|-------------|
| `GBA.h` | Header | ~100 | Main GBA class: LoadROM, Reset, Step, SaveGame, GetCPU/PPU/APU/Memory |
| `GBA.cpp` | Source | ~500 | Orchestration: subsystem init, step loop, cycle accounting, stall detection |
| `GBAMemory.h` | Header | ~120 | Memory space: Read32/Write32, MMIO routing, save file handling, HLE BIOS initialization |
| `GBAMemory.cpp` | Source | ~800 | Physical layout: BIOS (16KB), EWRAM (256KB), IWRAM (32KB), ROM, VRAM, OAM, Palette, I/O; EEPROM/SRAM save types |
| `IORegs.h` | Header | ~50 | I/O register address definitions: DISPCNT, VCOUNT, TMCNT, etc. |
| `PPU.h` | Header | ~100 | Graphics: 5 rendering modes (Mode 0-5), sprite layer, affine backgrounds, framebuffer access |
| `PPU.cpp` | Source | ~1200 | Scanline rendering: OBJ-BG prioritization, mosaic, blending, affine matrix math |
| `APU.h` | Header | ~80 | Audio: 4 square waves + noise, volume, duty cycle, DMA audio channel |
| `APU.cpp` | Source | ~600 | Sound synthesis: envelope, sweep, mixing, DMA sample rate |
| `SaveType.h` | Header | ~20 | Enum: NONE, EEPROM, SRAM for game detection |
| `CheatManager.h` | Header | ~60 | Cheat code parsing and injection |
| `CheatManager.cpp` | Source | ~300 | Game Genie/Action Replay code decoder |
| `ROMMetadataAnalyzer.h` | Header | ~80 | Game database: ROM header parsing, title, code, region, save type inference |
| `ROMMetadataAnalyzer.cpp` | Source | ~400 | Game detection logic: CRC matching, boot state setup |
| `M4AEngine.h` | Header | ~50 | Music engine interpreter (Sappy's M4A format) |
| `M4AEngine.cpp` | Source | ~300 | M4A song playback: note scheduling, envelope, mixing |

### PS1 CPU, Memory & Core
| File | Type | LOC | Description |
|------|------|-----|-------------|
| `R3000A.h` | Header | ~100 | MIPS R3000A: 32 GPR, HI/LO, delays slots, load delay pipeline |
| `R3000A.cpp` | Source | ~2000 | MIPS instruction set: I-type, R-type, J-type, exception handling, load/branch delay |
| `PS1Constants.h` | Header | ~80 | MIPS opcodes, register names, IRQ numbers, memory layout constants |
| `PS1.h` | Header | ~100 | Main PS1 class: LoadBIOS, LoadDisc, InitHLE, Step, Reset |
| `PS1.cpp` | Source | ~400 | Orchestration: subsystem init, lifecycle, HLE BIOS entry points |
| `PS1Memory.h` | Header | ~120 | Memory space: Read8/16/32, Write8/16/32, MMIO routing to GPU/SPU/DMA |
| `PS1Memory.cpp` | Source | ~900 | Physical layout: BIOS (512KB), DRAM (2MB), I/O (MMIO), cache control region |

### PS1 Graphics & Audio
| File | Type | LOC | Description |
|------|------|-----|-------------|
| `PS1GPU.h` | Header | ~100 | Graphics processor: VRAM, framebuffer format, command queue, rasterization |
| `PS1GPU.cpp` | Source | ~1500 | Primitive rendering: triangles, quads, lines, fill, blending, texturing, z-buffer |
| `PS1SPU.h` | Header | ~80 | Sound processor: 24 ADPCM channels, voice/noise generators, reverb, CDDA mixing |
| `PS1SPU.cpp` | Source | ~800 | ADPCM decoding, channel mixing, envelope DSP, sample rate conversion |

### PS1 DMA, Interrupts, Timers
| File | Type | LOC | Description |
|------|------|-----|-------------|
| `PS1DMA.h` | Header | ~100 | DMA controller: 7 channels (MDec, CDROM, Audio, GPU, OTC, SPU, GPU2), linked lists |
| `PS1DMA.cpp` | Source | ~700 | DMA transfers: block copy, chain mode, interrupt on completion |
| `InterruptController.h` | Header | ~60 | IRQ arbiter: edge/level detection, pending/enable separation, status register |
| `InterruptController.cpp` | Source | ~300 | IRQ dispatch, masking, priority resolution |
| `PS1Timer.h` | Header | ~60 | Count, Div (prescaler), Mode registers per timer |
| `PS1Timer.cpp` | Source | ~400 | Count-up, H-sync/V-sync gating, IRQ on reload, div preload |

### PS1 Specialized Hardware
| File | Type | LOC | Description |
|------|------|-----|-------------|
| `GTE.h` | Header | ~80 | Geometry Transform Engine (COP2): matrix/vector registers, rotation matrix, perspective division |
| `GTE.cpp` | Source | ~800 | GTE operations: multiply, inverse, clipping plane, RTPT (rotation translate perspective transform) |
| `PS1Controller.h` | Header | ~60 | Pad interface, button state machine, rumble support |
| `PS1Controller.cpp` | Source | ~300 | Pad read protocol, pressure sensors, rumble motor control |
| `CDROM.h` | Header | ~80 | CD-ROM controller, sector reads, modes (Audio, XA), error codes |
| `CDROM.cpp` | Source | ~600 | CD-ROM emulation: seek/read commands, disc image parsing, error reporting |
| `PS1MDEC.h` | Header | ~60 | MPEG decoder hardware for FMV |
| `PS1MDEC.cpp` | Source | ~400 | DCT, quantization, inverse transform, frame buffer output |
| `PS1HleBios.h` | Header | ~40 | System call stubs (SYS_LOAD, SYS_PRINT, SYS_EXEC, etc.) |
| `PS1HleBios.cpp` | Source | ~200 | HLE BIOS dispatch, context setup for boot, game execution |

### Shared Infrastructure
| File | Type | LOC | Description |
|------|------|-----|-------------|
| `Logger.h` | Header | ~80 | Singleton logger, AIO_LOG_* macros, categories, callback dispatch |
| `Logger.cpp` | Source | ~300 | Thread-safe logging, entry buffering, format support |
| `Fuzzer.h` | Header | ~40 | Fuzzing harness interface |
| `Fuzzer.cpp` | Source | ~200 | Loop detection via PC history, iteration counting |
| `Loggable.h` | Header | ~20 | Mixin base class for subsystems with logging |
| `Fuzzable.h` | Header | ~20 | Mixin base class for components fuzzable by Fuzzer |

---

## 11. CONSOLE COMPARISON MATRIX

| Feature | GBA | PS1 | Switch | Windows |
|---------|-----|-----|--------|---------|
| **Status** | Full | Full | Skeleton | Skeleton |
| **CPU** | ARM7TDMI 32-bit | R3000A MIPS | ARM A57/A53 | x86-64 |
| **Clock** | 16.78 MHz | 33.87 MHz | 1-2 GHz | N/A |
| **RAM** | 32 KB IWRAM | 2 MB DRAM | 4 GB | N/A |
| **ROM/Media** | Cartridge | CD-ROM | Game Card/DL | PE/DLL |
| **GPU** | Software (PPU) | Hardware (GPU) | NVIDIA Maxwell | NVIDIA |
| **Audio** | 4ch synth (APU) | 24ch ADPCM (SPU) | HW mixer | OS |
| **Save Types** | EEPROM/SRAM | BIOS/Memory Card | NAND | NTFS |
| **Debug Support** | Breakpoints, Single-step | Breakpoints, Single-step | TBD | TBD |
| **Test Coverage** | 7 test modules | 14 test modules | 0 | 0 |
| **Test Files** | CPUTests, PPUTests, APUTests, MemoryMapTests, EEPROMTests, DMATests, GbaTimingTests | PS1CPUTests, PS1GPUTests, PS1SPUTests, PS1DMATests, PS1ControllerTests, PS1TimerTests, PS1InterruptTests, PS1GTETests, PS1MemoryTests, PS1IntegrationTests, BIOSTests, DMATimingTests, AudioCorruptionTests, GraphicsCorruptionTests | None | None |

---

## 12. KNOWN INFRASTRUCTURE GAPS (from Roadmap)

### Track A: Core Architecture (Planned)
- [ ] **Deterministic scheduler library** — Needed for coordinated multi-subsystem timing
- [ ] **Bus/arbitration framework** — DMA and CPU bus conflict resolution shared model
- [ ] **Unified interrupt framework** — Currently per-console; should abstract edge/level, pending/enable
- [ ] **Save-state framework** — With determinism checks (needed for replay/TAS)
- [ ] **Trace and replay tooling** — Logger foundation exists; needs replay harness

### Current Per-Console Implementations
- GBA: CPU, PPU, APU, Memory, no unified scheduler
- PS1: CPU, GPU, SPU, DMA, Timer, InterruptController, no unified scheduler
- Infrastructure gaps force each console to manage its own timing

---

## 13. QUICK REFERENCE: KEY ENTRY POINTS

### GBA
```cpp
#include "emulator/gba/GBA.h"
AIO::Emulator::GBA::GBA gba;
if (gba.LoadROM("game.gba")) {
  gba.Step();  // Execute one instruction
}
```

### PS1
```cpp
#include "emulator/ps1/PS1.h"
AIO::Emulator::PS1::PS1 ps1;
if (ps1.LoadBIOS("scph1001.bin") && ps1.LoadDisc("game.bin")) {
  ps1.Step();  // Execute one instruction
}
```

### Logging
```cpp
#include "emulator/common/Logger.h"
AIO_LOG_INFO("MyComponent", "Boot message");
AIO_LOG_WARN("CPU", "Invalid address: 0x%08X", addr);
```

---

## 14. SUMMARY STATISTICS

| Metric | Count |
|--------|-------|
| **Total Source Files** | 47 |
| **Total Header Files** | 47 |
| **Total Test Files** | 26 |
| **GBA Implementation Files** | 11 src + 11 hdr |
| **PS1 Implementation Files** | 14 src + 14 hdr |
| **Switch Framework Files** | 5 src + 5 hdr |
| **Windows Framework Files** | 5 src + 5 hdr |
| **Common Infrastructure Files** | 3 src + 4 hdr |
| **Design Documents** | 3 |
| **Subsystems Implemented** | 2 (GBA, PS1) |
| **Subsystems Sketched** | 2 (Switch, Windows) |
| **Test Modules** | 21 majors |

---

## 15. NEXT STEPS FOR DEEP DIVES

To understand specific subsystems in detail, read:

1. **GBA Architecture** → `include/emulator/gba/GBA.h` + `include/emulator/gba/GBAMemory.h`
2. **PS1 Architecture** → `include/emulator/ps1/PS1.h` + `include/emulator/ps1/PS1Memory.h`
3. **Test Strategy** → Any test file in `tests/`, e.g., `tests/CPUTests.cpp`
4. **Program Direction** → `docs/emulation/end-to-end-emulator-program-roadmap.md`
5. **Accuracy Standards** → `docs/emulation/factory-accuracy-emulator-playbook.md`

---

**End of Inventory**
