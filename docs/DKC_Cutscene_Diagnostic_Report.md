# DKC Cutscene Diagnostic Report: Comprehensive Analysis

**Date:** January 15, 2026  
**Issue:** DKC cutscene sprites falling off-screen, delayed asset loading, missing textbox, incorrect animation playback

---

## Executive Summary

The symptoms observed in the DKC cutscene are **pure graphics rendering bugs** in an otherwise functional game system. Audio is confirmed working correctly, and the game is executing normally—the issues are localized to sprite rendering, asset loading timing, and layer composition. This diagnostic report focuses exclusively on graphics pipeline problems.

---

## Symptoms Summary

1. **Sprites Falling Due to Physics** (Primary Issue)

   - Donkey Kong and Diddy Kong sprites load, then immediately fall off-screen
   - Suggests sprites are being treated as dynamic/mobile objects with gravity applied
   - Position modified per frame in an unintended way

2. **Massive Asset Loading Delays** (5+ second delay for banana stack)

   - Sprites appear on next frame after multi-second wait
   - Suggests decompression/loading happening on-demand during rendering
   - Not pre-loaded at scene initialization

3. **Missing Textbox** (Likely Layering/Timing)

   - Text/UI not rendering or rendering behind other layers
   - Could be layer priority issue or timing issue with textbox initialization

4. **Animation Playing Incorrectly**

   - Sprites animating but not as expected
   - Suggests frame timing or animation data interpretation issue

5. **Color/Palette Differences** (Minor)
   - Noticeable lighting/palette change between screenshots 4 and 5
   - Could indicate BG color register timing or palette swapping

---

## Deep Dive: Root Cause Analysis

### 1. SPRITES FALLING OFF-SCREEN (Physics Applied)

**Current Implementation Status:**

- PPU sprite rendering in `src/emulator/gba/PPU.cpp` reads OAM (sprite attributes) per scanline
- No persistent physics simulation engine exists in the emulator
- Sprites are NOT updated by any velocity/acceleration system
- OAM data is read directly: `attr0` (Y position), `attr1` (X position), `attr2` (tile index, etc.)

**Possible Explanations (In Order of Likelihood):**

#### A. **OAM Corruption or Unintended Modification** ⭐ MOST LIKELY

- **What:** Game or DMA is writing incorrect Y values to OAM during scene init
- **Why:** Sprites appear initially, then fall → OAM Y coordinate incrementing each frame
- **Evidence:** 5 screenshots show progressive downward motion
- **Investigation Path:**
  - Monitor OAM writes at `0x07000000+` during scene load
  - Trace if any code is incrementing sprite Y positions
  - Check if DMA transfers are overwriting OAM incorrectly
- **Code Location:** `GBAMemory::Write8/16/32()` at address `0x07000000-0x070003FF`

#### B. **CPU Executing Unintended Code That Modifies OAM**

- **What:** Game's scene initialization code has a bug or we're executing wrong code
- **Why:** CPU stepping through incorrect ROM/RAM region
- **Evidence:** Sprites fall consistently, not randomly
- **Investigation Path:**
  - Log PC (program counter) during scene initialization
  - Dump OAM before/after key CPU instructions
  - Check if CPU PC is stuck in audio driver (DKC-specific issue)
- **Code Location:** `ARM7TDMI::Execute*()`, `GBA::Step()`

#### C. **DMA Channel Configured to Repeatedly Write to OAM** 🔴 WARNING

- **What:** DMA#0-3 is set to repeat mode and writing incrementing values to OAM
- **Why:** This would cause sprites to move progressively
- **Evidence:** Sprites fall at consistent rate frame-to-frame
- **Investigation Path:**
  - Log DMA channels 0-3 configuration (enable, src, dst, control)
  - Monitor if any DMA repeats during scene loop
  - Check DMA destination address overlap with OAM
- **Code Location:** `GBAMemory::PerformDMA()`, DMA registers `0x040000B0-0x040000DF`

#### D. **Memory Corruption: VRAM/OAM Buffer Overflow**

- **What:** Graphics data writes overflowing into OAM region
- **Why:** Unintended memory corruption causing sprite position changes
- **Evidence:** Delay before falling could be due to large decompression write
- **Investigation Path:**
  - Monitor all writes to `0x07000000+`
  - Track source of writes (CPU, DMA)
  - Check if sprite data being written to wrong VRAM location
- **Code Location:** Memory write interception in `GBAMemory`

---

### 2. ASSET LOADING DELAYS (5+ Seconds)

**Current Implementation Status:**

- Asset decompression (LZ77, Huffman, RLE) happens synchronously during SWI execution
- Graphics are written to VRAM during decompression
- PPU renders VRAM data each scanline (no pre-rendering cache)
- No streaming/async loading mechanism

