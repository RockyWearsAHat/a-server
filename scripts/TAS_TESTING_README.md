# TAS-Based Emulator Validation Test Suite

Tool-Assisted Speedrun (TAS) file validation for NES, Genesis, SNES, and Game Boy emulators.

## Overview

This test suite validates emulator correctness by:
1. **Converting TAS files** (Tool-Assisted Speedruns) from various formats into input scripts
2. **Running the emulator** in headless mode with these pre-recorded inputs
3. **Capturing frame output** at multiple points during execution
4. **Verifying** that the emulator produces correct/non-black output

This approach ensures that:
- Emulators respond correctly to inputs
- Graphics rendering is working (frames are not all-black)
- Timing/determinism is correct (TAS completes as expected)
- All four systems (NES, Genesis, SNES, GameBoy) are functional

## Components

### 1. TAS Converter (`scripts/tas_converter.py`)

Converts TAS files from various formats to AIO's input script format.

**Supported formats:**
- `fm2` - NES/Game Boy (text-based)
- `fm3` - Genesis/SNES (text-based)  
- `r08` - NES verification standard (binary)

**Input script format:**
```
<milliseconds> <KEY> <ACTION>
100 START DOWN
600 START UP
1200 A DOWN
1300 A UP
```

### 2. Test Runner (`scripts/test_tas_validation.py`)

Orchestrates validation testing across all systems.

**Features:**
- Finds ROMs in `test_roms/` directory
- Finds TAS files in `/tmp/test_tas/{system}/` directories
- Converts TAS → input script automatically
- Runs headless emulator with inputs
- Verifies frame output (non-black)
- Reports results per test

### 3. Setup Guide (`scripts/setup_test_roms.py`)

Provides instructions for obtaining test ROMs and TAS files.

## Quick Start

### 1. Build the project
```bash
cd /Users/alexwaldmann/Desktop/AIO\ Server
make build
```

### 2. Setup test directories
```bash
python3 scripts/setup_test_roms.py --setup
```

### 3. Obtain test ROMs and TAS files

See `setup_test_roms.py` for detailed source information. In summary:
- **ROMs**: Obtain from your own collection or legal ROM archives
- **TAS files**: Download from https://www.tasvideos.org

Example structure:
```
test_roms/
  game1.nes
  game2.md
  game3.smc
  game4.gb

/tmp/test_tas/nes/
  game1.fm2
/tmp/test_tas/genesis/
  game2.fm3
/tmp/test_tas/snes/
  game3.fm3
/tmp/test_tas/gb/
  game4.fm2
```

### 4. Run tests
```bash
# Quick 3-second validation per ROM
python3 scripts/test_tas_validation.py

# Extended 10-second runs
python3 scripts/test_tas_validation.py 10000
```

## Manual Usage

### Convert a TAS file
```bash
python3 scripts/tas_converter.py /path/to/game.fm2 > /tmp/input.script
python3 scripts/tas_converter.py /path/to/game.r08 r08 > /tmp/input.script
python3 scripts/tas_converter.py /path/to/game.fm3 fm3 50 > /tmp/input.script
```

### Run emulator with TAS inputs
```bash
# NES with TAS input
./build/bin/AIOServer \
  --headless \
  --rom test_roms/game.nes \
  --headless-max-ms 5000 \
  --input-script /tmp/input.script \
  --headless-dump-ppm /tmp/frame.ppm \
  --headless-assert-nonblack

# Genesis
./build/bin/AIOServer \
  --headless \
  --rom test_roms/game.md \
  --headless-max-ms 5000 \
  --input-script /tmp/input.script \
  --headless-assert-nonblack

# SNES
./build/bin/AIOServer \
  --headless \
  --rom test_roms/game.smc \
  --headless-max-ms 5000 \
  --input-script /tmp/input.script \
  --headless-assert-nonblack

# Game Boy
./build/bin/AIOServer \
  --headless \
  --rom test_roms/game.gb \
  --headless-max-ms 5000 \
  --input-script /tmp/input.script \
  --headless-assert-nonblack
```

