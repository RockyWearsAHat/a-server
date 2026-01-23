# AIO “Memory” (Living Codebase Notes)

This file is the **living memory** for the AIO Entertainment System repository.

Goal: keep a compact, accurate, _high-signal_ reference so that future work does **not** require repeatedly rescanning the entire tree.

> Model note: if you have a choice of model for documentation-only updates and want to reduce cost/latency, prefer **GPT-4.1** for updating this memory file. Use a stronger model for complex debugging/architecture changes.

## One-paragraph overview

AIO is a Qt6-based front-end hosting multiple emulator cores (notably a GBA emulator). The GBA subsystem is organized as a cycle-accurate core (CPU + memory + PPU + APU + DMA/timers/interrupts) wired together so that memory-mapped I/O writes trigger subsystem callbacks. The GUI and audio are presentation layers: Qt handles the UI, SDL2 provides audio output.

## Top-level layout

- `src/` — production source
  - `src/emulator/` — emulator cores
    - `src/emulator/gba/` — Game Boy Advance core
    - `src/emulator/switch/` — Switch-related code (implementation varies)
    - `src/emulator/common/` — shared emulator utilities
  - `src/gui/` — Qt UI (main window, rendering, emulator thread)
  - `src/input/` — input mapping and routing
  - `src/common/` — shared utilities/logging/helpers
  - `src/nas/`, `src/streaming/` — NAS/streaming features

- `include/` — public headers mirroring `src/` structure
- `tests/` — GoogleTest test suite
- `docs/` — project documentation (specs, principles, reports)
- `cmake/` — CMake modules
- `scripts/` — utilities (cleaning, analysis, test suite runner)

## Build system

- Root `Makefile` drives builds.
- CMake generation lives under `build/generated/cmake/`.
- Outputs:
  - `build/bin/` executables
  - `build/lib/` libraries
- Rule: **never edit anything under `build/`**.

## Key subsystems (GBA)

The core is wired so that **CPU execution** advances state and **memory writes** to I/O space trigger behavior.

Typical control/data flow:

1. `GBA::Step()` executes CPU work for a step (instruction / cycles).
2. Memory reads/writes go through the bus (`GBAMemory`), applying region wait states.
3. Writes to `0x04000000+` invoke I/O callbacks:
   - PPU register writes (`PPU::OnIOWrite`, VCOUNT/DISPSTAT/etc)
   - APU FIFO writes (DirectSound)
   - DMA/timer interactions
4. PPU renders scanlines → produces a framebuffer the GUI can display.
5. APU produces audio samples → SDL audio callback pulls from a ring buffer.

### Accuracy principles

- Use `docs/Proper_Emulation_Principles.md` as the project’s policy baseline.
- Prefer GBATEK-derived behavior; avoid relying on other emulator implementations.
- No game-specific hacks unless they replicate real BIOS/hardware behavior.

## Other subsystems

### NAS Server (`src/nas/`, `include/nas/`)

Network-attached storage server for serving ROMs and media to clients.

- Exposes a REST or socket-based API for browsing and streaming content
- Integrates with the GUI for local file management
- See `docs/NAS_Server.md` for protocol and configuration details

### Streaming (`src/streaming/`, `include/streaming/`)

Real-time video/audio streaming of emulator output to remote clients.

- Encodes framebuffer and audio for network transmission
- Low-latency focus for playable remote gaming
- Works in tandem with the NAS server for a "home theater" experience

### Qt GUI (`src/gui/`, `include/gui/`)

The presentation layer built on Qt6.

- Main window hosts the emulator viewport
- Emulator runs on a dedicated thread; GUI thread handles input and rendering
- Signals/slots used for thread-safe communication
- Stylesheets under `assets/qss/` for theming

## Tests

The `tests/` folder contains the primary correctness harness:

- `tests/CPUTests.cpp` — ARM/Thumb instruction correctness and flags
- `tests/EEPROMTests.cpp` — EEPROM save protocol and DMA read simulation
- `tests/DMATests.cpp` — DMA transfer behavior
- `tests/PPUTests.cpp` — PPU rendering/IO behavior
- `tests/MemoryMapTests.cpp` — memory map + access rules
- `tests/BIOSTests.cpp` — BIOS behavior
- `tests/BootTest.cpp` — boot-level scenarios
- `tests/InputLogicTests.cpp` — input behavior
- `tests/ROMMetadataTests.cpp` — ROM metadata parsing

