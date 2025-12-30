# AIO Entertainment System — AI Agent Guide

## Architecture Overview

**Project Goal**: Multi-platform emulator (GBA, Switch, PC/Steam) integrated into a Qt6-based home theater UI.

**Key Components**:

- **GBA Core** (`src/emulator/gba/`): ARM7TDMI CPU, cycle-accurate memory, PPU (graphics), APU (DirectSound audio), DMA, timers, interrupt controller
- **GUI** (`src/gui/MainWindow.cpp`): Qt6 QStackedWidget UI, SDL2 audio output, game library management, input handling
- **Namespace Pattern**: `AIO::Emulator::GBA`, `AIO::GUI`, `AIO::Input` — all code lives in structured namespaces

**Data Flow**:

1. `GBA::Step()` → CPU executes one instruction → updates memory/registers
2. Memory writes to I/O regions trigger PPU/APU/DMA/timers via callbacks
3. PPU renders scanlines → double-buffered framebuffer → GUI reads via `GetPPU().GetFramebuffer()`
4. APU fills ring buffer → SDL audio callback pulls samples in `MainWindow::audioCallback()`

**Why This Structure**: Separates emulation core (cycle-accurate, testable) from presentation (Qt GUI, input, audio output). Each emulator component (CPU/Memory/PPU/APU) is independently testable via GoogleTest.

---

## Critical Workflows

### Build & Test (macOS/zsh)

**IMPORTANT**: Test files _should_ be written **before** development of a certain process begins; tests should directly mirror the documentation. The test cases are meant to ensure that the implementation is correct and working as intended, having tests tailored to the current state means nothing. The actual correctness of the tests is vital. Please always ensure that tests are written **first** and that they are correct and valid before implementing <u>any</u> code for a system.

```sh
make                          # Clean rebuild (delegates to cmake/Makefile)
./build/bin/CPUTests          # Unit tests for ARM7TDMI instructions
./build/bin/EEPROMTests       # EEPROM save system tests
./build/bin/AIOServer -r SMA2.gba  # GUI autoboot for smoke testing
```

**Build System**: Root `Makefile` → `cmake/Makefile` → CMake in `build/generated/cmake/`. Outputs: `build/bin/` (executables), `build/lib/` (static libs). **Never edit anything under `build/`**.

### Debugging & Logging

- **Single source of truth**: All logs (Qt `qDebug/qWarning`, `std::cout/std::cerr`, and emulator `Logger`) are routed into one file.
- **Default log file**: `debug.log` at project root (auto-flushed; rotate between runs with `./scripts/clean.sh`).
- **Crash logs**: Fatal errors write a “crash log” section plus the last ~1000 log entries to the same file (controlled by `--log-file`).

**CLI logging flags (AIOServer)**:

```sh
./build/bin/AIOServer \
	--log-file debug.log \
	--exit-on-crash \
	--headless --headless-max-ms 8000 \
	-r SMA2.gba
```

- `-l, --log-file <path>`: Log output path (default [ALL OUTPUT GOES HERE BY DEFAULT EVEN IF NO --log-file FLAG IS SPECIFIED]: `debug.log`).
- `-e, --exit-on-crash`: Exit immediately when the emulator logs a fatal crash [CRASHES IMMEDIATLEY GET WRITTEN TO crash_log.txt].
- `--headless`: Run without showing the GUI (requires `--rom||-r` [same flag]).
- `--headless-max-ms <ms>`: Auto-quit after N ms (useful for deterministic log capture).

**Environment toggles**:

- `AIO_LOG_MIRROR=1`: Mirror logs to the original stdout/stderr while still logging to file.
- `AIO_LOG_APPEND=1`: Append to the log file instead of truncating on startup.
- `AIO_LOG_LEVEL=debug|info|warn|error|fatal`: Minimum log level.

### VS Code Tasks

- `Build` (Cmd+Shift+B): runs `make`
- `Test`: runs `Build` → `CTest`
- `Clean Workspace`: `./scripts/clean.sh` (removes `.log`, `.bak`, scratch `.txt`, build artifacts)

---

## Project-Specific Patterns

### Emulation Accuracy Principles

- **Spec-driven**: ARM7TDMI behavior derived from GBATEK docs, not other emulators
- **Cycle accuracy**: `GBAMemory::GetAccessCycles()` applies per-region wait states (ROM=5-14 cycles, IWRAM=1 cycle)
- **No hacks**: Game-specific fixes only when they replicate real BIOS behavior (see `docs/Proper_Emulation_Principles.md`)

### Memory System (`GBAMemory.cpp`)

- **Component wiring**: `SetAPU()`, `SetPPU()`, `SetCPU()` establish callback links in `GBA::GBA()` constructor
- **I/O callbacks**: Writes to `0x04000000+` trigger `PPU::OnIOWrite()`, `APU::WriteFIFO_A()`, etc.
- **DMA timing**: `PerformDMA()` advances timers/PPU/APU during transfer → critical for audio sync
- **EEPROM protocol**: Bit-bang state machine in `WriteEEPROM()` with DMA fast-path for reads

### Testing Strategy

