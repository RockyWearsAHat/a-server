# End-to-End Emulator Program Roadmap

Last updated: 2026-04-15

## Objective

Deliver factory-accurate emulator implementations for major game-console manufacturers using a phased, test-first program that scales from 8-bit systems to modern service-heavy platforms.

This roadmap is the execution layer for:
- docs/emulation/major-console-spec-sheets.md
- docs/emulation/factory-accuracy-emulator-playbook.md

## Program principles

- Correctness before speed.
- Tier-1 and Tier-2 sources before game-specific hacks.
- Test ROM conformance before compatibility claims.
- Explicit confidence labels per subsystem.
- No "100% accurate" claim without objective evidence.

## Definition of done (global)

A console reaches "factory-accurate target" when all are true:

1. Core CPU instruction and exception suites pass at target level.
2. Timing-sensitive hardware tests pass for interrupts, DMA, and video/audio cadence.
3. Regression suite for representative commercial titles is stable.
4. Frame/audio diffs are within agreed thresholds for known-good captures.
5. All unresolved deviations are documented with confidence tags and source citations.

## Program structure

### Track A: Core architecture

- Deterministic scheduler library
- Bus/arbitration framework
- Interrupt framework (edge/level, pending/enable separation)
- Save-state framework with determinism checks
- Trace and replay tooling

### Track B: Console implementation waves

- Wave 1: 8/16-bit cartridge era
- Wave 2: 32-bit transition era
- Wave 3: 6th generation and hybrid complexity
- Wave 4: modern x86/ARM service-heavy systems

### Track C: Validation and quality

- Test ROM harness and baseline corpus
- Golden-frame and golden-audio capture system
- Continuous regression matrix per console
- Performance profiling with correctness guards

### Track D: Knowledge and documentation

- Per-console design packet
- Source evidence index
- Known-deviation ledger
- Accuracy confidence dashboard

## Phase plan (start to finish)

## Phase 0: Program setup (Weeks 1-3)

Deliverables:
- Repository structure for emulator program docs and test assets.
- Source-tier policy and citation template.
- Determinism baseline harness.

Exit criteria:
- CI can run deterministic replay for at least one existing core (GBA or PS1).
- Program dashboard exists with pass/fail metrics.

## Phase 1: Shared infrastructure (Weeks 3-8)

Deliverables:
- Shared scheduler and event queue model.
- Standardized memory map + MMIO register abstraction.
- Unified interrupt controller abstraction.
- Common test harness runner.

Exit criteria:
- Existing emulators can opt into new scheduler in compatibility mode.
- Cross-platform deterministic replay passes on CI.

## Phase 2: Wave 1 consoles (Months 2-8)

Target manufacturers/systems:
- Nintendo: NES, GB/C, SNES
- Sega: Master System, Genesis, Game Gear
- Atari: 2600, 5200, 7800
- NEC/Hudson: PC Engine/TurboGrafx-16
- Early US majors: ColecoVision, Intellivision, Odyssey2, Channel F, Astrocade

Per-console checklist:
- CPU core passes instruction suites.
- PPU/VDP timing tests pass (scanline/raster behavior).
- Audio cadence and envelope tests pass.
- Mapper or cartridge banking behavior validated.

Exit criteria:
- At least 70% of Wave 1 consoles at Level 2 or higher.
- At least 3 flagship systems at Level 3 (suggested: NES, Genesis, SNES).

## Phase 3: Wave 2 consoles (Months 6-14)

Target manufacturers/systems:
- Sony: PS1
- Nintendo: N64
- Sega: Saturn
- Atari: Jaguar
- Panasonic/3DO: 3DO
- SNK: Neo Geo AES/MVS and Neo Geo CD
- NEC/Hudson: SuperGrafx, PC-FX

Priority sequencing inside Wave 2:
1. PS1 (already present in codebase; raise timing/audio fidelity)
2. N64
3. Saturn
4. Neo Geo
5. Jaguar + 3DO + others

Per-console high-risk focus:
- Multi-processor synchronization
- DMA arbitration correctness
- Geometry/video command pipeline timing
- Audio DSP edge behavior

Exit criteria:
- PS1 reaches Level 3 in CPU/GPU/DMA core paths with documented residual gaps.
- At least two additional Wave 2 systems reach Level 2+.

## Phase 4: Wave 3 consoles (Months 12-22)

