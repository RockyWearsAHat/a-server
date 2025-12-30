# Expand GBA Hardware-Accurate Tests (GBATEK-driven)

## Purpose

Expand the GBA emulator test suite from "mostly working" to **hardware-accurate** by encoding **GBATEK-defined behavior** as deterministic GoogleTest assertions.

### Known user-facing failures to target early

- **DKC**: enemies show incorrect colors (pink textures) and/or missing sprites in intro cutscene.
- **Metroid: Zero Mission**: audio bugs.
- **SMA2**: balloon shadow clipping into ocean.

These strongly implicate: **PPU OBJ rules (tile mapping/VRAM rules), windowing + blending + priority**, and **APU FIFO + DMA + timer scheduling**.

## Non-negotiable constraints

- **Spec-driven**: Use **GBATEK** as the source-of-truth; cross-check with Tonc / no$gba docs only when clarifying.
- **No game-specific hacks**: No ROM-ID conditionals, no per-title exceptions.
- **Tests first**: Implement tests before modifying emulator behavior.
- **Deterministic**: No GUI, no threads, no time-based sleeping; tests must be purely simulated.
- **Minimal harness**: Add the smallest helper layer needed to author many tests quickly.
- **No edits under build/**.

## Repo entry points (where to work)

- Unit tests: tests/\*.cpp
- Test build config: cmake/tests.cmake
- Unified runner: scripts/test_suite.py
- Emulator core (GBA): src/emulator/gba/** and include/emulator/gba/**

<ul>Note: scripts/test_suite.py auto-discovers and runs all built *Tests executables in build/bin (in addition to a few explicitly listed ones). Validate new suites with ctest as usual.</ul>

## Deliverables

1. A **test catalog** implemented as GoogleTests across PPU/DMA/Timers/IRQ/Memory/APU.
2. Minimal reusable test helpers under tests/support/ (or similar).
3. Enough coverage to validate and drive fixes for the 3 known failures.
4. A scalable structure that can grow toward **1000+ discrete hardware behaviors**.

## Test strategy (how to scale to 1000+ behaviors)

You are not trying to write 1000 gigantic tests.
You are trying to write **many small, orthogonal tests**, each asserting exactly one SPECIFIC hardware rule or behavior.

### Preferred test types

- **Register semantics tests**: IO register read/write behavior, bit masking, read-only bits, write-1-to-clear, side effects.
- **Timing-edge tests**: HBlank/VBlank boundaries, interrupt edge triggering, timer overflow exactness.
- **Golden pixel tests**: render a tiny scene and assert a handful of pixels (or stable per-scanline hash).
- **Golden PCM tests**: generate deterministic audio samples and compare to expected vectors.

### Avoid

- Large ROM-driven "integration" assertions inside GoogleTest (keep those as repro harnesses unless necessary).

## Minimal helper harness to implement first

Create helpers that make it easy to build tiny scenes.

### PPU helper requirements

- Ability to set IO registers (DISPCNT/BGCNT/BGHOFS/VOFS/WIN*/BLD* etc).
- Ability to write palettes, VRAM tiles, tilemaps, and OAM.
- Ability to step the emulator/PPU deterministically to:
  - Render a single scanline, or
  - Render a full frame (240x160)
- Ability to read framebuffer pixels deterministically.

### APU helper requirements

- Ability to configure sound registers.
- Ability to drive timers deterministically.
- Ability to push FIFO samples.
- Ability to capture N output samples deterministically.

## Phased execution order

### Phase 0: Make the suite runnable

- Verify that all unit-test executables built in cmake/tests.cmake are executed by your chosen runner.
  - If needed, extend scripts/test_suite.py to include PPUTests, DMATests, BIOSTests, MemoryMapTests, APUTests, ROMMetadataTests.

### Phase 1: PPU correctness (targets DKC + SMA2)

Implement tests in tests/PPUTests.cpp (and helpers) for:

#### (A) OBJ rendering rules (high priority)

Create tests asserting:

- 1D vs 2D OBJ tile mapping and tile index calculation.
- 4bpp vs 8bpp OBJ decode rules and palette bank selection.
- OBJ transparency: palette index 0 must be transparent.
- OBJ priority ordering vs BG layers and backdrop.
- OBJ affine parameter indexing, double-size affine bounds, and correct sampling.
- OBJ mosaic behavior (horizontal + vertical) and interaction with affine.

#### (B) Windowing (WIN0/WIN1/OBJWIN) (high priority, SMA2 shadow)

Create tests asserting:

