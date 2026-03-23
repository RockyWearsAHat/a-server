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

## Runtime ROM Verification

Unit tests verify isolated logic. To verify actual gameplay accuracy, use the runtime tooling:

- **GUI mode**: `python3 scripts/visual_dev_loop.py boot --rom <path> --no-focus` — launches AIOServer with a ROM, then poll `/state/emulator` for frame advancement, crash detection, and emulated time.
- **Headless mode**: `./build/bin/AIOServer --headless --rom <path> --headless-max-ms 5000 --headless-dump-ppm /tmp/frame.ppm --headless-assert-nonblack` — runs N ms of emulation, dumps a frame, asserts it's not blank.
- **State endpoint**: `poll-state --endpoint emulator` returns `{type, running, paused, frameNumber, emulatedMs}` — use to verify the emulator is advancing frames and not stuck.
- **Screenshot + visual judgment**: After boot, use `screenshot` + MCP `analyze_images` to verify rendered output matches expected game visuals.
- **Available ROMs**: `~/Desktop/ROMs/` — GBA (`.gba`), PS1 (`.bin/.cue` in subdirs), Switch (`.xci`).

When asked whether emulators "work" or are "accurate", runtime ROM testing is the PRIMARY verification method — not code reading alone.