Target systems:
- Nintendo: GameCube, Wii, DS
- Sony: PS2, PSP
- Microsoft: Xbox (2001)
- Sega: Dreamcast
- SNK/Bandai handhelds: Neo Geo Pocket, WonderSwan

Focus:
- Introduce HLE/LLE boundary contracts.
- Build subsystem-specific timing profilers.
- Add media/IO latency modeling for disc systems.

Exit criteria:
- PS2, GameCube, Dreamcast each have a validated design packet and passing core bring-up tests.
- At least one of these reaches Level 2 with reproducible regression suite.

## Phase 5: Wave 4 modern systems (Months 20-36+)

Target systems:
- Sony: PS3, Vita, PS4, PS5
- Microsoft: Xbox 360, Xbox One, Series X|S
- Nintendo: 3DS, Wii U, Switch

Focus:
- Service/kernel/API fidelity over pure transistor-level simulation.
- GPU API translation correctness and synchronization.
- Storage/decompression pipeline ordering.

Exit criteria:
- Clear per-platform scope statements: what is LLE, what is HLE, what is intentionally out-of-scope.
- Reproducible compatibility tiers with evidence-backed confidence.

## Phase 6: Finalization and audit loop (continuous from Month 6 onward)

Deliverables:
- Accuracy audit reports per release train.
- Public compatibility matrix with confidence labels.
- Known issue registry linked to subsystem owners.

Exit criteria:
- No release without updated accuracy and regression reports.
- Trend lines show improving determinism and reduced timing regressions.

## Console prioritization matrix

Use this matrix to decide "what next" at any point.

Score each console 1-5 across:
- Impact (community value)
- Technical leverage (reusable architecture)
- Existing codebase head start
- Documentation quality
- Team expertise fit

Recommended near-term top 10:
1. GBA (existing)
2. PS1 (existing)
3. NES
4. Genesis
5. SNES
6. Game Boy/Color
7. N64
8. Dreamcast
9. PS2
10. GameCube

## Team topology

- Program lead: roadmap and release criteria ownership.
- Architecture lead: scheduler/bus/interrupt framework.
- Console pod leads: one per active wave.
- Verification lead: test ROM, golden capture, regression infra.
- Research lead: source tiering and spec evidence quality.

Suggested pod shape per active console:
- 1 emulator engineer (core)
- 1 subsystem engineer (video/audio/media)
- 1 QA/tooling engineer

## Verification pipeline (must exist before major scaling)

1. Pre-merge checks
- Unit and subsystem tests.
- Determinism replay check.
- Lint/static analysis.

2. Nightly checks
- Full console regression matrix.
- Golden frame/audio drift checks.
- Performance trend monitoring.

3. Release checks
- Accuracy audit report.
- Known-deviation update.
- Compatibility report generation.

## Risk register and mitigation

### Risk: under-documented hardware behavior
Mitigation:
- Mark subsystem confidence explicitly.
- Maintain hypothesis tests and isolate assumptions.

### Risk: performance optimizations break determinism
Mitigation:
- Reference-accuracy mode is always retained.
- Fast paths must show equivalence tests.

### Risk: broad scope stalls delivery
Mitigation:
- Enforce wave-based milestones and stop conditions.
- Do not open new console waves before current wave gates are met.

### Risk: game-specific hacks accumulate
Mitigation:
- Ban untagged hacks.
- Every exception must reference source evidence and test coverage.

## Documentation outputs per milestone

For each console milestone, produce:
- specs/<console>-design-packet.md
- specs/<console>-timing-matrix.md
- specs/<console>-test-catalog.md
- specs/<console>-known-deviations.md
- specs/<console>-release-readiness.md

## Execution starter (immediate)

- Finalize Phase 0 deliverables.
- Integrate determinism harness with current GBA and PS1 code.

- Complete Phase 1 shared infrastructure baseline.
- Bring NES and Genesis into initial core bring-up.

- Push GBA and PS1 toward stricter Level 3 subsystems.
- Complete first Wave 1 full design packets (NES, Genesis, SNES).
- Stand up nightly regression dashboard.

## How to use this roadmap

1. Pick active wave and target consoles.
2. Generate console design packets from spec sheets.
3. Implement per subsystem in strict order: CPU -> interrupts -> DMA -> video -> audio -> media.
4. Gate each step on objective tests.
5. Update confidence and deviation docs continuously.
