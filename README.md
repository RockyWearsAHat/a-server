# AIO Entertainment System

[![Build](https://img.shields.io/github/actions/workflow/status/RockyWearsAHat/a-server/ci.yml?branch=main&label=build)](https://github.com/RockyWearsAHat/a-server/actions)
[![Tests](https://img.shields.io/github/actions/workflow/status/RockyWearsAHat/a-server/ci.yml?branch=main&label=tests)](https://github.com/RockyWearsAHat/a-server/actions)
[![License](<https://img.shields.io/badge/license-Free%20Use%20(Non--commercial)-blue>)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-20%2F23-blue)](https://isocpp.org/)
[![Qt](https://img.shields.io/badge/Qt-6-41cd52)](https://www.qt.io/)
[![SDL2](https://img.shields.io/badge/SDL2-audio%2Finput-lightgrey)](https://www.libsdl.org/)

> Multi-platform emulator core(s) integrated into a Qt6-based “home theater” UI, with cycle-accurate design goals and test-driven development.

---

## Project Goal

AIO Entertainment System aims to provide a clean, testable emulation architecture (starting with a cycle-accurate GBA core) paired with a modern Qt6 UI suitable for a living-room experience:

- **Accurate emulation**: spec-driven behavior (GBATEK), correct timing/wait states, deterministic stepping
- **Separation of concerns**: emulation core is independent from presentation (Qt UI, audio output, input)
- **Testability**: CPU and subsystems are covered by GoogleTest-based unit/integration tests
- **Great developer ergonomics**: single unified logging output, reproducible headless runs

---

## Repository Layout (high level)

- `src/emulator/gba/` — GBA core (CPU, memory/bus, PPU, APU, DMA, timers, interrupts)
- `include/emulator/gba/` — public headers for the GBA core
- `src/gui/` / `include/gui/` — Qt6 UI (`MainWindow`, rendering, input, SDL2 audio callback)
- `tests/` — GoogleTest test suites (CPU + integration tests like EEPROM)
- `cmake/` — project CMake modules
- `Makefile` — convenience entry point that delegates to the generated CMake/Make build

---

## Prerequisites

### Required

- **CMake** (recent)
- **C++ compiler** supporting C++20+ (Clang recommended on macOS)
- **Qt 6**
- **SDL2**
- **Git**

### Test dependencies

- **GoogleTest** is fetched/managed via CMake for this project.

### macOS (Homebrew example)

```sh
brew install cmake sdl2
brew install qt
```

> If Qt is installed via Homebrew, it is typically located at `/opt/homebrew/opt/qt`.

---

## Build From Scratch

> **Do not edit anything under `build/`** — it is generated output.

```sh
# From the repo root
make
```

Build outputs:

- `build/bin/` — executables (tests + AIOServer)
- `build/lib/` — static libraries

---

## Running

### GUI (autoboot a ROM)

```sh
./build/bin/AIOServer -r path/to/game.gba
```

### Headless (useful for CI / log capture)

```sh
./build/bin/AIOServer \
    --headless --headless-max-ms 8000 \
    --log-file debug.log \
    -r path/to/game.gba
```

---

## Testing

The project uses GoogleTest and includes both unit and integration tests.

```sh
make
./build/bin/CPUTests
./build/bin/EEPROMTests
```

---

## Logging & Debugging

All logs (Qt, stdout/stderr, emulator logger) are routed into a single file:

- **Default log file**: `debug.log` at the project root

Common flags:

```sh
./build/bin/AIOServer --log-file debug.log --exit-on-crash -r path/to/game.gba
```

Environment toggles:

- `AIO_LOG_MIRROR=1` — mirror logs to stdout/stderr while still logging to file
- `AIO_LOG_APPEND=1` — append rather than truncate
- `AIO_LOG_LEVEL=debug|info|warn|error|fatal` — minimum log level

---

## Development Notes

### Architecture principles

- **Spec-driven accuracy** (GBATEK, documented hardware behavior)
- **Cycle accuracy** (memory wait states, correct DMA timing, APU/PPU/timers progression)
- **No game-specific hacks** unless replicating real BIOS/hardware behavior

### Namespaces & conventions

All code lives under structured namespaces (e.g. `AIO::Emulator::GBA`, `AIO::GUI`, `AIO::Input`), and the project avoids `using namespace`.

---

## VS Code Tasks (recommended)

- **Build**: runs `make`
- **Test**: runs `Build` → `CTest`
- **Clean Workspace**: `./scripts/clean.sh`

---

## Contributing

Anyone is free to contribute, modify, or otherwise improve upon or use this code as a starting point in accordance with the rules and guidelines outlined in this document. If you would like to contribute directly please feel free to reach out via [phone](tel:4357317654) or [email](mailto:alexwaldmann2004@gmail.com).

---

## License

See [LICENSE](LICENSE).

---