**Possible Explanations:**

#### A. **Decompression Happening on-Demand During Scene** ⭐ MOST LIKELY

- **What:** Graphics data not decompressed until CPU calls decompression SWI mid-scene
- **Why:** 5-second delay between scene load and asset appearance
- **Evidence:** "One frame it's not there, next frame entire stack appears" (atomic decompression)
- **Mechanism:**
  1. Scene load initializes sprite OAM for bananas (but VRAM empty/uninitialized)
  2. Scene runs for ~5 seconds (CPU executing normal game logic/animations)
  3. CPU eventually calls SWI 0x12 (LZ77UnCompVram) to decompress banana graphics
  4. Decompression writes tile data to VRAM (`0x06000000+`)
  5. Next frame PPU renders newly-decompressed bananas from VRAM
- **Investigation Path:**
  - Trace SWI 0x12/0x13/0x14/0x15 calls with exact timestamps (relative to scene load)
  - Log decompression source/destination and size
  - Compare timing with scene events (when should bananas appear?)
  - Check if this is intentional delayed loading (streaming assets)
- **Code Location:** `ARM7TDMI::ExecuteSWI()` cases 0x11-0x15 (decompression kernels)

#### B. **Scene Has Intentional Stall/Wait Period** 🟡 POSSIBLE

- **What:** Game scene intentionally waits/stalls for N frames before loading next asset
- **Why:** Game design pattern—load assets in stages to manage memory/bandwidth
- **Evidence:** 5-second delay is consistent, not random
- **Investigation Path:**
  - Check if CPU executes tight loop (NOP, dummy instructions) during delay
  - Monitor if scene script has explicit "wait N frames" commands
  - Compare with reference hardware behavior in same scene
- **Code Location:** Game code in ROM (`0x8000000+`), CPU loop detection

#### C. **Emulator Decompression Performance Issue** 🟡 POSSIBLE

- **What:** Decompression (SWI 0x12/0x13/0x14/0x15) is accurate but slow in emulator
- **Why:** Pure software decompression implementation slower than real hardware
- **Evidence:** Asset appears after delay (emulator overhead, not game design)
- **Investigation Path:**
  - Profile SWI decompression execution time in emulator
  - Compare with expected decompression cycles on real GBA
  - Check if cycle counting in decompression is accurate
- **Code Location:** `ARM7TDMI::ExecuteSWI()` decompression implementations (~200 lines each)

#### D. **Game Intentionally Loads Assets During Cutscene Playback**

- **What:** DKC loads banana graphics while Donkey/Diddy animation plays, not upfront
- **Why:** Memory management—only allocate VRAM for assets when scene reaches that point
- **Evidence:** Assets appear exactly when needed (game design), not random delays
- **Investigation Path:**
  - Correlate 5-second delay with on-screen events (when do bananas actually need to appear?)
  - Check if decompression timing matches game choreography
- **Code Location:** Game code, scene script data

---

### 3. MISSING TEXTBOX (Layering/Timing)

**Current Implementation Status:**

- PPU renders layers in order: BG0-3 (modes), OBJ (sprites), then applies blending
- Layer priority from `priorityBuffer[]` determines draw order
- Window masking can disable layers at specific screen regions
- No explicit "UI layer" separate from OBJ

**Possible Explanations:**

#### A. **Textbox OBJ Has Incorrect Priority** ⭐ MOST LIKELY

- **What:** Textbox sprite priority set behind other objects
- **Why:** Drawn first, then overwritten by foreground sprites
- **Evidence:** Textbox data may be loaded but obscured
- **Investigation Path:**
  - Trace OBJ attributes (attr2 bits 10-11 = priority)
  - Compare textbox priority with Donkey/Diddy Kong sprites
  - Log PPU rendering order per scanline
- **Code Location:** `PPU::RenderOBJ()`, priority comparison at line ~1110

#### B. **Textbox OBJ Not Initialized in OAM**

- **What:** Textbox sprite attributes never written to OAM
- **Why:** Scene initialization code skipped or blocked
- **Evidence:** Textbox graphics may exist in VRAM but not rendered
- **Investigation Path:**
  - Dump OAM at scene load time
  - Check if textbox sprite slot (OAM index) has valid attributes
  - Trace if scene code reaches textbox initialization
- **Code Location:** OAM region `0x07000000-0x070003FF`

#### C. **Textbox Data in Disabled OBJ Window**

- **What:** Textbox sprite overlaps with OBJ window mask region
- **Why:** Window settings disable OBJ rendering in that area
- **Evidence:** Textbox graphics loaded but masked out
- **Investigation Path:**
  - Check WININ/WINOUT registers (window enable masks)
  - Trace window boundary calculations
  - Verify OBJ window flag for textbox sprite
