# Emulator Completion Roadmap

Phased plan for turning AIO Server emulator support into a finished product surface.

## Guiding Principle

Breadth without correctness is fake progress. Finish the exposed platforms first, then promote hidden cores, then add missing manufacturers.

## Phase 1: Truthful Launchable Surface

Goal: every platform already exposed in the shell becomes defensible.

Priority order:

1. GBA
2. PS1
3. NES
4. SNES
5. Game Boy / Color
6. Genesis
7. Atari 2600

Phase exit criteria:

- No disabled relevant emulator tests.
- No stub-only sign-off tests for hardware-defined behavior.
- Spec audit completed for each launchable platform.
- Required verification layers executed for each class of change.

## Phase 2: Strong Interaction Proof

Goal: move from component presence to interaction correctness.

Work items:

1. Expand cross-subsystem tests for CPU, DMA, interrupts, timers, graphics, audio, and storage interactions.
2. Require determinism evidence wherever save-state and frame output are part of the platform contract.
3. Add runtime/headless evidence for representative ROMs per launchable platform.
4. Keep architecture knowledge notes synchronized with verified behavior.

## Phase 3: Promote Core-Present Platforms

Goal: graduate the hidden-but-present systems into real product candidates.

Promotion order:

1. N64
2. GameCube
3. Saturn
4. Dreamcast
5. PS2

Promotion rule:

- Do not wire a platform into production launch flows until subsystem tests, determinism/runtime evidence, and minimum platform audit notes exist.

## Phase 4: Manufacturer Completion

Goal: support every major manufacturer the product intends to claim.

Current family state:

- Nintendo: broadest coverage, still not finished.
- Sony: PS1 launchable, PS2 core-present.
- Sega: Genesis launchable, Saturn/Dreamcast core-present.
- Atari: Atari 2600 launchable.
- Microsoft: no console platform yet.

Phase work:

1. Finish one verified Microsoft platform before claiming complete major-manufacturer coverage.
2. Decide whether other manufacturers belong in scope before adding more cores.

## Immediate Execution Order

If work starts now, take tasks in this sequence:

1. Eliminate all disabled GBA tests and replace stub-only GBA BIOS SWI acceptance.
2. Build dedicated PS1 CDROM verification and PS1 determinism support.
3. Audit the remaining launchable 8/16-bit platforms against authoritative specs.
4. Re-rank hidden platforms by product value versus verification cost.
5. Start Microsoft onboarding only after the launchable surface is honest.

### Progress Snapshot

- GBA ARM SWP/SWPB tests have been re-enabled and are passing.
- Remaining launchable-surface blockers still center on GBA DMA/PPU timing, GBA BIOS SWI stubs, and PS1 CDROM/determinism proof.

## Non-Goals

- Do not add more shell launch entries just because a source folder exists.
- Do not call a platform complete because synthetic unit tests pass.
- Do not chase manufacturer breadth while current launchable systems still fail the finished-state standard.