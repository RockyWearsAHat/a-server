---
description: "Test scoping — map changed files to the smallest sufficient ctest set."
applyTo: "tests/**,src/emulator/**,include/emulator/**,src/gui/**,include/gui/**,assets/qss/**"
---

# Test Scoping

889 tests total. Never run the full suite unless changes span multiple subsystems or it's a pre-merge check.

Command: `cd build/generated/cmake && ctest -R <pattern> --output-on-failure`

## Scope Map

| Changed area                          | ctest -R pattern                                   |
| ------------------------------------- | -------------------------------------------------- |
| QSS files (`assets/qss/`)             | `QssValidation`                                    |
| GUI code (`src/gui/`, `include/gui/`) | `QssValidation`                                    |
| GBA CPU                               | `^CPUTest`                                         |
| GBA PPU                               | `PPU`                                              |
| GBA APU                               | `APU`                                              |
| GBA DMA                               | `^DMA`                                             |
| GBA memory / EEPROM                   | `MemoryMap` and `EEPROM`                           |
| GBA BIOS                              | `BIOSTest`                                         |
| GBA broad / integration               | `GBA`                                              |
| PS1 CPU                               | `PS1CPUTest`                                       |
| PS1 GPU                               | `PS1GPUTest`                                       |
| PS1 GTE                               | `PS1GTETest`                                       |
| PS1 DMA                               | `PS1DMATest`                                       |
| PS1 SPU                               | `PS1SPUTest`                                       |
| PS1 timers                            | `PS1TimerTest`                                     |
| PS1 controllers                       | `PS1ControllerTest`                                |
| PS1 interrupts                        | `PS1InterruptTest`                                 |
| PS1 memory                            | `PS1MemoryTest`                                    |
| PS1 broad / integration               | `PS1`                                              |
| Atari 2600                            | `Atari2600`                                        |
| Switch core                           | `Switch`                                           |
| Windows compatibility layer           | `WindowsCompat`                                    |
| Logging                               | `LoggerTest`                                       |
| Input                                 | `InputLogic`                                       |
| ROM metadata                          | `ROMMetadata`                                      |
| Screen mirror                         | `AirPlayReceiverTest` + `MirrorSessionManagerTest` |
| Specific test file                    | Match the test binary name                         |
| Multi-subsystem or pre-merge          | Full: `ctest --output-on-failure`                  |

For multiple patterns in one run: `ctest -R 'PatternA\|PatternB' --output-on-failure`

## Rules

- One subsystem changed → run only that subsystem's tests.
- QSS/GUI-only edits never need emulator tests.
- Emulator-only edits never need QssValidation.
- When in doubt, run the broader subsystem group (all GBA or all PS1).
- Full suite only before merge or when changes touch >2 subsystems.