- **Code Location:** `PPU::IsLayerEnabledAtPixel()`, window registers

#### D. **Textbox Layer Timing Issue**

- **What:** Textbox rendering happens on wrong scanline (before scene loads)
- **Why:** Frame timing miscalculation
- **Evidence:** Textbox would appear only if rendered at correct time
- **Investigation Path:**
  - Monitor VCOUNT (scanline counter) during textbox appearance
  - Check if rendering starts before textbox is loaded to VRAM
- **Code Location:** PPU scanline update loop

#### E. **Textbox Decompression Not Triggered** 🟡 POSSIBLE

- **What:** Textbox graphics compressed but decompression SWI not called
- **Why:** Scene script blocked or scene execution timing wrong
- **Evidence:** Combined with asset loading delays
- **Investigation Path:**
  - Trace when textbox decompression SWI is called
  - Check if CPU stuck before reaching textbox init code
- **Code Location:** Similar to asset loading delays

---

### 4. ANIMATION PLAYING INCORRECTLY

**Current Implementation Status:**

- No animation system in emulator—animation is game code responsibility
- Game reads frame counters, selects sprite tiles, updates OAM
- PPU renders OAM data as-is each frame

**Possible Explanations:**

#### A. **Frame Counter Not Incrementing or Incorrect**

- **What:** Game animation code relies on frame counter (VCOUNT or internal timer)
- **Why:** Emulator timing off, game running at wrong speed
- **Evidence:** Animation plays but at wrong speed or stutters
- **Investigation Path:**
  - Monitor VCOUNT behavior during scene
  - Check if VBlank interrupts firing at 60Hz
  - Compare CPU cycle count with expected 59.73 Hz
- **Code Location:** `PPU::Update()`, VBlank interrupt generation

#### B. **Animation Data Corruption**

- **What:** Animation frame table or sprite tile indices corrupted
- **Why:** Memory overflow or incorrect DMA transfer
- **Evidence:** Animation plays incorrectly, not simply paused
- **Investigation Path:**
  - Dump animation data region before/after load
  - Trace if DMA is writing to animation region
  - Check if sprite tile index writes are correct
- **Code Location:** Depends on DKC's animation data format

#### C. **Game Code Execution Normal, Animation Update Correct**

- **What:** Scene code and animation execution proceeding normally
- **Why:** Game updates OAM attr2 (tile indices) each frame as designed
- **Evidence:** Animation plays but position changes make it appear wrong
- **Investigation Path:**
  - Correlate sprite Y position changes with animation frame changes
  - Check if animation is correct but position makes it invisible/off-screen
  - Compare animation playback speed with expected frame rate
- **Code Location:** Game code, scene animation loops

#### D. **Sprite Tile Index (attr2) Not Being Updated**

- **What:** Game updates OAM attr2 but our PPU not reading updated value
- **Why:** Cache coherency issue or OAM read stale data
- **Evidence:** Animation frame changes not visible
- **Investigation Path:**
  - Log OAM reads in PPU vs actual OAM storage
  - Verify PPU reads current OAM each scanline (not cached)
  - Check if race condition between CPU write and PPU read
- **Code Location:** `PPU::RenderOBJ()` OAM read at line ~875

---

## Investigation Roadmap (Priority Order)

### Phase 1: Quick Wins (Enable Tracing)

```bash
# 1. Monitor CPU PC during scene load (detect audio driver stall)
AIO_TRACE_DKC_AUDIO=1 ./build/bin/AIOServer -r DKC.gba 2>&1 | tee dkc_trace.log

# 2. Monitor OAM writes and sprite positions
export AIO_TRACE_PPU_OAM=1
export AIO_TRACE_PPU_OAM_SPR=1
./build/bin/AIOServer -r DKC.gba

# 3. Monitor PPU rendering order and priorities
export AIO_TRACE_PPU_OBJPIX=1
export AIO_TRACE_PPU_OBJPIX_FRAME=2
./build/bin/AIOServer -r DKC.gba
```

### Phase 2: Hypothesis Testing

