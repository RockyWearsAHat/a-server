# Input Scripts for Automated Testing

This directory contains input scripts for automated testing of games via the `--input-script` command-line option.

## Script Format

Each line follows the format:

```
<time_ms> <KEY> <DOWN|UP>
```

- `time_ms`: Milliseconds from emulation start when the input should occur
- `KEY`: One of `A`, `B`, `START`, `SELECT`, `UP`, `DOWN`, `LEFT`, `RIGHT`, `L`, `R`
- `DOWN|UP`: Whether the key is being pressed or released

Lines starting with `#` are comments.

## Available Scripts

| Script                  | Purpose                                              |
| ----------------------- | ---------------------------------------------------- |
| `boot_and_start.input`  | Basic boot sequence - waits then presses START and A |
| `movement_test.input`   | Tests all D-pad directions                           |
| `button_mash.input`     | Rapid button testing for input validation            |
| `platformer_jump.input` | Run-and-jump patterns for platformers                |
| `menu_navigation.input` | Typical menu navigation patterns                     |

## Usage

### GUI Mode (Wall Clock Time)

```bash
./build/bin/AIOServer --rom "test_roms/game.gba" --input-script "test_inputs/boot_and_start.input"
```

### Headless Mode (Emulated Time - Deterministic)

```bash
./build/bin/AIOServer --headless \
  --rom "test_roms/game.gba" \
  --input-script "test_inputs/boot_and_start.input" \
  --headless-max-ms 10000 \
  --headless-dump-ppm /tmp/result.ppm \
  --headless-dump-ms 8000
```

## Timebase

- **Headless mode**: Uses emulated time by default (`AIO_INPUT_SCRIPT_TIMEBASE=EMU`)
- **GUI mode**: Uses wall clock time by default

Override with environment variable:

```bash
AIO_INPUT_SCRIPT_TIMEBASE=WALL ./build/bin/AIOServer ...
AIO_INPUT_SCRIPT_TIMEBASE=EMU ./build/bin/AIOServer ...
```

## Creating New Scripts

1. Play the game manually and note the timing of key inputs
2. Create a new `.input` file with the sequence
3. Test with headless mode to verify timing
4. Adjust timestamps as needed

For frame-perfect inputs, remember GBA runs at ~59.73 FPS (~16.74ms per frame).
