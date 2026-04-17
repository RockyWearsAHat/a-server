# Emulator Verification Pipeline

Durable reference for emulator correctness verification in AIO Server.

## Purpose

Define a single verification contract so emulator work is accurate, complete, and repeatable.

## Layers

| Layer | Goal | Primary command/evidence |
| --- | --- | --- |
| 0 | Build gate | `make build` |
| 1 | Sanitizer clean | Sanitizer-enabled build has zero ASan/UBSan findings |
| 2 | Targeted subsystem tests | `ctest -R <pattern> --output-on-failure` |
| 3 | Cross-subsystem tests | `ctest -R 'GBAIntegrat\|PS1Integrat\|Determinism' --output-on-failure` |
| 4 | TAS determinism | `python3 scripts/tas_determinism_test.py --system <platform> ...` |
| 5 | Headless/runtime state | `AIOServer --headless ... --headless-assert-nonblack` and state polling |
| 6 | Visual evidence | screenshot + MCP image judgment |
| 7 | Acceptance gate | `.github/COMPLETION_CHECKLIST.md` updated/validated |

## Change Type To Minimum Layers

| Change type | Required layers |
| --- | --- |
| CPU/DMA/timer/memory behavior | 0-4 |
| GPU/PPU/rendering | 0-5 plus 6 |
| APU/SPU behavior | 0-4 |
| New platform onboarding | 0-7 |

## Definition Of Done For Emulator Correctness

Passing tests are necessary, but they are not sufficient for sign-off.

A platform or subsystem is only considered complete when all of the following are true:

1. All implemented behaviors are tested against authoritative hardware documentation, not convenience assumptions.
2. No relevant tests are disabled for the supported behavior surface.
3. Tests do not merely assert "does not crash" for behavior that the hardware specifies concretely.
4. Known stubs, approximations, and unimplemented commands are documented and block any claim of "lossless" status.
5. Required verification layers for the change type have been executed and recorded.
6. Cross-subsystem interaction paths are covered, not just isolated component smoke tests.

"100% complete" and "lossless" are reserved for platforms that satisfy the full contract above. A green CTest run alone is not enough.

## Coverage Snapshot

- GBA: Layer 4 TAS coverage exists with 120s validated baselines, but disabled tests and stub-only BIOS SWI coverage still prevent a "lossless" claim.
- PS1: No Layer 4 TAS support yet; use Layers 3 and 5 as temporary substitute. CDROM behavior and some interaction paths still need stronger spec-backed tests.
- NES/SNES/GB/GBC: Script adapter slots exist, but deterministic coverage depends on ROM/baseline setup.
- Genesis/N64/GameCube/Saturn/Dreamcast/PS2/Atari2600/Switch: presence of source files or basic tests does not imply acceptance-gate completion.

## Current Platform Support Matrix

Status meanings:

- `Launchable`: registered in `include/gui/EmulatorFormats.h` with `launchable=true`
- `Core present`: emulator source tree exists
- `Test surface`: dedicated platform test files exist in `tests/`
- `Lossless`: full definition-of-done above satisfied

| Manufacturer | Platform | Launchable | Core present | Test surface | Current sign-off status |
| --- | --- | --- | --- | --- | --- |
| Nintendo | Game Boy Advance | Yes | Yes | Yes | Not lossless yet |
| Nintendo | Game Boy / Color | Yes | Yes | Yes | Not lossless yet |
| Nintendo | NES | Yes | Yes | Yes | Not lossless yet |
| Nintendo | SNES | Yes | Yes | Yes | Not lossless yet |
| Nintendo | Nintendo 64 | No | Yes | Yes | Core/test work exists, not product-complete |
| Nintendo | GameCube | No | Yes | Yes | Core/test work exists, not product-complete |
| Nintendo | Switch | No | Yes | Yes | Indexed only, intentionally unavailable |
| Sony | PlayStation | Yes | Yes | Yes | Not lossless yet |
| Sony | PlayStation 2 | No | Yes | Yes | Core/test work exists, not product-complete |
| Sega | Genesis | Yes | Yes | Yes | Not lossless yet |
| Sega | Saturn | No | Yes | Yes | Core/test work exists, not product-complete |
| Sega | Dreamcast | No | Yes | Yes | Core/test work exists, not product-complete |
| Atari | Atari 2600 | Yes | Yes | Yes | Not lossless yet |
| Microsoft | Xbox family | No | No | No | Not started |

## Major-Manufacturer Expansion Standard

Supporting every major manufacturer means more than adding folders or a launcher entry. Each new platform must ship in this order:

1. Core architecture present.
2. Authoritative reference set documented.
3. Subsystem tests for CPU, memory/bus, graphics, audio, DMA/timers/interrupts as applicable.
4. Cross-subsystem and determinism coverage.
5. Runtime/headless evidence.
6. Product launch wiring only after the correctness gates above are credible.

Do not mark a manufacturer family complete while one of its flagship production platforms still lacks a verified emulator path.

## Current High-Priority Gaps

1. GBA still has disabled hardware-behavior tests and BIOS SWI paths that are only stub-validated.
2. PS1 still lacks full CDROM proof and TAS/determinism support.
3. Product-launchable platforms do not yet all meet the stronger lossless-definition gate.
4. Several core-present systems (N64, GameCube, Saturn, Dreamcast, PS2) are not yet promoted to production launchability.
5. Microsoft console support is absent entirely.

## Baseline Location

`test_output/tas_baselines/<rom_stem>/<capture_ms>ms.ppm`

These artifacts are part of deterministic evidence and should be kept synchronized with validated behavior changes.

## Known Gaps

1. PS1 TAS support is not yet implemented in `scripts/tas_determinism_test.py`.
2. Sanitizer gate depends on adding CMake sanitizer flags.
3. The repository has multiple platforms with source/test presence but without a full acceptance-gate trail.
4. Disabled emulator tests and stub-acceptance tests must be treated as blockers for any "100% complete" statement.

## References

- `.github/skills/emulator-verification-pipeline/SKILL.md`
- `.github/instructions/emulator-core.instructions.md`
- `.github/instructions/test-scoping.instructions.md`
- `.github/knowledge/gba-determinism-validation-2026.md`
