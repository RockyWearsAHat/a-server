# Factory-Accuracy Emulator Knowledge Doc

Last updated: 2026-04-15

## Goal

Define the engineering standard required to implement emulators that behave like original hardware, not just "game-compatible enough".

## Accuracy ladder

- Level 0: Boots some games.
- Level 1: Broad compatibility with heuristic timing.
- Level 2: Passes major test ROMs; event-accurate core timing.
- Level 3: Cycle-accurate in timing-critical subsystems.
- Level 4: Hardware-verified parity (logic analyzer / decap-informed behavior where available).

Target for "factory-accurate" claims: Level 3 minimum, Level 4 where reference data exists.

## Non-negotiable architecture requirements

1. Deterministic scheduler
- Single global timebase or formally equivalent synchronized domains.
- No hidden host-time dependencies in emulation state.

2. Bus and arbitration model
- Explicit CPU/PPU/APU/DMA ownership windows.
- Contention and wait-states represented per bus transaction class.

3. Interrupt and exception semantics
- Pending vs enabled split is modeled.
- Edge vs level triggers are correct.
- Acknowledge side effects and latency are exact.

4. Memory map fidelity
- Mirroring, open-bus behavior, unmapped reads, and side-effect registers are implemented.
- MMIO write masks and reserved bits preserved.

5. Video pipeline fidelity
- Fetch/decode/render stages separated logically.
- Scanline/beam position visible to subsystems that depend on it.
- Sprite evaluation limits and overflow flags modeled.

6. Audio pipeline fidelity
- Mixer path models clipping/saturation behavior.
- Envelope and timer stepping tied to hardware clocks.
- Resampler does not hide emulation timing errors.

## Implementation blueprint per console

For each target console, write a design packet with these sections:

- Clock domains and conversion rules
- CPU core contract (instruction semantics + timing)
- DMA channels and trigger matrix
- Interrupt controller behavior and latency
- MMIO register table with reset defaults
- PPU/GPU command pipeline and hazards
- APU/SPU channel model and mixer path
- Cartridge/disc media subsystem behavior
- Save-state invariants and serialization rules
- Hardware test ROM matrix and pass criteria

## Console-family risk map

### 8-bit and early 16-bit
- Highest risk: scanline/PPU timing, mapper edge cases, audio frame sequencer cadence.
- Typical failures: mid-scanline effects, sprite limits, unstable audio envelopes.

### Late 16-bit / 32-bit cartridge and early disc
- Highest risk: multi-processor sync, DMA timing, command FIFO hazards.
- Typical failures: geometry glitches, audio desync, race conditions around interrupts.

### 6th generation and later
- Highest risk: OS/service surface, GPU API contracts, asynchronous IO pipelines.
- Typical failures: shader/API mismatch, kernel behavior mismatch, IO completion ordering.

## Validation strategy

1. Layered test plan
- Unit tests for CPU instructions and flags.
- Subsystem tests for DMA/IRQ/video/audio.
- Integration tests for boot sequence and BIOS behavior.
- Golden capture tests for frame/audio diffs.

2. Test source hierarchy
- First: official compliance suites and vendor docs.
- Second: community canonical tests (e.g., blargg/mooneye/acid where applicable).
- Third: game-based smoke tests.

3. Pass/fail policy
- "Runs game X" is never a correctness proof.
- Require objective thresholds: test ROM pass rates, frame hash stability, audio drift bounds.

## Performance policy

- Optimize only after correctness baseline is green.
- Use decoupled fast paths guarded by proof-equivalent behavior.
- Keep a reference-accuracy mode even if slower.

## Documentation policy

Every emulator subsystem change must include:

- What hardware rule is being implemented
- Which source established that rule (Tier 1/2/3)
- Which tests prove behavior
- Known deviations left intentionally and why

## Practical disclaimer

Publicly available information cannot guarantee perfect transistor-level identity for every platform. When official silicon behavior is undocumented, state assumptions explicitly and downgrade confidence for that area.

## Recommended next docs to add

- One deep-dive document per major console family in docs/emulation/specs/
- One shared "timing model cookbook" with scheduler patterns
- One "test ROM catalog" mapping each console to canonical test suites
