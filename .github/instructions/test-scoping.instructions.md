---
description: "Test scoping — map changed files to the smallest sufficient ctest set."
applyTo: "tests/**,src/emulator/**,include/emulator/**,src/gui/**,include/gui/**,assets/qss/**"
---

# Test Scoping

889 tests total. Never run the full suite unless changes span multiple subsystems or it's a pre-merge check.

Command: `cd build/generated/cmake && ctest -R <pattern> --output-on-failure`

## Scope Map

| Changed area | ctest -R pattern | Also run |
| --- | --- | --- |
| QSS files (`assets/qss/`) | `QssValidation` | None |
| GUI code (`src/gui/`, `include/gui/`) | `QssValidation` | None |
| GBA CPU | `^CPUTest` | Layer 4: `python3 scripts/tas_determinism_test.py --system gba --all` |
| GBA PPU | `PPU` | Layer 4: `python3 scripts/tas_determinism_test.py --system gba --all` |
| GBA APU | `APU` | Layer 4: `python3 scripts/tas_determinism_test.py --system gba --all` |
| GBA DMA | `^DMA` | Layer 4: `python3 scripts/tas_determinism_test.py --system gba --all` |
| GBA memory / EEPROM | `MemoryMap` and `EEPROM` | Layer 4: `python3 scripts/tas_determinism_test.py --system gba --all` |
| GBA BIOS | `BIOSTest` | Layer 4: `python3 scripts/tas_determinism_test.py --system gba --all` |
| GBA broad / integration | `GBA` | Layer 4: `python3 scripts/tas_determinism_test.py --system gba --all` |
| PS1 CPU | `PS1CPUTest` | Layer 3: `ctest -R 'PS1Integration\|Determinism' --output-on-failure`; Layer 5 runtime/headless check |
| PS1 GPU | `PS1GPUTest` | Layer 3: `ctest -R 'PS1Integration\|Determinism' --output-on-failure`; Layer 5 runtime/headless check |
| PS1 GTE | `PS1GTETest` | Layer 3: `ctest -R 'PS1Integration\|Determinism' --output-on-failure`; Layer 5 runtime/headless check |
| PS1 DMA | `PS1DMATest` | Layer 3: `ctest -R 'PS1Integration\|Determinism' --output-on-failure`; Layer 5 runtime/headless check |
| PS1 SPU | `PS1SPUTest` | Layer 3: `ctest -R 'PS1Integration\|Determinism' --output-on-failure`; Layer 5 runtime/headless check |
| PS1 timers | `PS1TimerTest` | Layer 3: `ctest -R 'PS1Integration\|Determinism' --output-on-failure`; Layer 5 runtime/headless check |
| PS1 controllers | `PS1ControllerTest` | Layer 3: `ctest -R 'PS1Integration\|Determinism' --output-on-failure`; Layer 5 runtime/headless check |
| PS1 interrupts | `PS1InterruptTest` | Layer 3: `ctest -R 'PS1Integration\|Determinism' --output-on-failure`; Layer 5 runtime/headless check |
| PS1 memory | `PS1MemoryTest` | Layer 3: `ctest -R 'PS1Integration\|Determinism' --output-on-failure`; Layer 5 runtime/headless check |
| PS1 broad / integration | `PS1` | Layer 5 runtime/headless check |
| Atari 2600 | `Atari2600` | Layer 5 runtime/headless check when behavior changes |
| Switch core | `Switch` | Runtime validation only if platform is enabled for launch path |
| Windows compatibility layer | `WindowsCompat` | None |
| Cross-system determinism / save-state | `Determinism` | Layer 4 where TAS coverage exists |
| Logging | `LoggerTest` | None |
| Input | `InputLogic` | None |
| ROM metadata | `ROMMetadata` | None |
| Screen mirror | `AirPlayReceiverTest` + `MirrorSessionManagerTest` | None |
| Specific test file | Match the test binary name | Optional layer checks based on behavior change |
| Multi-subsystem or pre-merge | Full: `ctest --output-on-failure` | Add required emulator layers from `.github/instructions/emulator-core.instructions.md` |

For multiple patterns in one run: `ctest -R 'PatternA\|PatternB' --output-on-failure`

## Rules

- One subsystem changed → run only that subsystem's tests.
- QSS/GUI-only edits never need emulator tests.
- Emulator-only edits never need QssValidation.
- When in doubt, run the broader subsystem group (all GBA or all PS1).
- Full suite only before merge or when changes touch >2 subsystems.
- Any emulator behavior change must include at least one non-ctest runtime verification layer.
- GBA behavior changes require Layer 4 TAS determinism evidence.
- PS1 TAS support is not yet available in `scripts/tas_determinism_test.py`; use Layers 3 and 5 as the temporary substitute gate.
