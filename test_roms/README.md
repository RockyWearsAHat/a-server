# Test ROMs Directory

Place your GBA ROM files here for testing and development.

**Note:** ROM files are not committed to version control (see `.gitignore`).

## Supported Formats

- `.gba` - Game Boy Advance ROMs
- `.gb` / `.gbc` - Game Boy / Game Boy Color (if supported)
- `.sav` - Save files (auto-generated)

## Usage

### Running ROMs with GUI

```bash
# Launch GUI, then select ROM from File menu
./build/bin/AIOServer

# Load a ROM directly on startup (GUI mode)
./build/bin/AIOServer --rom test_roms/your_game.gba
# OR using short flag:
./build/bin/AIOServer -r test_roms/your_game.gba
```

### Headless Testing (No GUI)

```bash
# Run for 5 seconds and dump framebuffer
./build/bin/AIOServer --headless --rom test_roms/your_game.gba --headless-max-ms 5000

# Run with debug tracing enabled
./build/bin/AIOServer --headless --rom test_roms/your_game.gba --headless-max-ms 2000
```

### Debug Output

**All console output (stdout/stderr) goes to `debug.log` in the project root!**

To monitor debug output in real-time:

```bash
# In one terminal, run the emulator:
./build/bin/AIOServer -r test_roms/OG-DK.gba

# In another terminal, tail the log:
tail -f debug.log
```

Available debug environment variables:

```bash
# PPU (Graphics) Tracing
AIO_TRACE_PPU_OAM=1              # Trace OAM (sprite) processing
AIO_TRACE_PPU_OAM_SPR=1          # Detailed sprite information
AIO_TRACE_PPU_OAM_SPR_MAX=N      # Max sprites to log (default: 16)
AIO_TRACE_PPU_OAM_SPR_FRAME=N    # Log sprites only on frame N
AIO_TRACE_PPU_OBJPIX=1           # Trace individual sprite pixel rendering
AIO_TRACE_PPU_OBJPIX_FRAME=N     # Trace pixels only on frame N
AIO_TRACE_PPU_OBJPIX_X=N         # Trace specific X coordinate
AIO_TRACE_PPU_OBJPIX_Y=N         # Trace specific Y coordinate
AIO_TRACE_PPU_BGPIX=1            # Trace background pixel rendering
AIO_TRACE_PPU_PIX=1              # Trace final pixel output
AIO_TRACE_NES_PALETTE=1          # Trace Classic NES Series palette handling
AIO_PPU_SWAP_4BPP_NIBBLES=1      # Swap nibbles in 4bpp tile reads
AIO_PPU_IGNORE_WINDOWS=1         # Disable window masking
AIO_PPU_DISABLE_COLOR_EFFECTS=1  # Disable blending/brightness effects

# APU (Audio) Tracing
AIO_TRACE_AUDIO_STATS=1          # Periodic audio buffer stats
AIO_TRACE_GBA_SPAM=1             # Verbose audio/timer logs

# CPU/Memory Tracing
AIO_TRACE_PC_EVERY_CYCLES=N      # Log PC every N cycles
AIO_TRACE_EEPROM_IO=1            # Log EEPROM operations

# Performance
AIO_GBA_TARGET_FPS=N             # Override target FPS (default: 60)
AIO_GBA_INPUT_CHUNKS=N           # Input processing chunk size

# Misc
AIO_GBA_BIOS=path                # Use custom BIOS file
```

Example usage:

```bash
# Trace sprites on frame 60 with audio stats
AIO_TRACE_PPU_OBJPIX=1 AIO_TRACE_PPU_OBJPIX_FRAME=60 AIO_TRACE_AUDIO_STATS=1 \
  ./build/bin/AIOServer -r test_roms/your_game.gba

# Then monitor the log:
tail -f debug.log | grep -E "OBJPIX|AUDIO"
```

### ROM Sweep Testing

The `rom_sweep.py` script tests multiple ROMs in headless mode:

```bash
# Test all ROMs in directory (10 seconds each)
./scripts/rom_sweep.py --roms-dir test_roms/ --timeout-s 10

# Custom timeout (30 seconds per ROM)
./scripts/rom_sweep.py --roms-dir ~/Desktop/ROMs/GBA --timeout-s 30
```

**Note:** `--timeout-s` is a rom_sweep.py flag (converted internally to `--headless-max-ms` for AIOServer)

## Save Files

Save files are automatically created alongside ROM files:

- `game.gba` → `game.sav` (SRAM/EEPROM/Flash)
- Save files use the same base name as the ROM

## Debugging Tips

1. **Check debug.log first** - All emulator output goes there
2. **Use environment variables** for specific traces (see list in src files)
3. **Headless mode** is faster for automated testing
4. **ROM sweep script** helps catch regressions across multiple games

## Legal Notice

Only use ROM files that you legally own or have permission to use.
