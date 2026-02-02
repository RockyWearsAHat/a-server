---
applyTo: "**"
description: "Instructions for hands-free visual development testing using screen capture, audio monitoring, recordings, and automated input scripts to see exactly what the user sees rather than relying on logs alone."
---

# Visual Development Testing Instructions

## ⚠️ MANDATORY FOR ALL AGENTS IN THIS WORKSPACE

**These instructions apply to EVERY agent working in this codebase.** When developing user-facing features (graphics, audio, input, UI), you MUST use these visual testing capabilities to verify changes work correctly. Do not rely solely on logs—SEE what the user sees, HEAR what the user hears.

## Purpose

These instructions enable the agent to **truly see and hear** application output during development—exactly as the user experiences it—rather than inferring behavior from logs alone. This dramatically accelerates debugging and development by providing immediate sensory feedback on changes.

**This is NOT a replacement for unit tests.** Unit tests prevent regressions. Visual/audio testing lets the agent verify changes work correctly in real-time by observing actual screen output, hearing audio, and interacting via input scripts.

### Why This Matters

- **Logs tell you what the code did. Visuals/audio show you what the user experienced.**
- A log saying "frame rendered" doesn't tell you if it looks correct
- A log saying "audio sample generated" doesn't tell you if it sounds right
- Screen capture and recording let you SEE and HEAR the actual result
- This is MULTIPLES faster than hunting through thousands of log lines

---

## 🎬 PRIMARY METHOD: Built-in A/V Recording

**This is the BEST and PREFERRED method for all visual/audio testing.** It captures video frames directly from the PPU framebuffer and audio samples directly from the SDL callback—perfect sync, no external dependencies, cross-platform.

```bash
# Record 5 seconds of gameplay with PERFECT A/V sync
./build/bin/AIOServer --headless \
  --rom "test_roms/DKC.gba" \
  --record-av /tmp/test.mp4 \
  --headless-max-ms 5000

# Play the result to verify
open /tmp/test.mp4   # macOS
# xdg-open /tmp/test.mp4  # Linux

# Combined with input script for deterministic testing
./build/bin/AIOServer --headless \
  --rom "test_roms/game.gba" \
  --input-script test_inputs/boot_and_start.input \
  --record-av /tmp/with_inputs.mp4 \
  --headless-max-ms 10000
```

### Why This is the ONLY Method You {SHOULD} Need

- **Perfect A/V sync** - Video and audio captured from the same source
- **Cross-platform** - Works on macOS, Linux, Windows
- **No external setup** - No BlackHole, no screen recording permissions
- **Headless support** - Works in CI/CD, no display needed
- **Captures actual output** - Internal PPU framebuffer + SDL audio callback

---

## Additional Capabilities

### 1. Application Frame Dumps (Built-in)

The AIOServer has built-in headless frame capture that dumps the emulator's internal framebuffer:

```bash
# Dump frame after 2 seconds of emulation to a PPM file
./build/bin/AIOServer --headless \
  --rom "test_roms/game.gba" \
  --headless-max-ms 5000 \
  --headless-dump-ppm /tmp/frame.ppm \
  --headless-dump-ms 2000

# Convert PPM to PNG for easier viewing
convert /tmp/frame.ppm /tmp/frame.png

# Assert frame is not entirely black (sanity check)
./build/bin/AIOServer --headless \
  --rom "test_roms/game.gba" \
  --headless-max-ms 3000 \
  --headless-dump-ppm /tmp/frame.ppm \
  --headless-dump-ms 2000 \
  --headless-assert-nonblack
```

### 2. Automated Input Scripts

The AIOServer supports deterministic input playback for testing game interactions:

**Script Format:** `<time_ms> <KEY> <DOWN|UP>`

**Available Keys:** `A`, `B`, `START`, `SELECT`, `UP`, `DOWN`, `LEFT`, `RIGHT`, `L`, `R`

**Example Script** (`test_inputs/boot_and_start.input`):

```
# Wait for boot, then press Start
2000 START DOWN
2100 START UP

# Navigate menu: press A
3000 A DOWN
3100 A UP

# Move character right
4000 RIGHT DOWN
5000 RIGHT UP

# Jump with A while moving right
5500 RIGHT DOWN
5600 A DOWN
5700 A UP
6500 RIGHT UP
```

