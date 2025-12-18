# Testing Suite

This repo’s tests are split into:

- **Unit tests**: GoogleTest executables in `build/bin/*Tests`
- **Integration / app smoke tests**: driven via `build/bin/AIOServer` in `--headless` mode
- **Emulator fuzz runs (time-bounded)**: runs the emulator for a fixed duration (default 10s) or fails fast on crash

The unified entrypoint is:

- `scripts/test_suite.py`

## Quick start

Build first:

```sh
make
```

Run everything (default):

```sh
./scripts/test_suite.py
```

List available tests:

```sh
./scripts/test_suite.py --list
```

## Selecting tests

Tests are addressed as:

- `system.test_name`

You can also refer to a whole system by using only:

- `system`

### Whitelist (run only these)

Run only the GBA CPU tests:

```sh
./scripts/test_suite.py --only gba.cpu
```

Run only all tests under a system:

```sh
./scripts/test_suite.py --only gba
```

Multiple selections:

```sh
./scripts/test_suite.py --only gba.cpu --only input.logic
```

### Exclude

Exclude an entire system:

```sh
./scripts/test_suite.py --exclude gba
```

Exclude a specific test:

```sh
./scripts/test_suite.py --exclude app.nas_smoke
```

Whitelist + exclude (exclude still applies):

```sh
./scripts/test_suite.py --only gba --exclude gba.eeprom
```

## Emulator fuzz runs

By default, the suite **auto-discovers `*.gba` ROM files in the workspace root** and runs each in:

- `--headless` mode
- `--exit-on-crash`
- bounded by `--headless-max-ms` (default 10 seconds per ROM)

You can disable ROM auto-discovery:

```sh
./scripts/test_suite.py --no-auto-discover-roms
```

Or provide ROMs explicitly:

```sh
./scripts/test_suite.py --rom ./SMA2.gba --rom ./DKC.gba
```

Or point at a directory:

```sh
./scripts/test_suite.py --roms-dir ./roms
```

Adjust the time limit per ROM:

```sh
./scripts/test_suite.py --fuzz-seconds 10
```

## NAS smoke test

The suite includes `app.nas_smoke`, which starts `AIOServer` in `--headless` mode long enough to ensure the NAS server can start.

You can control the served root for that smoke test:

```sh
./scripts/test_suite.py --nas-root "$PWD"
```

## VS Code tasks

- The `CTest` task must run from the real CMake build directory: `build/generated/cmake`.
- The unified runner can be invoked via the terminal:

```sh
./scripts/test_suite.py
```
