---
description: "Accuracy, testing, and logging rules for emulator-core changes."
applyTo: "src/emulator/**/*.cpp,include/emulator/**/*.h,include/emulator/**/*.hpp,tests/**/*Tests.cpp"
---

# Emulator Core Workflow

- Architecture references: `.github/knowledge/gba-emulator-architecture.md` (GBA) and `.github/knowledge/ps1-emulator-architecture.md` (PS1). Check these before researching a subsystem.
- Research policy: `.github/knowledge/research-policy.md`. ALL emulation behavior must be sourced from official technical documentation — not other emulators, not hardware testing.
- **No hardware verification**: We do not have access to original hardware. Do NOT propose hardware testing or validation. Find the official spec instead. Official technical documentation is the ONLY path to 100% accuracy.
- **No other-emulator sourcing**: Other emulators (mGBA, PCSX-R, Duckstation, etc.) are NOT authoritative. Our emulator is an independent implementation built from official specs.
- Find the actual cause and fix every part of the problem before adjusting timings, masking failures, or broadening tolerances.
- Preserve existing timing and correctness behavior unless the task explicitly changes emulation semantics.
- Prefer focused tests, characterization tests, or deterministic headless runs before broad manual verification.
- Keep hot-path logging minimal and use targeted trace flags plus `debug.log` when runtime evidence is needed.

## Verification Layers (Authoritative)

Use this layered pipeline for emulator correctness. Run the minimum required layers for the change type.

| Layer | Gate | Command / Evidence |
| --- | --- | --- |
| 0 | Build | `make build` |
| 1 | Sanitizer clean (when available) | Sanitizer-enabled build must report zero ASan/UBSan issues |
| 2 | Targeted tests | `cd build/generated/cmake && ctest -R <pattern> --output-on-failure` |
| 3 | Cross-subsystem tests | `ctest -R 'GBAIntegrat\|PS1Integrat\|Determinism' --output-on-failure` |
| 4 | TAS determinism | `python3 scripts/tas_determinism_test.py --system <platform> ...` |
| 5 | Headless runtime state | `AIOServer --headless ... --headless-assert-nonblack` plus emulator state polling |
| 6 | Visual evidence | `visual_dev_loop.py screenshot` + MCP image judgment |
| 7 | Acceptance gate | Update/verify `COMPLETION_CHECKLIST.md` for affected platform |

Detailed operational procedure lives in `.github/skills/emulator-verification-pipeline/SKILL.md` and `.github/knowledge/emulator-verification-pipeline.md`.

## Required Layers By Change Type

| Change type | Minimum layers |
| --- | --- |
| CPU opcode, DMA, timer, memory-map behavior | 0-4 |
| GPU/PPU/rendering correctness | 0-5 plus 6 |
| APU/SPU behavior | 0-4 |
| New platform wire-up | 0-7 |
| UI/QSS-only changes | Follow GUI/QSS instructions instead of emulator pipeline |

If this file says an emulator layer is required, treat it as mandatory sign-off criteria.

## Accuracy Scope Guardrail

The GBA emulator is instruction-accurate, not sub-cycle-accurate. Timing is modeled per instruction. Do not propose sub-cycle pipeline modeling unless the task explicitly expands scope.

## Determinism Baseline Protocol

- Treat baseline changes in `test_output/tas_baselines/` as behavior evidence updates, not routine artifacts.
- Baseline updates require an explicit reason, associated change type, and the verification layers used to approve the new baseline.
- Do not accept a baseline change without matching test/runtime evidence.

## Runtime ROM Verification

Unit tests verify isolated logic. To verify actual gameplay accuracy, use the runtime tooling:

- **GUI mode**: `python3 scripts/visual_dev_loop.py boot --rom <path> --no-focus` — launches AIOServer with a ROM, then poll `/state/emulator` for frame advancement, crash detection, and emulated time.
- **Headless mode**: `./build/bin/AIOServer --headless --rom <path> --headless-max-ms 5000 --headless-dump-ppm /tmp/frame.ppm --headless-assert-nonblack` — runs N ms of emulation, dumps a frame, asserts it's not blank.
- **State endpoint**: `poll-state --endpoint emulator` returns `{type, running, paused, frameNumber, emulatedMs}` — use to verify the emulator is advancing frames and not stuck.
- **Screenshot + visual judgment**: After boot, use `screenshot` + MCP `analyze_images` to verify rendered output matches expected game visuals.
- **Available ROMs**: `~/Desktop/ROMs/` — GBA (`.gba`), PS1 (`.bin/.cue` in subdirs), Switch (`.xci`).

When asked whether emulators "work" or are "accurate", runtime ROM testing is the PRIMARY verification method — not code reading alone.

For behavior-changing emulator work, runtime checks are not enough by themselves. Include deterministic TAS verification and targeted `ctest` evidence in the same result.