Guideline: tests should mirror **documentation/spec**, not the current implementation.

## Timing & Performance

### Peripheral Batching

The GBA core batches peripheral updates for performance optimization:

- `PERIPHERAL_BATCH_CYCLES` in `GBA.h` controls batch size (now: 8 cycles, was 64)
- Cycles accumulate in `pendingPeripheralCycles` until threshold or CPU halt
- Cycles are flushed early when reading timing-sensitive registers (DISPSTAT, VCOUNT) — implemented by calling `FlushPendingPeripheralCycles()` in `GBAMemory::Read16()`
- **IMPORTANT:** PPU::Update() must use `ReadIORegister16Internal()` (not `Read16()`) to read IO registers to avoid infinite recursion with the flush mechanism
- **Rationale:** 8 cycles provides sufficient granularity while keeping performance acceptable. This change fixes SMA2 lag caused by stale DISPSTAT/VCOUNT reads.
- **Tradeoff:** Larger batches are still an option for performance, but 8 is a safe default to avoid timing-sensitive regressions

### PPU Color Effects

The PPU supports four blending modes (BLDCNT bits 6-7):

- **Mode 0:** None (no blending)
- **Mode 1:** Alpha blend between two layers (uses BLDALPHA EVA/EVB)
- **Mode 2:** Brightness increase / fade to white (uses BLDY EVY)
- **Mode 3:** Brightness decrease / fade to black (uses BLDY EVY)

Layer tracking via `layerBuffer[]`:

- 0-3: BG0-BG3
- 4: OBJ (sprites)
- 5: Backdrop (when no layer covers pixel)

**Known Issues:**

- ~~DKC intro fade: Verify `layerBuffer[]` correctly identifies backdrop pixels~~ FIXED
- Target selection: BLDCNT bits 0-5 select first target layers (note: semi-transparent OBJs always blend and do NOT require OBJ in firstTarget; they only require the underlying layer to be selected in secondTarget)

### Classic NES Series / NES-on-GBA ROMs

Games like "Classic NES Series: Donkey Kong" (OG-DK) run NES emulators on GBA hardware:

- **Stress-test timing accuracy** — inner emulator expects precise GBA timing
- **Solution:** Fix timing to match GBATEK spec, not use LLE BIOS as workaround
- These ROMs are excellent test cases for timing accuracy

## Logging and crash capture

- Default log target is `debug.log` at repo root.
- Prefer the centralized logger (`AIO::Emulator::Common::Logger`) so output is captured consistently.
- Environment toggles (if implemented):
  - `AIO_LOG_MIRROR=1` mirror to stdout/stderr
  - `AIO_LOG_APPEND=1` append instead of truncating
  - `AIO_LOG_LEVEL=debug|info|warn|error|fatal`
  - `AIO_TRACE_PPU_IO_WRITES=1` trace PPU register writes (BLDCNT, BLDY, etc.)
  - `AIO_TRACE_GBA_SPAM=1` verbose CPU/PPU tracing
  - `AIO_GBA_BIOS=/path/to/bios.bin` use LLE BIOS instead of HLE

## Workspace hygiene

- Use `./scripts/clean.sh` for routine cleanup.
- Default cleanup removes logs and build outputs but **keeps** saves and dumps.
- Aggressive cleanup (saves, dumps) should run **only after the user verifies** a problem is solved.

## Update protocol (when changing code)

Whenever you change behavior, update _at least_:

1. The relevant document(s) under `docs/` (or add a new one if needed).
2. The relevant test(s) under `tests/`.
3. This file **only where the summary/invariants changed** (don’t churn it).

## Changelog (curated)

- 2026-01-22: Added timing/performance section documenting peripheral batching, PPU color effects, and NES-on-GBA ROM requirements.- 2026-01-22: Added NAS/streaming/GUI subsystem sections to memory.md.
- 2026-01-22: Instruction suite moved into `.github/instructions/` and cleanup policy made safe-by-default.