## Test Results Interpretation

### Success indicators:
- **Exit code 0** - Test passed
- **"non-black" logged** - Frame output is rendering correctly
- **No timeout** - Emulator completed requested duration without hanging

### Failure indicators:
- **Exit code non-zero** - Test failed
- **"black" in output** - Frame was completely black (rendering issue)
- **Process hangs** - Emulator logic or infinite loop problem
- **"ROM not found"** - Path or file issue

## Input Script Format Reference

```
# Lines starting with # are comments
# Format: <milliseconds> <KEY> <ACTION>

# Standard controller buttons
100 A DOWN          # Press A button at 100ms
200 A UP            # Release A button at 200ms
300 B PRESS         # Alternative syntax
400 SELECT DOWN
500 START DOWN
600 START UP
700 UP DOWN
800 DOWN DOWN
900 LEFT DOWN
1000 RIGHT DOWN
1100 L DOWN
1200 R UP

# Actions (case-insensitive):
#   DOWN, PRESS, PRESSED - button pressed
#   UP, RELEASE, RELEASED - button released

# Buttons (must match enum):
#   A, B, SELECT, START, UP, DOWN, LEFT, RIGHT, L, R
```

## Emulator Controls (via Input Script)

Each system maps standard controller to its native buttons:

**NES:**
- A = NES A
- B = NES B
- Select = NES Select
- Start = NES Start
- D-Pad = Direction pad

**Genesis/Mega Drive:**
- A = Genesis A
- B = Genesis B
- X = Genesis X
- Y = Genesis Y (if supported)
- Select = Genesis Mode
- Start = Genesis Start
- D-Pad = Direction pad

**SNES:**
- A, B, X, Y = SNES buttons
- Select = SNES Select
- Start = SNES Start
- L, R = shoulder buttons
- D-Pad = Direction pad

**Game Boy:**
- A = GB A
- B = GB B
- Select = GB Select
- Start = GB Start
- D-Pad = Direction pad

## Troubleshooting

### "ROM not found"
- Verify ROM path in test_roms/ directory
- Check file extension matches system: .nes, .md, .smc, .gb

### Black frame output
- Rendering issue in that emulator's graphics core
- Check graphics implementation in that emulator's source
- Run individual system's tests for diagnosis

### Timeout/Hang
- Infinite loop in emulator core
- Check CPU execution logic
- Enable debug logging: check debug.log for crash messages

### Input script conversion fails
- Verify TAS file format (fm2, fm3, r08)
- Check TASVideos download is complete (not truncated)
- Try manual conversion first: `python3 scripts/tas_converter.py file.fm2`

## Advanced: Creating Synthetic Tests

If you only have a few ROMs, create minimal input sequences:

```bash
# Create simple test: press START on title screen for 600ms
cat > /tmp/simple_test.script << EOF
100 START DOWN
700 START UP
EOF

# Run test
./build/bin/AIOServer \
  --headless \
  --rom test_roms/game.nes \
  --headless-max-ms 2000 \
  --input-script /tmp/simple_test.script \
  --headless-assert-nonblack
```

If the emulator responds to START input, the input system is working.

## Performance Notes

- Duration per test: 3-5 seconds (default)
- Full test suite (4 systems × 2 ROMs): ~30-40 seconds
- Screenshot capture: minimal overhead (PPM format, uncompressed)

## Related Files

- [MainWindow_Emulation.cpp](../src/gui/mainwindow/MainWindow_Emulation.cpp) - Input script parsing & emulation loop
- [InputTypes.h](../include/input/InputTypes.h) - Button definitions
- [emulator-core.instructions.md](../.github/instructions/emulator-core.instructions.md) - Core validation rules
- [TASVideos](https://www.tasvideos.org) - TAS file repository
