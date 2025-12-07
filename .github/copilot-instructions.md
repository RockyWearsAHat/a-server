# AIO Entertainment System - Copilot Instructions

## GOAL:

Build a fully functional and working AIO ("all in one") entertainment system for a home theater PC. This includes a multi-platform emulator core (GBA, Nintendo Switch, etc.) integrated into a Qt6-based GUI application with features like game library management, input handling, audio/video output, and vast coverage of game-specific fixes for compatibility.

## CURRENT STEP:

**Memory Collision Bug Found!** Investigation shows:

1. DKC uses address 0x3001500 for both audio sample buffer AND a jump table
2. Game's audio mixing code writes samples like 0xf6fe0a18 to 0x3001500
3. Later, code at 0x30032fc does `LDR R11, [R11, R0]` where R11=0x3001500
4. This loads the audio sample (0xf6fe0a18) as a code pointer, causing crash
5. The game runs fine on real hardware, so something in our emulation is wrong

**Root cause hypotheses:**

- Game expects audio buffer at different address (we're writing to wrong location)
- Game's IRQ handler runs at wrong time (before jump table is restored)
- IWRAM layout is different than expected

**Previous fixes this session:**

1. Verified sprites are rendering (13,739 non-backdrop pixels)
2. LZ77 SWI 0x11 decompression working
3. DMA transfers to OBJ VRAM working

## NEXT STEP:

1. **Investigate audio buffer location**: Find where DKC's audio mixing code gets the buffer address from - it might be reading from wrong location
2. **Check IWRAM initialization**: Game copies code to IWRAM on boot - verify this happens correctly
3. **Compare with other emulators**: Look at how mGBA/VBA handle DKC's audio mixing

## INSTRUCTIONS FOR COPILOT:

- Analyze the GOAL.
- If CURRENT STEP is empty OR NOT PROGRESSED UPON BY THE NEW REQUEST, define Step 1 AS THE REQUEST.
- Implement the step, if testing or wandering away from the current step happens find your way back.
- Update CURRENT STEP and keep this copilot-instructions.md document updated.
- Append a NEXT STEP section.
- Do NOT ask the user questions except for direct yes or no or a.b.c options.
- Return useful insights with your knowledge of the project after code implementation to complete the request.

## Project Overview

A multi-platform emulator and home entertainment system built with C++20/Qt6. Currently implements GBA and (skeleton) Nintendo Switch emulation. The ultimate goal is a Linux-based OS for TV that integrates emulators, NAS, streaming apps, and Steam via Windows VM.

## Architecture

### Emulator Core Pattern

Each emulator follows a component-based architecture in `src/emulator/<platform>/`:

- **CPU Core** (`ARM7TDMI.cpp` / `CpuCore.cpp`) - Instruction execution, pipeline, mode switching
- **Memory Manager** (`GBAMemory.cpp` / `MemoryManager.cpp`) - Memory mapping, IO registers, save handling
- **PPU/GPU** (`PPU.cpp` / `GpuCore.cpp`) - Graphics rendering to framebuffer
- **APU** (`APU.cpp`) - Audio synthesis with SDL audio callback
- **Main Wrapper** (`GBA.cpp` / `SwitchEmulator.cpp`) - Ties components together, exposes `LoadROM()`, `Step()`, `Reset()`

### Key Relationships

```
MainWindow (Qt GUI) -> GBA/SwitchEmulator -> CPU + Memory + PPU + APU
                    -> InputManager (keyboard/gamepad)
```

- GUI polls `Step()` in 16ms timer (~60 FPS), runs ~280,000 cycles/frame for GBA
- Audio uses SDL callback (`audioCallback`) that pulls from APU ring buffer

### Game-Specific Fixes

`GameDB.cpp` contains per-game overrides (game code → patches, save type). Critical pattern:

```cpp
{"AA2E", {"AA2E", "SMA2 (Alt)", SaveType::EEPROM_64K, {
    {0x494, 0x03002BD0},  // Patch literal pool
    {0x3FFFD0, 0xE92D4000}, // Inject ARM code at ROM end
}}}
```

When adding game-specific memory hacks in `GBAMemory.cpp`, **always check `gameCode`**:

```cpp
bool isSMA2 = (gameCode == "AMQE" || gameCode == "AA2E" || ...);
if (isSMA2) { /* game-specific behavior */ }
```

## Build & Test

### Build Commands

```bash
cd build && cmake .. && make -j8
# Binaries output to build/bin/
```

### Key Executables

- `bin/AIOServer` - Main Qt application
- `bin/HeadlessTest` - GBA emulator test (loads `SMA2.gba` from current dir by default)
- `bin/CPUTests` / `bin/EEPROMTests` - GoogleTest unit tests

### Debugging Workflow

1. **HeadlessTest** for emulator debugging: `./bin/HeadlessTest path/to/rom.gba 2>&1 | head -500`
2. Common debug patterns in code (search for and enable as needed):
   - `[Memory Watch]` - Memory write tracing in `GBAMemory::Write8`
   - `[Trace]` - CPU instruction tracing in `ARM7TDMI::Step`
   - `[Loop Stuck]` - Infinite loop detection in HeadlessTest

### Running the App

```bash
./bin/AIOServer 2>&1 | tee logs.txt  # GUI with logging
```

Set ROM directory in Settings → Browse Folder. ROMs are discovered recursively by extension (`.gba`, `.nso`, etc.)

## Conventions

### Namespaces

- `AIO::Emulator::GBA::` for GBA emulator
- `AIO::Emulator::Switch::` for Switch emulator
- `AIO::GUI::` for Qt GUI code
- `AIO::Input::` for input handling

### Memory Addresses (GBA)

- `0x04000130` - KEYINPUT register (0 = pressed)
- `0x04000200-202` - IE/IF interrupt registers
- `0x03007FFC` - User ISR handler pointer
- Document game-specific addresses in `docs/` (see `SMA2_Fix.md` as example)

### Input System

GBA button bits in KEYINPUT: `0=A, 1=B, 2=Select, 3=Start, 4=Right, 5=Left, 6=Up, 7=Down, 8=R, 9=L`
InputManager combines keyboard (`keyboardState`) + gamepad (`gamepadState`) with bitwise AND.

## Common Pitfalls

1. **Game-specific code must check game code** - Memory hacks that work for one game break others
2. **8-bit writes to IO** - GBA has quirky 8-bit write behavior (some regs read-only, some write-only)
3. **Thumb vs ARM mode** - Track `thumbMode` carefully, especially during IRQ returns
4. **EEPROM save size** - 4Kbit vs 64Kbit affects addressing (see `GameDB::DetectSaveType`)
5. **Audio buffer underrun** - APU ring buffer must stay ahead of SDL callback

## Future Work (Not Yet Implemented)

- NAS server (React/TypeScript frontend)
- Streaming service integration
- Steam via Windows VM passthrough
- Additional console emulators (Sony, Microsoft, other Nintendo)
- Resolution scaling with aspect ratio preservation