**Running with Input Script:**

```bash
./build/bin/AIOServer \
  --rom "test_roms/game.gba" \
  --input-script "test_inputs/boot_and_start.input"

# Headless with input script (uses emulated time for determinism)
./build/bin/AIOServer --headless \
  --rom "test_roms/game.gba" \
  --input-script "test_inputs/boot_and_start.input" \
  --headless-max-ms 10000 \
  --headless-dump-ppm /tmp/after_inputs.ppm \
  --headless-dump-ms 8000
```

### 3. Audio Monitoring and Recording

Monitor and capture audio output to verify sound quality during development.

#### Audio Statistics Tracing (Built-in)

```bash
# Enable detailed audio statistics in logs
AIO_TRACE_AUDIO_STATS=1 ./build/bin/AIOServer --headless \
  --rom "test_roms/game.gba" \
  --headless-max-ms 5000 2>&1 | grep -i "audio\|AUDIO\|buffer\|sample"

# Check for audio underruns, buffer levels, sample rates
AIO_TRACE_AUDIO_STATS=1 ./build/bin/AIOServer --headless \
  --rom "test_roms/DKC.gba" \
  --headless-max-ms 3000 2>&1 | tee /tmp/audio_trace.log
```

#### Direct Audio Capture (RECOMMENDED - No Setup Required)

```bash
# Capture audio directly from SDL buffer to WAV file
# This captures EXACTLY what the emulator outputs - no system audio routing needed!
./build/bin/AIOServer --headless \
  --rom "test_roms/game.gba" \
  --headless-max-ms 5000 \
  --dump-audio /tmp/audio.wav

# The WAV file contains the exact audio samples from the emulator
# Play it: afplay /tmp/audio.wav
```

#### Audio Quality Checks

```bash
# Check if audio file has content (not silent)
ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 /tmp/audio_capture.wav

# Get audio statistics (requires sox)
sox /tmp/audio_capture.wav -n stat 2>&1

# Check for clipping/distortion
sox /tmp/audio_capture.wav -n stats 2>&1 | grep -i "pk\|clip\|max"
```

#### What to Listen/Look For in Audio

- **Silence**: No audio output at all (APU not running, audio device issue)
- **Crackling/Popping**: Buffer underruns, timing issues
- **Wrong Pitch**: Timer frequency miscalculation
- **Missing Channels**: Square wave 1/2, wave, noise not playing
- **Distortion**: Clipping, incorrect mixing
- **Stuttering**: Frame timing or audio sync issues

---

## Development Testing Workflow

### Quick Visual Check After Code Change

Use this workflow to immediately see if a change works:

```bash
# 1. Build the project
make build

# 2. Record 5 seconds of gameplay
./build/bin/AIOServer --headless \
  --rom "test_roms/MegaManBattleNetwork.gba" \
  --record-av /tmp/visual_check.mp4 \
  --headless-max-ms 5000

# 3. Open and examine the result
open /tmp/visual_check.mp4
```

### Automated Visual Regression Check

Compare visual output before and after changes:

```bash
# Capture "before" state (on main branch or before changes)
./build/bin/AIOServer --headless \
  --rom "test_roms/Metroid - Zero Mission (USA).gba" \
  --record-av /tmp/before.mp4 \
  --headless-max-ms 5000

# Make code changes, rebuild...
make build

# Capture "after" state
./build/bin/AIOServer --headless \
  --rom "test_roms/Metroid - Zero Mission (USA).gba" \
  --record-av /tmp/after.mp4 \
  --headless-max-ms 5000

# Compare by watching both videos
open /tmp/before.mp4 /tmp/after.mp4
```

---

## Recommended Test ROMs

| ROM                                          | Purpose           | Key Behaviors to Verify                 |
| -------------------------------------------- | ----------------- | --------------------------------------- |
| `test_roms/MegaManBattleNetwork.gba`         | General gameplay  | Menu navigation, battle system, sprites |
| `test_roms/Metroid - Zero Mission (USA).gba` | Action platformer | Smooth scrolling, animation, audio cues |
| `test_roms/DKC.gba`                          | Platformer        | Backgrounds, sprite scaling, music      |
| `test_roms/OG-DK.gba`                        | Simple classic    | Basic rendering, timing                 |
| `test_roms/SMA2.gba`                         | Mario platformer  | Tile rendering, physics feel            |

