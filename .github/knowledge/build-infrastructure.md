# Build System & Development Infrastructure

> **Last audited**: 2025-07-18

## Build Pipeline

```
User → Makefile (project root)
      → cmake/Makefile (configuration wrapper)
      → build/generated/cmake/ (CMake + Ninja)
      → build/bin/ (executables)
```

### Root Makefile Targets

| Target                | Purpose                                                 |
| --------------------- | ------------------------------------------------------- |
| `make build`          | Configure (if needed) + build AIOServer binary          |
| `make test`           | Build everything including tests, run ctest             |
| `make all`            | Clean + full rebuild                                    |
| `make clean`          | Remove build/ directory                                 |
| `make configure`      | Run CMake configuration only                            |
| `make reconfigure`    | Force fresh CMake configuration                         |
| `make coverage`       | Clean rebuild with instrumentation + test + lcov report |
| `make coverage-regen` | Regenerate coverage from last test run                  |

### CMake Configuration

- **Entry**: `cmake/CMakeLists.txt` (v3.16+)
- **Standard**: C++20
- **Generator**: Ninja
- **ccache**: Automatic if available
- **Qt6 auto**: AUTOMOC, AUTORCC, AUTOUIC enabled
- **Output**: All under `build/` (bin, lib, generated/autogen)
- **compile_commands.json** copied to workspace root for clangd

### Dependencies

| Dependency                                     | CMake Package           | Source                                 |
| ---------------------------------------------- | ----------------------- | -------------------------------------- |
| Qt6 (Widgets, WebEngineWidgets, Network, Test) | `find_package(Qt6)`     | Homebrew `/opt/homebrew/opt/qt`        |
| SDL2                                           | `find_package(SDL2)`    | vcpkg or Homebrew                      |
| libcurl                                        | `find_package(CURL)`    | Homebrew                               |
| OpenSSL 3                                      | `find_package(OpenSSL)` | Homebrew `/opt/homebrew/opt/openssl@3` |
| GoogleTest 1.14.0                              | `FetchContent`          | GitHub                                 |

### Library Targets

| Target           | Type       | Purpose                            |
| ---------------- | ---------- | ---------------------------------- |
| `GBAEmulator`    | STATIC     | GBA emulator core                  |
| `PS1Emulator`    | STATIC     | PS1 emulator core                  |
| `SwitchEmulator` | STATIC     | Switch emulator skeleton           |
| `AIOServer`      | EXECUTABLE | Main GUI application               |
| `QssValidator`   | EXECUTABLE | QSS syntax validation (post-build) |

### Post-Build

1. QssValidator runs on `assets/qss/` — build fails on QSS syntax errors
2. macOS code signing with `debuggable.entitlements.plist` (enables core dumps)

## Test Infrastructure

- **Framework**: GoogleTest 1.14.0
- **Discovery**: `enable_testing()` + `gtest_discover_tests()`
- **Run**: `cd build/generated/cmake && ctest --output-on-failure`
- **Targeted**: `ctest -R <pattern> --output-on-failure`

### Test Suites (889+ tests)

**GBA**: CPUTests, PPUTests, APUTests, MemoryMapTests, DMATests, DMATimingTests, EEPROMTests, BIOSTests, GBAIntegrationTests, GraphicsCorruptionTests, AudioCorruptionTests, InputLogicTests

**PS1**: PS1CPUTests, PS1GPUTests, PS1DMATests, PS1GTETests, PS1SPUTests, PS1TimerTests, PS1InterruptTests, PS1ControllerTests, PS1MemoryTests, PS1IntegrationTests

**Other**: QssValidator, LoggerTests, ScreenMirrorTests

## Code Coverage

- Enable: `-DENABLE_COVERAGE=ON` adds `-fprofile-arcs -ftest-coverage`
- Generate: `make coverage` (full) or `make coverage-regen` (reuse last run)
- Output: `build/lcov.info` for Coverage Gutters

## Visual Development Loop

- `scripts/visual_dev_loop.py` — Automated boot → navigate → capture → inspect → judge
- RemoteControlServer on port 9876 — HTTP REST dev-automation server. State polling (7 endpoints), event streaming (/events ring buffer), input injection (key/click/type), emulator control (launch-rom, stop-game, pause, resume, step-frame), per-page structured state (gameStore, gamesLibrary, streamingHub).
- MCP vision tool for screenshot analysis
- Used by Visual Engineer agent for rendered-output verification

## Copilot Agent Hierarchy

```
Main Agent (coordinator)
  └── Project Lead (planner/dispatcher)
        ├── R&D Lead (external docs research)
        ├── Senior Engineer (implementation + verification)
        │     ├── Code Engineer (code changes)
        │     ├── Test Engineer (test verification)
        │     └── Visual Engineer (screenshot verification)
        └── Quality Auditor (standards check)
```

## MCP Tools

- **aioserver-vision-tool**: Image analysis for visual testing
- **aioserver-research-tool**: Web search + knowledge cache
