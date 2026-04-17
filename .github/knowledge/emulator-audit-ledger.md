# Emulator Audit Ledger

Durable backlog for taking AIO Server from "many cores exist" to a defensible finished emulator product.

## Rules

- A green test run is not enough for sign-off.
- Launchable platforms are held to the highest standard first.
- Disabled tests, stub-only tests, and known unimplemented commands block any "lossless" claim.
- Add new platforms only after the current launchable surface is honest and stable.

## Status Key

- `Launchable`: available to the shell today.
- `Core present`: source exists, but not a finished product surface.
- `Test surface`: dedicated platform tests exist.
- `Lossless`: not yet assigned to any platform.

## Wave 1: Launchable Platforms

### Game Boy Advance

- Status: Launchable, broad unit coverage, not lossless.
- Evidence:
  - Remaining disabled tests are in `tests/DMATests.cpp` and `tests/PPUTests.cpp`.
  - ARM SWP/SWPB CPU tests are now enabled and passing.
  - Stub-only BIOS SWI coverage in `tests/CPUTests.cpp` and `src/emulator/gba/ARM7TDMI.cpp`.
  - TAS determinism support exists.
- Release blockers:
  1. Re-enable sound FIFO DMA timing validation.
  2. Re-enable OAM visible/HBlank timing validation.
  3. Replace stub/no-crash BIOS SWI acceptance with spec-backed assertions where behavior is defined.
  4. Audit remaining launchable BIOS/HLE behavior against GBATEK and ARM7TDMI docs.

### PlayStation

- Status: Launchable, strong subsystem coverage, not lossless.
- Evidence:
  - Dedicated CPU, GPU, DMA, GTE, SPU, timer, interrupt, controller, memory, and integration tests exist.
  - CDROM exists, but there is no dedicated PS1 CDROM test suite.
  - `CmdPlay()` is skeletal in `src/emulator/ps1/CDROM.cpp`.
  - TAS determinism support is missing.
- Release blockers:
  1. Add dedicated CDROM command/data/interrupt tests from psx-spx.
  2. Replace smoke-style interaction checks with stronger CPU<->DMA<->GPU<->CDROM<->SPU integration proofs.
  3. Add PS1 determinism/TAS layer support.
  4. Continue timer, DMA, and GPU edge-case auditing against psx-spx and MIPS docs.

### NES

- Status: Launchable, core/test surface present, not lossless.
- Evidence:
  - Dedicated CPU, PPU, cartridge, rendering, and determinism tests exist.
- Release blockers:
  1. Complete spec audit against authoritative 2A03/2C02 behavior sources.
  2. Confirm APU/audio correctness with dedicated timing-oriented evidence.
  3. Add runtime/headless verification evidence for representative ROMs.

### SNES

- Status: Launchable, core/test surface present, not lossless.
- Evidence:
  - Dedicated CPU, PPU, cartridge, and determinism tests exist.
- Release blockers:
  1. Complete spec audit against 65816/PPU/APU behavior references.
  2. Strengthen subsystem interaction evidence, especially audio/timing.
  3. Add runtime/headless verification evidence for representative ROMs.

### Game Boy / Game Boy Color

- Status: Launchable through the handheld-family wrapper, not lossless.
- Evidence:
  - Dedicated CPU, PPU, cartridge, and determinism tests exist.
- Release blockers:
  1. Complete LR35902/PPU/APU hardware-spec audit.
  2. Verify wrapper-level routing behavior between GB/GBC and GBA-family shell entry points.
  3. Add runtime/headless verification evidence for representative ROMs.

### Genesis

- Status: Launchable, not lossless.
- Evidence:
  - Dedicated CPU, VDP, cartridge, and determinism tests exist.
- Release blockers:
  1. Add or strengthen audio verification for YM2612/SN76489 behavior.
  2. Complete M68000/Z80/VDP spec audit.
  3. Add runtime/headless verification evidence for representative ROMs.

### Atari 2600

- Status: Launchable, not lossless.
- Evidence:
  - Dedicated combined platform test file exists.
  - No separate determinism evidence currently recorded.
- Release blockers:
  1. Split and deepen CPU/TIA/PIA timing coverage where needed.
  2. Add determinism/save-state evidence.
  3. Add runtime/headless verification evidence for representative ROMs.

## Wave 2: Core Present, Not Product-Ready

### Nintendo 64

- Status: Core present, not launchable.
- Evidence: CPU, cartridge, and RDP tests exist.
- Promotion blockers:
  1. Complete subsystem coverage for missing components and interactions.
  2. Add determinism/runtime verification.
  3. Wire product launch only after acceptance gates are real.

### GameCube

- Status: Core present, not launchable.
- Evidence: Gekko, Flipper, and memory tests exist.
- Promotion blockers:
  1. Strengthen subsystem depth beyond current basic tests.
  2. Add determinism/runtime verification.
  3. Wire product launch after verification.

### Saturn

- Status: Core present, not launchable.
- Evidence: CPU, VDP, and memory tests exist.
- Promotion blockers:
  1. Expand subsystem coverage.
  2. Add determinism/runtime verification.
  3. Wire product launch after verification.

### Dreamcast

- Status: Core present, not launchable.
- Evidence: CPU, GPU, and memory tests exist.
- Promotion blockers:
  1. Expand subsystem coverage.
  2. Add determinism/runtime verification.
  3. Wire product launch after verification.

### PlayStation 2

- Status: Core present, not launchable.
- Evidence: CPU, GS, and memory tests exist.
- Promotion blockers:
  1. Expand subsystem coverage.
  2. Add determinism/runtime verification.
  3. Wire product launch after verification.

### Nintendo Switch

- Status: Indexed only, intentionally unavailable in production.
- Rule: keep unavailable until the repository's finished-state standard is met for the existing launchable platforms.

## Wave 3: Missing Manufacturer Families

### Microsoft Xbox Family

- Status: Not started.
- Requirement to count as supported manufacturer:
  1. Choose an initial target platform explicitly (original Xbox first is the practical entry point).
  2. Add authoritative reference sources.
  3. Add core architecture, subsystem tests, determinism/runtime verification, then shell launch wiring.

## Finish Criteria For The Repository

The emulator product is only in a finished state when:

1. Every launchable platform clears the full correctness gate in `emulator-verification-pipeline.md`.
2. No disabled emulator tests remain for supported behavior.
3. No stub/no-crash tests remain for hardware-defined behavior on launchable systems.
4. Every major manufacturer targeted by product strategy has at least one credible, verified production platform.
5. Core-present but hidden platforms are either promoted with proof or explicitly deprioritized in product planning.