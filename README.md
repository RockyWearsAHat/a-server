# AIO Server - Multi-Console Emulator

A high-performance Game Boy Advance (GBA) and Nintendo Switch emulator with an integrated 10-foot UI, network-attached storage (NAS), and streaming capabilities.

## Features

- **GBA Emulation**: Full ARM7TDMI CPU, PPU, APU, DMA, and memory subsystems
- **Switch Emulation**: ARMv8 CPU core with HLE support (experimental)
- **10-Foot UI**: TV-friendly interface designed for controllers
- **Network Storage**: Built-in NAS server for ROM and save file management
- **Streaming Integration**: Quick access to streaming apps
- **Separate YouTube Auth Server**: Optional Node/Express/TypeScript service for OAuth and YouTube API proxying without embedding secrets in the Qt app
- **Debugging Tools**: Built-in debugger with breakpoints and step execution
- **Deterministic Input**: Script-based input replay for testing
- **Code Coverage**: Integrated testing and coverage reporting

## Architecture

```
AIO Server
├── Emulator Core (src/emulator/)
│   ├── GBA: ARM7TDMI, PPU, APU, Memory, DMA, EEPROM
│   └── Switch: ARMv8 CPU, HLE (experimental)
├── GUI (src/gui/)
│   ├── MainWindow: 10-foot UI orchestration
│   ├── Pages: Menu, game select, settings, NAS
│   └── Adapters: Controller navigation logic
├── Input System (src/input/)
│   ├── SDL2 controller/keyboard handling
│   └── Unified input manager with configurable bindings
├── NAS Server (src/nas/)
│   └── HTTP file server for ROM/save management
├── Streaming (src/streaming/)
│   └── Integration with media apps
└── Common (src/common/)
    ├── Logging system
    ├── Asset management
    └── Utilities
```

## Building

### Prerequisites

- **CMake** 3.15+
- **C++17** compiler (GCC 9+, Clang 10+, MSVC 2019+)
- **Qt 5.15+** (Widgets, Network, WebEngine)
- **SDL2** 2.0.14+
- **GoogleTest** (fetched automatically)

Install dependencies on macOS:

```bash
brew bundle
```

### Build Commands

```bash
# Configure and build (Release)
make build

# Run unit tests
make test

# Generate code coverage report
make coverage

# Clean workspace
./scripts/clean.sh
```

Build artifacts are in `build/bin/`:

- `AIOServer` - Main application
- `*Tests` - Unit test binaries

## Running

### GUI Mode

```bash
./build/bin/AIOServer
```

### Load ROM Directly

```bash
./build/bin/AIOServer --rom path/to/game.gba
```

### Headless Mode (Testing/CI)

```bash
./build/bin/AIOServer --rom test_roms/game.gba --headless --headless-max-ms 5000
```

### Enable Debugger

```bash
./build/bin/AIOServer --rom game.gba --enable-debugger --breakpoint 0x08000000
```

## Configuration

Configuration is managed through:

1. **Command-line arguments** (see `--help`)
2. **Environment variables** (`.env` file)
3. **QSettings** (persistent UI preferences)

### Environment Variables

```bash
# Logging
AIO_LOG_MIRROR=1          # Mirror logs to stdout
AIO_LOG_APPEND=1          # Append to log file
AIO_LOG_LEVEL=debug       # Log level: debug|info|warn|error|fatal

# NAS Server
AIO_NAS_ROOT=/path/to/roms  # NAS root directory
AIO_NAS_PORT=8080            # HTTP server port
AIO_NAS_URL=http://10.0.1.5:8080  # Public URL for clients

# Input
AIO_INPUT_DEBUG=1         # Log controller events
AIO_INPUT_SCRIPT=/path    # Script-based input playback
AIO_INPUT_SCRIPT_TIMEBASE=EMU  # Use emulated time for scripts

# Debugging
AIO_TRACE_IE_WRITES=1     # Trace interrupt enable writes

# YouTube auth proxy (optional, recommended)
AIO_YOUTUBE_SERVER_URL=http://127.0.0.1:8916
AIO_YOUTUBE_SERVER_AUTOBOOT=1
AIO_YOUTUBE_SERVER_NODE=node
AIO_YOUTUBE_SERVER_WORKDIR=/absolute/path/to/AIO Server/server
AIO_YOUTUBE_SERVER_ENTRY=dist/index.js
```

## Separate Server

The YouTube OAuth client id, client secret, and API key can now live in the separate Node server under [server/README.md](/Users/alexwaldmann/Desktop/AIO Server/server/README.md).

This keeps Google credentials out of the Qt application binary and out of the app's `.env`.

## Testing

### Unit Tests

Individual component tests using GoogleTest:

```bash
# Run all tests
make test

# Run specific test binary
./build/bin/CPUTests
./build/bin/PPUTests
```

### Test Suite (Python)

Comprehensive test runner with timeout handling:

```bash
./scripts/test_suite.py
```

### ROM Sweep

Test stability across multiple ROMs:

```bash
./scripts/rom_sweep.py --roms-dir ~/ROMs/GBA --timeout-s 10
```

## Project Structure

```
.
├── assets/           # UI themes, NAS web interface
├── build/            # Build artifacts (generated)
├── cmake/            # CMake build configuration
├── include/          # Public headers
├── scripts/          # Build and test utilities
├── src/              # Implementation files
├── tests/            # Unit tests
├── test_roms/        # Test ROM files (not in repo)
├── Brewfile          # macOS dependencies
├── Makefile          # Build wrapper
└── LICENSE           # Software license
```

## Development Workflow

1. **Feature Development**: Create a feature branch
2. **Build**: `make build`
3. **Test**: `make test` - Ensure all tests pass
4. **Coverage**: `make coverage` - Verify code coverage
5. **Commit**: Use descriptive commit messages
6. **Pull Request**: Submit for review

### Code Style

- **C++ Standard**: C++17
- **Formatting**: Follow existing style (see CONTRIBUTING.md)
- **Naming**: PascalCase for classes, camelCase for functions/variables
- **Headers**: Use `#pragma once`
- **Namespaces**: Organize by component (e.g., `AIO::Emulator::GBA`)

## Debugging

### GDB/LLDB Integration

```bash
# Debug with breakpoints
lldb ./build/bin/AIOServer
(lldb) b MainWindow::LoadROM
(lldb) run --rom game.gba
```

### Built-in Emulator Debugger

```bash
# Step through ARM instructions
./build/bin/AIOServer --rom game.gba --enable-debugger

# Controls during execution:
# - Down/Enter: Step one instruction
# - Up: Step back (reverse execution)
# - C: Continue execution
```

### Logging

Logs are written to `debug.log` by default. Increase verbosity:

```bash
AIO_LOG_LEVEL=debug ./build/bin/AIOServer
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on:

- Code standards
- Commit conventions
- Pull request process
- Testing requirements

## License

See [LICENSE](LICENSE) file for details.

## Credits

Built with:

- [Qt](https://www.qt.io/) - GUI framework
- [SDL2](https://www.libsdl.org/) - Input and audio
- [GoogleTest](https://github.com/google/googletest) - Unit testing