---

## Common Visual Issues to Check

### Graphics Issues

- **Black screen**: Game not booting, check BIOS/ROM loading
- **Corrupted tiles**: PPU rendering bug, check VRAM access
- **Wrong colors**: Palette issue, check palette RAM
- **Flickering sprites**: OAM timing, check sprite evaluation
- **Screen tearing**: V-blank timing issue

### Audio Issues

- **No sound**: APU not running, check audio initialization
- **Crackling/popping**: Buffer underrun, check sample generation rate
- **Wrong pitch**: Timer frequency calculation issue
- **Channels missing**: Individual channel enable/disable logic

### Input Issues

- **Unresponsive**: KEYINPUT register not updating
- **Stuck keys**: Key release not being processed
- **Wrong mapping**: SDL keycode to GBA button mapping

---

## Environment Variables for Debugging

```bash
# Verbose GBA emulation tracing
AIO_TRACE_GBA_SPAM=1

# Audio statistics (buffer levels, underruns)
AIO_TRACE_AUDIO_STATS=1

# Input script timebase (EMU = emulated time, WALL = wall clock)
AIO_INPUT_SCRIPT_TIMEBASE=EMU

# Disable streaming features (cleaner headless runs)
AIO_ENABLE_STREAMING=0
```

---

## Quick Reference Commands

```bash
# Build
make build

# Run with ROM (GUI)
./build/bin/AIOServer --rom "test_roms/game.gba"

# Run headless for N ms
./build/bin/AIOServer --headless --rom "test_roms/game.gba" --headless-max-ms 5000

# ⭐ Record A/V (THIS IS THE PRIMARY TESTING METHOD)
./build/bin/AIOServer --headless --rom "test_roms/game.gba" \
  --record-av /tmp/test.mp4 --headless-max-ms 5000

# Capture single frame at specific time
./build/bin/AIOServer --headless --rom "test_roms/game.gba" \
  --headless-max-ms 5000 --headless-dump-ppm /tmp/frame.ppm --headless-dump-ms 3000

# Dump audio only to WAV
./build/bin/AIOServer --headless --rom "test_roms/game.gba" \
  --headless-max-ms 5000 --dump-audio /tmp/audio.wav

# Run with input script
./build/bin/AIOServer --rom "test_roms/game.gba" --input-script inputs.txt

# Kill running AIOServer instances
pkill -f AIOServer
```

---

## Agent Workflow: Verifying a Change

When making a code change, follow this workflow to verify it works:

1. **Understand the expected behavior** - What should the user see/hear?

2. **Build the project**

   ```bash
   make build
   ```

3. **Record A/V to verify the change (PRIMARY METHOD)**

   ```bash
   # This is the BEST way to verify changes - captures exactly what the emulator outputs
   ./build/bin/AIOServer --headless --rom "test_roms/game.gba" \
     --record-av /tmp/test_result.mp4 --headless-max-ms 5000

   # Open to review
   open /tmp/test_result.mp4
   ```

4. **For complex interactions, combine with input scripts**

   ```bash
   # Create script, run with it, record A/V
   echo "2000 START DOWN\n2100 START UP" > /tmp/test.input
   ./build/bin/AIOServer --headless --rom "test_roms/game.gba" \
     --input-script /tmp/test.input \
     --record-av /tmp/with_inputs.mp4 \
     --headless-max-ms 10000
   ```

5. **Examine the recording** - Does it show and sound like the expected result?

6. **For before/after comparisons, capture both**

   ```bash
   # Before changes
   ./build/bin/AIOServer --headless --rom "test_roms/game.gba" \
     --record-av /tmp/before.mp4 --headless-max-ms 5000

   # After changes (rebuild first)
   make build && ./build/bin/AIOServer --headless --rom "test_roms/game.gba" \
     --record-av /tmp/after.mp4 --headless-max-ms 5000
   ```

---

## Notes

- **`--record-av` is the PRIMARY method** - captures directly from internal buffers with perfect A/V sync
- **PPM frame dumps** show single frames, useful for quick static checks