- WIN0/WIN1 bounds behavior (including wrap cases where left > right, top > bottom).
- Window priority: WIN0 > WIN1 > OBJWIN > outside.
- WININ/WINOUT layer enable masks (BG0..BG3, OBJ, backdrop) are respected.
- "Color effects enable" mask inside/outside windows correctly gates blending.
- OBJWIN produces a mask (via OBJ-window mode) but does not draw pixels.

#### (C) Blending / brightness (high priority, SMA2 shadow)

Create tests asserting:

- Alpha blend math and clamping (EVA/EVB clamp 0..16, result clamp 0..31 per channel).
- Brightness increase/decrease math and clamping (BLDY clamp 0..16).
- BLDCNT target selection (first/second targets) is respected.
- Semi-transparent OBJ blending behavior exactly matches GBATEK.
- Window "effects enable" bit disables blending where masked.

#### (D) VRAM/OAM/Palette access semantics (high priority, DKC pink/missing sprites)

Create tests asserting:

- Byte/halfword/word write behavior to VRAM, OAM, and palette RAM matches GBATEK (including alignment and bus behavior).
- OBJ VRAM mirroring behavior for the 0x06018000 region is correct for reads/writes of multiple sizes.
- OAM update restrictions during rendering (visible vs HBlank/VBlank) match GBATEK.

### Phase 2: DMA + timers + IRQ edges (targets Metroid ZM audio stability)

Implement tests across tests/DMATests.cpp and tests/MemoryMapTests.cpp:

- DISPSTAT HBlank/VBlank/VCOUNT flags transition exactly at the documented boundaries.
- HBlank/VBlank IRQ triggers on edges (0→1), not continuously.
- IF is write-1-to-clear; IME gating rules are correct.
- DMA immediate / HBlank / VBlank / special timing triggers and repeat/reload semantics.
- DMA sound FIFO request behavior (request threshold, pending requests if DMA busy).
- Timer prescalers, reload, cascade, enable/disable edge behavior; timer IRQ correctness.

### Phase 3: APU DirectSound + GB PSG (square wave required)

Implement tests in tests/APUTests.cpp.

#### (A) DirectSound FIFO

- FIFO write ordering for 8/16/32-bit writes (byte order and packing).
- FIFO threshold request behavior and DMA refill semantics.
- Timer selection bits route FIFO A/B to the correct timers.
- Mixing/scaling rules (SOUNDCNT_H volumes, routing to L/R, clamp/saturation).
- SOUNDBIAS center and clamp behavior.
- Deterministic sample count: after N timer ticks, exactly N output samples are produced.

#### (B) GB PSG: Square wave channels (required)

The GBA includes legacy GB sound (PSG) hardware. Implement tests for:

Channel 1 (Square + sweep):

- Duty patterns (12.5%, 25%, 50%, 75%) generate correct waveform sequences.
- Frequency timer period mapping is correct.
- Length counter enable and decrement timing (frame sequencer).
- Envelope: period, direction, saturation behavior.
- Sweep: shift, direction, overflow/disable behavior, and exact tick timing.
- DAC enable rules: when DAC disabled, channel output forced to 0 and channel disabled.

Channel 2 (Square, no sweep):

- Duty patterns identical rules.
- Length + envelope correctness.

Global PSG rules:

- Master enable/disable behavior and register reset semantics.
- Mixing routing to left/right outputs via control regs.
- Volume scaling and clamping when mixed with DirectSound.

Note: Use GBATEK for the exact frame sequencer timing and register bit definitions; tests must encode the exact documented cadence.

(Optional but recommended, Phase 3b):

- Channel 3 wave RAM behavior, bank switching, output level.
- Channel 4 noise LFSR width mode and frequency mapping.

## Concrete test format (template)

For each behavior, encode:

- Setup: write specific registers/memory.
- Stimulus: step a known number of cycles/scanlines/timer ticks.
- Assertion: pixel/sample/flag matches expected result.

Suggested naming:

- PPU_Window_WIN0_Wraparound_LeftGreaterThanRight
- PPU_Blend_OBJ_SemiTransparent_RespectsTargets
- APU_FIFO_Request_Threshold_TriggersAt16Bytes
- PSG_Square_Duty_50Percent_WaveformPattern

## Acceptance criteria

- New tests are deterministic and pass on a clean build.
- Failures are actionable: each test points to one broken rule.
- The three known issues have at least one direct test each:
  - DKC sprites/color rules: OBJ mapping + VRAM write semantics + transparency.
  - SMA2 shadow: windowing + blending + priority.
  - Metroid ZM audio: FIFO threshold + DMA special + timer cadence + mixing.

## Notes about runner integration

- Your unified runner (scripts/test_suite.py) currently executes only a subset of GoogleTest binaries; you may extend it as part of Phase 0.
- Even if you don’t extend it immediately, ensure `ctest` from the real CMake build dir runs the new tests.
