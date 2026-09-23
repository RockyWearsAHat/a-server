# Zero-Regression Truth Contract

Last updated: 2026-04-17

## Purpose

Absolute mathematical proof of zero bugs across all emulator states is not computable in practice.
This contract defines the strongest practical evidence gate and makes it executable.

A claim of correctness is valid only as:

- "Proven against the zero-regression contract at commit X"

Never as:

- "Perfect for all possible states"

## Hard Gate Command

Run from workspace root:

```bash
python3 scripts/zero_regression_gate.py
```

or:

```bash
make truth-gate
```

Exit code 0 is required for sign-off.

## Required Evidence Layers

1. Build clean
- Command: `make build`
- Must pass

2. Full native test suite clean
- Command: `cd build/generated/cmake && ctest --output-on-failure`
- Must pass

3. GBA TAS determinism clean
- Command: `python3 scripts/tas_determinism_test.py --system gba --all`
- Must pass

4. GBA run-to-run repeatability
- For each `.gba` ROM in `test_roms/`, run TAS determinism twice and compare frame hashes at 500/1500/3000ms
- Hashes must match exactly

5. Launchable platform smoke evidence
- For each launchable platform in `include/gui/EmulatorFormats.h`, require at least one ROM in `test_roms/` and a passing headless non-black dump run
- Missing ROM evidence fails the gate by default

## Why This Is Strong

- Catches regression in deterministic timing
- Catches crash/boot/render smoke failures per launchable platform
- Forces explicit evidence collection rather than intuition

## Known Limits

- This still does not prove full hardware equivalence for all cycle-level edge cases
- No finite test set can prove all future behaviors
- Baseline quality depends on initial oracle quality

## Raising Confidence Further

1. Add hardware or known-good reference trace differential tests per subsystem
2. Add long-run soak determinism (minutes/hours) for each platform
3. Add opcode-level trace parity corpus for CPU cores
4. Add per-platform TAS coverage beyond GBA

## Governance Rule

If any gate fails, no correctness claim is allowed.
If evidence is missing for a launchable platform, claim must be blocked until coverage is added.
