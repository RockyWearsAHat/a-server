# Plan: Diagnose OG-DK (Classic NES Series: Donkey Kong) 8-Color Graphics Issue

**Status:** 🟡 DIAGNOSIS COMPLETE - REQUIRES FURTHER INVESTIGATION
**Goal:** Understand why OG-DK (FDKE01) shows only 8 colors instead of full palette

---

## Context

### Executive Summary

OG-DK (Classic NES Series: Donkey Kong) shows corrupted graphics with only 8 distinct colors. This issue exists at ALL commits tested, including known-good d20cc15 and 31b507b, meaning it predates recent changes.

### Diagnostic Findings

#### Display Configuration

- **Mode 0** (tiled backgrounds), NOT Mode 4 (bitmap)
- **DISPCNT = 0x160**: BG0 enabled, OBJ disabled, 1D OBJ mapping
- **BG0CNT = 0x8d04**: 4bpp mode (16 colors per palette bank), charBase=1, screenBase=13
- **Tile map uses palette banks 0, 1, 3, 8** (various tiles use different palette banks)

#### Palette State at Frame 200

```
Palette[0-8]: All zeros (BLACK)
Palette[9]: 0x35ad (gray)
Palette[10]: 0x4e73 (gray)
Palette[11]: 0x7ff0 (cyan)
Palette[12]: 0x7fff (white)
Palette[13]: 0x3ff (yellow/cyan)
Palette[14]: 0x1f (red)
Palette[15]: 0x0 (black)
```

Colors are **offset by 9 indices** within palette bank 0!

#### Frame Output Analysis

```
8 unique colors in frame dump:
- RGB(0,0,0): 37,714 pixels (BLACK - most pixels!)
- RGB(248,0,0): 217 pixels (RED)
- RGB(248,248,248): 175 pixels (WHITE)
- RGB(128,248,248): 91 pixels (CYAN)
- RGB(248,248,0): 67 pixels (YELLOW)
- RGB(104,104,104): 64 pixels (GRAY)
- RGB(152,152,152): 64 pixels (LIGHT GRAY)
- RGB(224,224,224): 8 pixels (VERY LIGHT GRAY)
```

### Root Cause Hypothesis

In 4bpp mode with palette bank 0:

- Tiles reference color indices 0-15
- Palette bank 0 covers palette RAM at 0x05000000-0x0500001F
- BUT colors are written starting at index 9, not index 0

This means tiles using color indices 0-8 render as BLACK (zero), while only indices 9-14 have actual colors.

### Possible Causes

1. **Palette initialization bug**: Game expects BIOS or hardware to pre-fill palette indices 0-8, but HLE BIOS doesn't
2. **DMA timing issue**: Palette DMA may be writing to wrong offset or being partially skipped
3. **SWI decompression bug**: Palette may be LZ77/RLE compressed with a header that shifts the destination
4. **Classic NES Series quirk**: These games contain NES emulators; the inner emulator may have specific palette expectations

---

## Investigation Steps Required

### Step 1: Compare with LLE BIOS

Test if the game works correctly with real GBA BIOS:

```bash
# Add gba_bios.bin to project root and run
./build/bin/AIOServer --headless --headless-max-ms 5000 --rom OG-DK.gba --headless-dump-ppm /tmp/ogdk_lle.ppm
```

### Step 2: Trace Palette Writes During Boot

Add tracing to `GBAMemory::Write16()` for palette RAM writes (0x05000000-0x050003FF) during the first 100 frames to see:

- What addresses are being written
- What values are being written
- What triggers the writes (CPU, DMA, SWI)

### Step 3: Check SWI Decompression

If palette is compressed, verify HLE SWI 0x11/0x12/0x13 (LZ77/Huffman) decompression:

- Destination address calculation
- Header byte handling (size vs destination offset)

### Step 4: Compare Against mGBA

Run with mGBA to verify correct behavior:

```bash
/opt/homebrew/bin/mgba OG-DK.gba
# Capture frame at same point for comparison
```

---

## Files to Investigate

1. [src/emulator/gba/ARM7TDMI.cpp](src/emulator/gba/ARM7TDMI.cpp) - SWI handlers (LZ77, Huffman, RLE decompression)
2. [src/emulator/gba/GBAMemory.cpp](src/emulator/gba/GBAMemory.cpp) - Palette RAM writes, DMA to palette
3. [src/emulator/gba/PPU.cpp](src/emulator/gba/PPU.cpp) - 4bpp palette bank calculation (verified correct)

---

## Technical Details

### 4bpp Palette Bank Calculation (Verified Correct)

```cpp
// In PPU::RenderBackground()
if (!is8bpp) {
  paletteAddr += (paletteBank * 32) + (colorIndex * 2);
}
```

For palette bank 0, color index 1:

- paletteAddr = 0x05000000 + (0 _ 32) + (1 _ 2) = 0x05000002

This is correct per GBATEK.

### Expected vs Actual Palette Layout

**Expected (correct game):**

```
Palette[0]: backdrop color
Palette[1-15]: game colors for palette bank 0
```

**Actual (OG-DK):**

```
Palette[0-8]: BLACK (0x0000)
Palette[9-14]: game colors (offset by 9!)
Palette[15]: BLACK
```

---

## Next Steps

This plan documents the diagnosis. The fix requires:

1. **Confirming root cause** with LLE BIOS test
2. **Tracing palette initialization** to see why indices 0-8 are empty
3. **Possible SWI fix** if decompression is writing to wrong address

**Status:** Investigation needed. The PPU rendering logic is correct; the issue is in palette initialization/population.

---

## Handoff

This diagnosis is complete. Further work requires:

- Access to LLE BIOS for comparison testing
- Adding palette write tracing to identify the source of the offset problem