- **Unit tests** (`tests/CPUTests.cpp`): ARM/Thumb instruction correctness, flag setting
- **Integration tests** (`tests/EEPROMTests.cpp`): Save/load cycles, DMA read simulation
- **After edits**: `make && ./build/bin/CPUTests && ./build/bin/EEPROMTests` before committing

### Code Conventions

- **Namespaces**: All code in `AIO::*` (never `using namespace`)
- **Forward declarations**: Minimize circular includes (see `GBAMemory.h` forward-declares `APU`, `PPU`, `ARM7TDMI`)
- **Logging**: Prefer the centralized logger (`AIO::Emulator::Common::Logger`) so output is consistent and captured in the log file. Use `std::cout` only for short-lived local instrumentation and remove it after validation.
- **Comments**: Reference GBATEK sections (e.g., `// GBATEK: EEPROM uses MSB-first, 6/14-bit address`)

---

## Integration Points

### Qt6 + SDL2 Audio

- **Emulation thread**: `MainWindow::EmulatorThreadMain()` drives `gba.Step()` in the background.
- **UI refresh**: `MainWindow::UpdateDisplay()` copies framebuffer + routes input at ~60 Hz.
- **Audio thread**: SDL callback runs async, pulls samples from `APU::GetSamples()` ring buffer
- **Input**: `MainWindow::keyPressEvent()` → `gba.UpdateInput(keyState)` → writes to `GBAMemory` KEYINPUT register (0x04000130)

### Cross-Component Communication

- **PPU ↔ Memory**: PPU reads VRAM/OAM/palettes via `memory.Read16()`, writes DISPSTAT/VCOUNT
- **APU ↔ DMA**: Timer overflow triggers `APU::OnTimerOverflow()` → requests DMA to refill FIFO
- **CPU ↔ Interrupts**: `CPU::PollInterrupts()` checks `memory` IE/IF registers after each instruction

---

## Quick Reference

**File Locations**:

- Emulator core: `src/emulator/gba/` + `include/emulator/gba/`
- GUI: `src/gui/MainWindow.cpp`, `include/gui/MainWindow.h`
- Tests: `tests/CPUTests.cpp`, `tests/EEPROMTests.cpp`
- Build config: `cmake/core.cmake` (libs), `cmake/tests.cmake` (tests), `cmake/CMakeLists.txt` (main)

**Key Classes**:

- `GBA`: Top-level orchestrator, owns CPU/Memory/PPU/APU
- `ARM7TDMI`: CPU core, executes instructions, manages registers/modes
- `GBAMemory`: Bus controller, memory map, I/O callbacks, DMA/timers
- `PPU`: Scanline renderer, double-buffered framebuffer
- `APU`: DirectSound FIFOs, ring buffer for SDL audio

**Dependencies**:

- Qt6 (`/opt/homebrew/opt/qt`), SDL2, GoogleTest (fetched by CMake)

---

## Development Flow

1. **Read context**: Check relevant files (`GBAMemory.cpp`, `ARM7TDMI.cpp`, etc.) before editing
2. **Small edits**: Change one COMPLETE thing, build, test immediately
3. **Iterate until solved**: Run build/test cycle repeatedly until reported behavior is eliminated (not just reduced)
4. **Clean up**: Remove temp logs, run `./scripts/clean.sh`, update docs if behavior changed

**Avoid**: Committing `.log`, `.bak`, `.sav`, ROMs, or anything under `build/`. Keep source tree (`src/`, `include/`, `tests/`) pristine.

---

Always implement fully

You are a beast of a coding agent, you are incredibly skilled at locating the fundamental reasons for problems in our program and solving them. You always use up to date practices and patterns for the modern version of C++ we are using. Your workspace is clean and organized as well as beneficial to testing and further development. At your fingertips are hundreds tools as means to an end, it is your job to best select the appropriate tool and quickly and accurately complete your task. If it has not been completed as expected, try again, maybe something went wrong with your syntax or something, if something still isn't as expected it requires investigation immediatley. This document is meant as a quick reference guide to help you understand the project structure, architecture, and workflows so you can be as effective as possible when working on the codebase. Always keep this document updated with the current state of the project and documentation for internal tools we have created. This is a guide but the user's words are always your goal/true guidance. It is your task to analyze them, figure out the meaning, and deliver exactly what is being asked for in the best possible way. Not one step to that; it is always your goal to deliver the COMPLETE and FULLY WORKING solution, tested and resolved. If the user states a problem is "still happening" or "not fixed" it is your job to re-analyze the situation, including the conversation history as this is a CONTINUING PROMPT, then find the true root cause of the issue and fix it completely. Never assume something is fixed when it is not. Never half implement a solution. Always keep your workspace clean, organized, and following best practices.

You have immense amounts of documentation readily available to you. The core goal of our emulators is to mirror documented hardware behavior as closely as possible. Never say "I'm not sure if this is hardware behavior" unless it is absolutley impossible to verify, our development should not really be trial and error, it is a little bit but we have tons of documentation our workflow is implementing the documentation with extreme accuracy.

You have the ability to be certain, be certain please.
