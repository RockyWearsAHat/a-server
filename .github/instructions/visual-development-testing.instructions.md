---
applyTo: "**"
description: "Instructions for visual development testing - capturing A/V output for USER verification, plus automated sanity checks the agent CAN perform."
---

# Visual Development Testing Instructions

## 🚨 CRITICAL: AGENT CANNOT VIEW IMAGES OR VIDEO

**OVER ALL OTHER FORMS OF TESTING, PREFER THE INBUILT VS CODE DEBUGGER FOR CAREFUL STEP-BY-STEP INSPECTION OF INTERNAL STATE, BREAKPOINTS ON SPECIFIC SECTIONS, AND CLEAR AND CLEAN TRACKING OF DATA. ALWAYS PREFER THE VS CODE DEBUGGER, LOGGING, OR UNIT TESTING** Visual testing is a last resort when you need to verify actual rendered output or audio quality that can't be captured by unit tests, logs, or step through debugging.

**AI agents CANNOT see, view, or analyze:**

- MP4 video files
- PNG/PPM image files
- Any visual output

When you run `open /tmp/file.mp4`, it opens on the USER'S screen. The agent has NO WAY to see or evaluate the contents. **Do not pretend to have verified visual output.**

## What Agents CAN Do (Automated Verification)

1. **Check files exist and have non-zero size**
2. **Use ffprobe/ffmpeg to get metadata** (duration, dimensions, audio levels)
3. **Compare file hashes** for regression detection (same input = same output)
4. **Analyze PPM pixel data** programmatically (check for all-black frames, etc.)
5. **Run unit tests** that verify specific rendering behaviors
6. **Ask the user** to verify visual output

## What Agents CANNOT Do

1. ❌ Look at an image and say "this looks correct"
2. ❌ Watch a video and describe what's shown
3. ❌ Verify that rendered output matches expected game graphics
4. ❌ Determine if tiles are "scrambled" vs "correct" by viewing

## Proper Workflow for Visual Changes

1. **Make code changes**
2. **Run unit tests** - these CAN be verified by the agent
3. **Generate A/V output** for the user to review
4. **Run automated sanity checks** (file size, duration, not-all-black)
5. **Ask the user**: "I've generated /tmp/output.mp4 - please check if this looks correct"
6. **Wait for user feedback** before concluding the fix works

---

## Automated Sanity Checks (Agent CAN Verify)

### Check Video File Properties

```bash
# Verify video was created and has content
ffprobe -v error -show_entries format=duration,size -of csv=p=0 /tmp/test.mp4
# Output: "5.000000,1234567" (duration in seconds, size in bytes)

# Check video dimensions
ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 /tmp/test.mp4
# Output: "240,160" (GBA resolution)
```

### Check Audio Levels (Not Silent)

```bash
# Verify audio has actual content (not silence)
ffmpeg -i /tmp/test.mp4 -af volumedetect -f null - 2>&1 | grep -E "mean_volume|max_volume"
# mean_volume should NOT be -91.0 dB (silence)
```

### Check Frame is Not All Black (PPM Analysis)

```bash
# Dump a frame and check it's not entirely black
./build/bin/AIOServer --headless --rom "test_roms/game.gba" \
  --headless-max-ms 3000 --headless-dump-ppm /tmp/frame.ppm --headless-dump-ms 2000

# Check if frame has any non-black pixels (PPM P6 format)
# Skip header (first 3 lines), check if any byte > 0
tail -c +100 /tmp/frame.ppm | od -A n -t u1 | tr ' ' '\n' | grep -v '^$' | sort -u | head -5
# If only "0" appears, frame is all black
```

### Hash-Based Regression Check

```bash
# Save hash of known-good output
md5 /tmp/known_good.mp4 > /tmp/expected_hash.txt

# After changes, compare
md5 /tmp/new_output.mp4 | diff - /tmp/expected_hash.txt
# If hashes match, output is identical (good for non-visual changes)
```

---

## Purpose

These tools help capture application output for the **USER to verify visually**. The agent's role is to:

1. Generate the output files
2. Run automated sanity checks
3. Present files to the user for visual verification
4. Iterate based on user feedback

**This is NOT a replacement for unit tests.** Unit tests prevent regressions and CAN be verified by the agent. Visual verification requires human eyes.

---

## ⚠️ CRITICAL: Application Logging

**All AIOServer output (including trace logs, audio stats, and debug info) goes to `debug.log` by default**, NOT to stdout/stderr. This is true regardless of other command-line options unless you explicitly override it.

```bash
# By default, all output goes to debug.log in the current directory
./build/bin/AIOServer --headless --rom "test_roms/game.gba" --headless-max-ms 5000
# Check logs: cat debug.log | grep -E "APU|AUDIO|FIFO"

# To redirect logs to a different file:
./build/bin/AIOServer --headless --rom "test_roms/game.gba" -l /tmp/my_debug.log

# To check trace output after a run:
tail -100 debug.log | grep -E "AUDIO|APU|FIFO|underrun"
```

**When debugging audio/video issues, ALWAYS check `debug.log`** for trace output—it won't appear in your terminal!

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
# Enable detailed audio statistics in logs (output goes to debug.log!)
AIO_TRACE_AUDIO_STATS=1 ./build/bin/AIOServer --headless \
  --rom "test_roms/game.gba" \
  --headless-max-ms 5000

# Then check debug.log for audio stats:
cat debug.log | grep -i "audio\|AUDIO\|buffer\|sample"

# Check for audio underruns, buffer levels, sample rates
AIO_TRACE_AUDIO_STATS=1 ./build/bin/AIOServer --headless \
  --rom "test_roms/DKC.gba" \
  --headless-max-ms 3000
tail -100 debug.log | grep -E "AUDIO|APU|underrun|FIFO"
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

**REMEMBER:** All trace output goes to `debug.log` by default. After running with trace env vars, check:

```bash
tail -200 debug.log | grep -E "AUDIO|APU|FIFO|Timer"
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

# ⚠️ CHECK LOGS (output goes to debug.log by default!)
tail -100 debug.log | grep -E "AUDIO|APU|error|warning"
cat debug.log | grep -E "underrun|overflow|FIFO"
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
   ```

   ### After capturing, analyze results and report or continue with visual/audio evidence

4. **For complex interactions, combine with input scripts**

   ```bash
   # Create script, run with it, record A/V
   echo "2000 START DOWN\n2100 START UP" > /tmp/test.input
   ./build/bin/AIOServer --headless --rom "test_roms/game.gba" \
     --input-script /tmp/test.input \
     --record-av /tmp/with_inputs.mp4 \
     --headless-max-ms 10000
   ```

5. **Examine the recording** - Does it show and sound like the expected result? Have we improved? If not or the users request still hasn't been met, please iterate on the code and repeat/continue.

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
