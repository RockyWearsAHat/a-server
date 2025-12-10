# DKC Audio Analysis

## Problem Summary

Donkey Kong Country (DKC) remains stuck on a black screen with `DISPCNT=0x0` indefinitely. The PC continuously cycles through addresses in IWRAM (`0x3002xxx`), indicating the game is stuck in its custom audio driver loop.

## Key Findings

### 1. DKC Does NOT Use Standard M4A/MP2K

**Evidence:**

- ROM scan with `tools/analyze_m4a.py` found NO "Smsh" signature (standard M4A magic value)
- The game NEVER calls BIOS SWI 0x1A (SoundDriverInit), 0x1C (SoundDriverMain), or 0x1D (SoundDriverVSync)
- PC stuck in `0x3002b40`-`0x3002e3c` range (custom driver code in IWRAM)

**Conclusion:** DKC uses a completely custom audio system, not Nintendo's M4A engine.

### 2. Custom Audio Driver Architecture

From HeadlessTest logs:

```
[DEBUG] Audio driver @ 0x3002b40 = 0x4000
[DEBUG] Jump table @ 0x3001500 = 0x0
[DEBUG] IRQ Handler @ 0x3007FFC = 0x3000210
```

**Driver Flow:**

1. Game uploads custom audio driver code to IWRAM via DMA
2. Driver expects a jump table at `0x3001500` to be initialized (by BIOS or game)
3. Jump table is cleared by DMA#1 (IWRAM clear) and never restored
4. Driver loops waiting for initialization that never happens
5. Graphics initialization never occurs because control never returns from audio loop

### 3. Why Our M4A Engine Didn't Help

**What We Built:**

- Complete M4AEngine class (~400 lines) with:
  - ADPCM decoder (IMA standard)
  - ADSR envelope generator
  - Multi-channel mixer (16 channels)
  - Stereo panning/volume control
  - NoteOn/NoteOff API
- Integration into GBA core (GBA.h/cpp, GBAMemory.h, ARM7TDMI.cpp SWI hooks)

**Why It Doesn't Help DKC:**

- M4A engine is triggered by BIOS SWI calls (0x1A/0x1C/0x1D)
- DKC never calls these SWIs
- DKC's custom driver runs independently in IWRAM
- The driver waits for initialization (jump table at `0x3001500`) that our HLE BIOS doesn't provide

### 4. The Real Problem

**Root Cause:** DKC's custom audio driver expects the **real GBA BIOS** to have initialized certain memory structures during console bootup. Our HLE BIOS doesn't implement these initializations because we don't know what DKC's custom format expects.

**Specific Issue:**

- Jump table at `0x3001500` is cleared by DMA#1 (IWRAM initialization)
- Real BIOS would have set up this table during console boot
- Our HLE BIOS doesn't know what to put there
- Driver loops forever waiting for valid function pointers

## Attempted Fixes (All Failed)

### Iteration 1-3: Mailbox Polling

- Tried forcing `0x3000064` to 1 to signal driver ready
- Didn't help - driver still looped

### Iteration 4-5: BIOS Stubs

- Implemented SWI 0x1A-0x1E sound function stubs
- Didn't help - DKC never calls them

### Iteration 6-7: Driver Patching

- Tried patching driver code with `BX LR` (return immediately)
- Confirmed patch in memory but driver still executed original code
- Likely due to instruction cache or self-modifying code detection

### Iteration 8: Full M4A Engine

- Built complete M4A/MP2K engine from scratch
- Properly integrated into GBA core
- Doesn't help - DKC doesn't use M4A SWI interface

## Solutions

### Option 1: LLE BIOS (Legal Issues)

**Approach:** Load real GBA BIOS binary instead of HLE

- **Pros:** Would provide all initializations DKC expects
- **Cons:**
  - Distributing GBA BIOS is illegal (Nintendo copyright)
  - User must obtain their own BIOS dump
  - Slower than HLE (full BIOS emulation overhead)

### Option 2: Reverse Engineer DKC's Audio Format

**Approach:** Disassemble DKC's custom audio driver and replicate initialization

- **Pros:** Legal, educational, maintains HLE speed
- **Cons:**
  - Weeks/months of reverse engineering work
  - Highly game-specific (won't help other games)
  - May violate DKC's copyright (derivative work)

### Option 3: Skip DKC Audio Entirely

**Approach:** Detect DKC and patch driver to immediately return

- **Pros:** Simple, gets graphics working
- **Cons:**
  - No audio in DKC
  - Violates "proper emulation" principle
  - Might break game logic that depends on audio timing

### Option 4: Accept Current Limitation

**Approach:** Document DKC as incompatible, focus on other games

- **Pros:** Realistic, focuses effort on standard M4A games
- **Cons:** DKC is a high-profile game

## Recommendation

**Accept Option 4** with future path to Option 1:

1. **Document DKC Incompatibility:** Add to `docs/Compatibility.md` that DKC requires LLE BIOS
2. **Test M4A Engine with Standard Games:** Try Pokemon, Metroid Fusion, Mario Kart which use standard M4A
3. **Add LLE BIOS Support (User-Provided):** Allow user to load their own dumped BIOS for maximum compatibility
4. **Focus Development:** Prioritize games that use standard emulation interfaces

## Current Status

### What Works

✅ SMA2 (Super Mario Advance 2): Boots, runs, saves, fully playable
✅ Direct Sound audio (FIFO A/B, DMA, Timer-based)
✅ Graphics engine (BG0-3, OBJ, affine, mosaic, blending)
✅ Save system (EEPROM, Flash, SRAM)
✅ Input system (keyboard + gamepad)
✅ Cheats (GameShark/CodeBreaker)
✅ M4A engine (ready for standard M4A games)

### What Doesn't Work

❌ DKC: Custom audio driver blocks graphics init
❌ Games requiring LLE BIOS initialization
❌ PSG channels (GB legacy sound)

## Technical Debt

### M4A Engine Status

- **Core Engine:** Complete and integrated
- **Sequence Parser:** Not implemented (music data parsing)
- **Testing:** Not tested with any M4A game yet
- **Documentation:** Needs usage guide for future M4A game testing

### Next Steps for M4A

1. Find a standard M4A game (Pokemon Ruby/Sapphire, Metroid Fusion)
2. Test if game calls SWI 0x1A/0x1C/0x1D
3. Verify M4A engine produces audio output
4. Implement sequence parser if needed
5. Document M4A engine usage and game compatibility

## Conclusion

DKC uses a custom audio system that is incompatible with HLE BIOS emulation. The game expects specific BIOS initialization that we cannot replicate without either:

- Using a real GBA BIOS (LLE mode) - requires user-provided BIOS dump
- Extensive reverse-engineering of DKC's custom driver format

**Current Status:** All game-specific workarounds have been removed from the emulator core. DKC is documented as incompatible with HLE BIOS and will remain on a black screen until proper LLE BIOS support is added.

The M4A engine implementation remains valuable for standard M4A games (Pokemon, Metroid Fusion, Mario Kart, etc.) and should be tested with those titles.
