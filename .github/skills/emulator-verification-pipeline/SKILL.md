---
name: emulator-verification-pipeline
description: "Run the full layered emulator verification pipeline with change-type-based minimum gates."
user-invocable: false
---

# Emulator Verification Pipeline

Use this skill for emulator behavior changes, runtime correctness verification, and platform acceptance gates.

## When To Load

- CPU opcode, DMA, timer, bus, memory-map, or interrupt behavior changed.
- GPU/PPU/rendering behavior changed.
- APU/SPU behavior changed.
- A new emulator platform is being wired into launch flow.

## Layered Gates

| Layer | Goal | Command / Evidence |
| --- | --- | --- |
| 0 | Build | `make build` |
| 1 | Sanitizer clean (when available) | Sanitizer build reports zero ASan/UBSan issues |
| 2 | Targeted tests | `cd build/generated/cmake && ctest -R <pattern> --output-on-failure` |
| 3 | Cross-subsystem tests | `ctest -R 'GBAIntegrat\|PS1Integrat\|Determinism' --output-on-failure` |
| 4 | TAS determinism | `python3 scripts/tas_determinism_test.py --system <platform> ...` |
| 5 | Headless/runtime state | `AIOServer --headless ... --headless-assert-nonblack` plus `poll-state --endpoint emulator` |
| 6 | Visual evidence | `visual_dev_loop.py screenshot` + MCP image judgment (use `.github/skills/visual-development-loop/SKILL.md`) |
| 7 | Acceptance gate | Verify/update `.github/COMPLETION_CHECKLIST.md` for affected platform |

## Minimum Required Layers By Change Type

| Change type | Minimum layers |
| --- | --- |
| CPU opcode, DMA, timer, bus, memory-map behavior | 0-4 |
| GPU/PPU/rendering correctness | 0-5 plus 6 |
| APU/SPU behavior | 0-4 |
| New platform launch wire-up | 0-7 |
| UI/QSS-only change | Use UI/QSS workflow, not this pipeline |

## Platform Coverage Matrix (Current)

| Platform | Layer 4 TAS | Layer 5 headless/runtime |
| --- | --- | --- |
| GBA | Supported | Supported |
| PS1 | Not yet supported in TAS script | Supported |
| NES/SNES/GB/GBC | Adapter slots exist, coverage depends on ROM/baseline setup | Partial |
| Genesis/N64 | Not yet production verification ready | Partial |

## Fallback Rules

- If Layer 4 is unavailable for a platform, require Layer 3 plus Layer 5 and clearly mark TAS as a known gap.
- Do not mark platform-complete status without Layer 7 checklist evidence.
- If any required layer fails, stop sign-off and return a focused fix plan with exact failing layer(s).

## Baseline Governance

- Any change to `test_output/tas_baselines/` must cite the change type and the verification layers that justified the update.
- Baseline-only updates without a corresponding behavior rationale are not valid sign-off evidence.

## Reporting Contract

Every verification result must include:

1. Change type.
2. Required layers for that change type.
3. Executed commands by layer.
4. Pass/fail per layer.
5. Blockers and next fix step.