**Hypothesis 1: Decompression Timing is Scene-Driven (But Shouldn't Be)**

- Expected: SWI 0x12/0x13/0x14/0x15 called ~5 seconds after scene load
- Known fact: This is NOT intentional—bananas should decompress before scene initializes
- Action: Trace SWI calls with timestamps, identify what's blocking decompression at scene load
- Fix path: Ensure all scene assets are decompressed upfront, not lazily during playback

**Hypothesis 2: OAM Y Coordinates Modified by Game Code**

- Expected: OAM bytes 0-7 (attr0/attr1) incrementing each frame for Donkey/Diddy sprites
- Action: Dump OAM entry 0 and entry 1 each frame for first 30 frames
- Result: If Y coordinates match falling motion, determine if CPU writes or DMA writes

**Hypothesis 3: Textbox Priority Lower Than Foreground Sprites**

- Expected: Textbox OAM entry has attr2[10:11] higher priority number than Donkey/Diddy
- Action: Dump OAM textbox entry, check priority bits
- Result: If true, textbox rendered first then covered by foreground

### Phase 3: Root Cause Diagnosis

1. **If SWI 0x12/0x13/0x14/0x15 called at 5-second mark:**

   - Scene intentionally loading assets on-demand
   - Fix: Verify this matches game design (expected behavior)
   - Alternative: Profile decompression performance if timing seems too slow

2. **If OAM Y coordinates incrementing by game code:**

   - Game IS updating sprite positions as designed
   - Known fact: Sprites should NOT fall in this intro cutscene
   - Fix: Identify what's causing incorrect Y values (physics applied when it shouldn't be, or wrong sprite data loaded)
   - Root cause: Either game code bug, emulator OAM write bug, or DMA corruption

3. **If textbox priority too low:**

   - PPU priority logic working correctly, textbox just behind foreground
   - Fix: Verify textbox priority in game data, or check window masking

4. **If textbox never loaded to OAM:**
   - Scene initialization code not reaching textbox setup
   - Fix: Trace scene script execution to find where it stops

---

## Critical Code Locations for Investigation

| Component        | File                             | Lines                                | Issue                                        |
| ---------------- | -------------------------------- | ------------------------------------ | -------------------------------------------- |
| CPU Execution    | `src/emulator/gba/ARM7TDMI.cpp`  | Step(), ExecuteARM(), ExecuteThumb() | What is CPU doing during 5-second delay?     |
| OAM/Sprite Read  | `src/emulator/gba/PPU.cpp`       | RenderOBJ() ~line 875                | Reads OAM each scanline—sprite Y changing?   |
| OAM/Sprite Write | `src/emulator/gba/GBAMemory.cpp` | Write8/16/32() case 0x07             | CPU writes incrementing Y coordinates?       |
| Decompression    | `src/emulator/gba/ARM7TDMI.cpp`  | ExecuteSWI() cases 0x11-0x15         | When called? How long does it take?          |
| VRAM Access      | `src/emulator/gba/GBAMemory.cpp` | ReadVRAM(), WriteVRAM()              | Uninitialized VRAM causing visible glitches? |
| Priority         | `src/emulator/gba/PPU.cpp`       | RenderOBJ() ~line 1110               | OBJ priority vs BG priority comparison       |
| Window Masking   | `src/emulator/gba/PPU.cpp`       | IsLayerEnabledAtPixel()              | Window settings hiding textbox?              |
| DMA to OAM       | `src/emulator/gba/GBAMemory.cpp` | PerformDMA()                         | DMA writes to `0x07000000`?                  |
| DMA to VRAM      | `src/emulator/gba/GBAMemory.cpp` | PerformDMA()                         | When/how often DMA writes to VRAM?           |

---

## Recommended Next Steps

1. **Enable OAM tracing** and capture sprite Y coordinates each frame (first 200 frames)
2. **Enable SWI decompression tracing** with timestamps to identify when assets are loaded
3. **Dump OAM/Window registers** at scene load and at asset appearance moments
4. **Monitor CPU PC** to verify scene code is executing normally
5. **Profile decompression performance** to determine if delays are emulator overhead

---

## Conclusion

The DKC cutscene issues are **bugs with known incorrect behavior**:

1. **Sprites Falling (BUG)** → Sprites fall during intro cutscene when they shouldn't

   - Investigation: Determine if OAM Y values are wrong from start, or incrementing incorrectly
   - Root cause: Could be game data corruption, DMA bug, or emulator OAM handling
   - Fix: Correct whatever is writing wrong Y coordinates to OAM

2. **5-Second Asset Loading Delay (BUG)** → Bananas load at 5 seconds instead of scene init

   - Known fact: This is NOT intentional game design
   - Investigation: Identify why decompression SWI not called at scene load
   - Fix: Ensure scene initialization triggers asset decompression upfront

3. **Missing Textbox (BUG)** → Textbox not visible during cutscene

   - Investigation: Dump OAM to confirm textbox sprite exists, check priority and window settings
   - Fix: Correct priority, visibility mask, or OAM initialization as needed

4. **Animation Issues (SYMPTOM)** → Likely cascading effect from sprite falling
   - Once sprite falling is fixed, animation should normalize

**Action items:** Run diagnostics to pinpoint which component is writing/not writing the correct data, then fix that specific bug.
