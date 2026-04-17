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

## Coverage Snapshot

- GBA: Layer 4 TAS coverage exists with 120s validated baselines.
- PS1: No Layer 4 TAS support yet; use Layers 3 and 5 as temporary substitute.
- NES/SNES/GB/GBC: Script adapter slots exist, but deterministic coverage depends on ROM/baseline setup.

## Baseline Location

`test_output/tas_baselines/<rom_stem>/<capture_ms>ms.ppm`

These artifacts are part of deterministic evidence and should be kept synchronized with validated behavior changes.

## Known Gaps

1. PS1 TAS support is not yet implemented in `scripts/tas_determinism_test.py`.
2. Sanitizer gate depends on adding CMake sanitizer flags.

## References

- `.github/skills/emulator-verification-pipeline/SKILL.md`
- `.github/instructions/emulator-core.instructions.md`
- `.github/instructions/test-scoping.instructions.md`
- `.github/knowledge/gba-determinism-validation-2026.md`
