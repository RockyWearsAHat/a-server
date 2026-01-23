# Plan: Fix Critical Game Issues (SMA2 Lag, DKC Fade, OG-DK Corruption)

**Date:** 2026-01-22  
**Goal:** Fix three critical emulation issues: SMA2 performance lag, DKC intro logo fade not working, and OG-DK corruption/display issues.

---

## Context

### Issues to Fix

| Issue                     | Game  | Symptom                            | Likely Root Cause                                                                                 |
| ------------------------- | ----- | ---------------------------------- | ------------------------------------------------------------------------------------------------- |
| **Massive Lag**           | SMA2  | Game runs extremely slow           | Peripheral batching too aggressive, or cycle counting overhead                                    |
| **Logo Fade Not Working** | DKC   | Logos don't fade in intro cutscene | PPU color effects (brightness increase/decrease) bug                                              |
| **Corrupted Mess**        | OG-DK | Display completely broken          | ROM is likely a NES emulator (PocketNES) running on GBA; requires accurate timing/memory handling |

### Technical Analysis

#### 1. SMA2 Lag Issue

**Hypothesis:** The peripheral batching system (`PERIPHERAL_BATCH_CYCLES = 64`) accumulates cycles before updating PPU/APU/Timers. This can:

- Delay VBlank IRQ delivery, causing games to miss frames
- Create timing drift between CPU execution and peripheral state
- Cause the game loop to spin waiting for VBlank when it should have already fired

**Evidence:**

- `GBA::Step()` batches peripheral updates: cycles accumulate in `pendingPeripheralCycles`
- Only flushes when ≥64 cycles or CPU halted
- If VBlank IRQ is delayed by batching, game's main loop spins longer than needed

**Potential Fixes:**

1. Reduce `PERIPHERAL_BATCH_CYCLES` from 64 to 16 or lower
2. Flush peripheral cycles before returning from any instruction that reads DISPSTAT/VCOUNT
3. Ensure VBlank/HBlank IRQs fire at exact cycle boundaries

#### 2. DKC Logo Fade Issue

**Hypothesis:** The PPU's `ApplyColorEffects()` function implements brightness increase (fade to white, mode 2) and brightness decrease (fade to black, mode 3), but something is wrong with:

- Target layer selection (BLDCNT bits 0-5 for first target)
- Effect coefficient (BLDY bits 0-4)
- The layer detection (`layerBuffer[]` tracking)

**Evidence from PPU.cpp:**

```cpp
} else if (effectMode == 2) {
  if (!topIsFirstTarget) { continue; }  // Brightness Increase
  r = from5(to5(r) + ((31 - to5(r)) * evy / 16));
  ...
} else if (effectMode == 3) {
  if (!topIsFirstTarget) { continue; }  // Brightness Decrease
  r = from5(to5(r) - (to5(r) * evy / 16));
  ...
}
```

**Potential Fixes:**

1. Verify `layerBuffer[]` correctly tracks which layer each pixel came from
2. Check if backdrop (color 0) is being treated as a first target
3. Verify BLDCNT register reads are returning correct values during intro
4. Add tracing to see what BLDCNT/BLDY values DKC sets during the fade

#### 3. OG-DK Corruption Issue

**Hypothesis:** OG-DK is likely "Classic NES Series: Donkey Kong" or a PocketNES-style ROM that runs an NES emulator on GBA hardware. These ROMs:

- Are extremely timing-sensitive (NES emulator on GBA needs precise timing)
- Often use non-standard memory layouts
- May use undocumented hardware behavior

**Potential Fixes:**

1. First, identify what OG-DK actually is (check ROM header)
2. If it's an NES-on-GBA emulator, fix timing accuracy issues
3. Check for memory region access patterns that differ from normal games
4. May require BIOS timing improvements

---

## Steps

### Phase 1: Diagnose & Fix SMA2 Lag

1. [ ] **Add Performance Tracing**
   - Add timing measurement to GBA::Step() to identify where time is spent
   - Log how often peripheral batching flushes vs how often it accumulates
   - Command: `AIO_TRACE_PERF=1 ./build/bin/AIOServer SMA2.gba`

   Patch into [GBA.cpp](src/emulator/gba/GBA.cpp):

   ```cpp
   // At top of Step():
   static uint64_t stepCount = 0;
   static uint64_t totalCpuTime = 0;
   static uint64_t totalPpuTime = 0;
   auto stepStart = std::chrono::high_resolution_clock::now();

   // ... existing code ...

   // After peripheral update:
   auto stepEnd = std::chrono::high_resolution_clock::now();
   if (EnvFlagCached("AIO_TRACE_PERF") && ++stepCount % 1000000 == 0) {
     std::cout << "[PERF] Steps=" << stepCount
               << " pendingCycles=" << pendingPeripheralCycles << std::endl;
   }
   ```

2. [ ] **Reduce Peripheral Batch Size**
   - Change `PERIPHERAL_BATCH_CYCLES` from 64 to 8 in [GBA.h](include/emulator/gba/GBA.h)
   - This trades some performance for better timing accuracy

   Patch into [GBA.h](include/emulator/gba/GBA.h#L88):

   ```cpp
   // OLD: static constexpr int PERIPHERAL_BATCH_CYCLES = 64;
   static constexpr int PERIPHERAL_BATCH_CYCLES = 8;
   ```

3. [ ] **Flush Cycles on DISPSTAT/VCOUNT Reads**
   - When game reads DISPSTAT (0x04000004) or VCOUNT (0x04000006), flush pending cycles first
   - This ensures the game sees accurate VBlank/HBlank state

   Patch into [GBAMemory.cpp](src/emulator/gba/GBAMemory.cpp) in `Read16()` for IO region:

   ```cpp
   case 0x04: // DISPSTAT
   case 0x06: // VCOUNT
     // Flush pending peripheral cycles before reading timing-sensitive registers
     if (auto* gba = GetGBA()) {
       gba->FlushPendingPeripheralCycles();
     }
     break;
   ```

4. [ ] **Verify Frame Timing**
   - Ensure 280,896 cycles per frame (1232 cycles × 228 scanlines)
   - Log actual cycles between VBlank IRQs
   - If frames are taking too long, the game appears slow

5. [ ] **Test SMA2 After Fixes**
   - Run SMA2 and verify lag is gone
   - Ensure no regression in other games
   - Check frame rate is stable 60fps

### Phase 2: Diagnose & Fix DKC Fade Issue

6. [ ] **Add BLDCNT/BLDY Tracing**
   - Log every write to blend registers during DKC intro
   - Command: `AIO_TRACE_PPU_IO_WRITES=1 ./build/bin/AIOServer DKC.gba`

   The existing tracing in GBAMemory.cpp should capture this. Check output for:
   - BLDCNT (0x04000050) writes - should see mode 2 or 3 being set
   - BLDY (0x04000054) writes - should see EVY coefficient changing over time

7. [ ] **Fix Layer Buffer Tracking**
   - In `DrawScanline()`, ensure `layerBuffer[]` correctly identifies each pixel's source layer
   - Backdrop pixels (when no BG/OBJ covers them) should be layer 5

   Verify in [PPU.cpp](src/emulator/gba/PPU.cpp) `DrawScanline()`:

   ```cpp
   // After rendering all layers, backdrop pixels should have layerBuffer[x] = 5
   // Check if this is being set correctly
   ```

8. [ ] **Verify First Target Selection**
   - BLDCNT bits 0-5 select first target layers (BG0-3, OBJ, Backdrop)
   - During fade, bit 5 (backdrop) should be set if fading the background
   - Bit 0-3 (BG0-3) should be set for background layers

   Add diagnostic in [PPU.cpp](src/emulator/gba/PPU.cpp) `ApplyColorEffects()`:

   ```cpp
   static int fadeLogCount = 0;
   if (effectMode == 2 || effectMode == 3) {
     if (fadeLogCount < 100) {
       fadeLogCount++;
       std::cout << "[FADE] mode=" << effectMode
                 << " evy=" << evy
                 << " firstTarget=0x" << std::hex << (int)firstTarget
                 << " scanline=" << std::dec << scanline << std::endl;
     }
   }
   ```

9. [ ] **Fix Backdrop Handling in Color Effects**
   - If DKC fades the backdrop and all BGs together, ensure backdrop (layer 5) is included
   - The `layerBuffer[]` may not be setting layer 5 for backdrop pixels

   Patch into [PPU.cpp](src/emulator/gba/PPU.cpp) in `ApplyColorEffects()`:

   ```cpp
   // After getting topLayer:
   const uint8_t topLayer = layerBuffer[pixelIndex] <= 5 ? layerBuffer[pixelIndex] : 5;

   // If pixel is backdrop (no layer rendered), set layer to 5
   // Verify backdrop pixels have layerBuffer[] = 5, not some other value
   ```

10. [ ] **Test DKC Fade After Fixes**
    - Run DKC and watch intro cutscene
    - Logos should fade in/out smoothly
    - Verify no regression in other games' color effects

### Phase 3: Diagnose & Fix OG-DK Corruption

11. [ ] **Identify OG-DK ROM Type**
    - Check ROM header to identify what OG-DK actually is
    - Command: `xxd OG-DK.gba | head -20`
    - Look for "NINTENDO" header and game code

    If it's "Classic NES Series: Donkey Kong":
    - Game code should be "FDKE" (USA) or similar
    - Uses Nintendo's NES emulator core on GBA

12. [ ] **Enable Detailed Memory Tracing**
    - OG-DK corruption likely caused by memory access issues
    - Command: `AIO_TRACE_GBA_SPAM=1 ./build/bin/AIOServer OG-DK.gba 2>&1 | head -1000`
    - Look for:
      - Invalid memory accesses (0xFFFFFFFF patterns)
      - Unexpected PC values
      - Stack underflow/overflow

13. [ ] **Check for Timing-Sensitive Code**
    - Classic NES Series games are notorious for tight timing requirements
    - The NES emulator running on GBA expects precise VBlank timing
    - Our HLE BIOS may not provide accurate enough timing

    Test with LLE BIOS:

    ```bash
    AIO_GBA_BIOS=/path/to/gba_bios.bin ./build/bin/AIOServer OG-DK.gba
    ```

14. [ ] **Fix Common NES-on-GBA Issues**
    - These games often use:
      - Precise HBlank timing for scanline effects
      - Specific DMA patterns
      - Timer-based audio

    Potential fixes if LLE BIOS helps:
    - Improve HLE BIOS timing accuracy
    - Add cycle-accurate HBlank callback

15. [ ] **Test OG-DK After Fixes**
    - If LLE BIOS works, document requirement
    - If still broken, may need deeper investigation into NES emulator requirements

---

## Files Affected

### Primary Files to Modify

| File                                            | Changes                                                      |
| ----------------------------------------------- | ------------------------------------------------------------ |
| [GBA.h](include/emulator/gba/GBA.h)             | Reduce PERIPHERAL_BATCH_CYCLES                               |
| [GBA.cpp](src/emulator/gba/GBA.cpp)             | Add performance tracing, expose FlushPendingPeripheralCycles |
| [GBAMemory.cpp](src/emulator/gba/GBAMemory.cpp) | Flush cycles on DISPSTAT/VCOUNT reads                        |
| [PPU.cpp](src/emulator/gba/PPU.cpp)             | Fix layer buffer tracking, add fade diagnostics              |

### Documentation to Update

| File                                        | Updates                               |
| ------------------------------------------- | ------------------------------------- |
| [memory.md](.github/instructions/memory.md) | Document peripheral batching behavior |
| [Compatibility.md](docs/Compatibility.md)   | Update OG-DK requirements             |

---

## Test Strategy

1. **SMA2 Lag Test**
   - Run SMA2 for 60 seconds
   - Count frames rendered (should be ~3600)
   - Measure wall-clock time (should be ~60 seconds)
   - Pass criteria: ≤5% timing deviation

2. **DKC Fade Test**
   - Boot DKC and watch intro
   - Verify Rare/Nintendo logos fade in and out
   - Capture screenshots at fade midpoint
   - Pass criteria: Visible gradient, not instant on/off

3. **OG-DK Test**
   - Boot OG-DK with LLE BIOS
   - Verify title screen displays correctly
   - Play through first level
   - Pass criteria: Playable without visual corruption

4. **Regression Tests**
   - Run existing test suite: `make test`
   - Verify SMA2, DKC (with LLE BIOS), and other games still work
   - No performance regression (maintain 60fps)

---

## Verification Criteria

- [ ] SMA2 runs at full speed (60fps, no perceived lag)
- [ ] DKC logos fade correctly in intro cutscene
- [ ] OG-DK displays correctly (or documented as requiring LLE BIOS)
- [ ] All existing tests pass
- [ ] No regression in other games

---

## Risks / Unknowns

1. **OG-DK ROM Unknown** — May be a homebrew, modified ROM, or specific regional variant
2. **Performance Tradeoff** — Reducing batch size improves accuracy but may impact performance
3. **DKC Custom Audio** — Even with fade fixed, DKC may still need LLE BIOS for audio driver
4. **Layer Tracking Complexity** — PPU layer buffer may need significant refactoring

---

## Memory.md Updates

After implementing these fixes, update `.github/instructions/memory.md` with:

```markdown
## Peripheral Batching

The GBA core batches peripheral updates for performance:

- `PERIPHERAL_BATCH_CYCLES` controls batch size (default: 8 cycles)
- Smaller values = more accurate timing, slightly lower performance
- Cycles are flushed early when reading DISPSTAT/VCOUNT for timing accuracy

## PPU Color Effects

The PPU supports four blending modes:

- Mode 0: None
- Mode 1: Alpha blend (two layers)
- Mode 2: Brightness increase (fade to white)
- Mode 3: Brightness decrease (fade to black)

Layer tracking via `layerBuffer[]`:

- 0-3: BG0-BG3
- 4: OBJ (sprites)
- 5: Backdrop (no layer)
```

---

**Ready for implementation!** Hand off to `@Implement` agent.

---

# Previous Plan: Make GBA Emulator 100% Functional

**Date:** 2026-01-22  
**Goal:** Transform the AIO GBA emulator from ~90% to 100% game compatibility by implementing missing features, fixing known issues, and ensuring all major game categories work correctly.

---

## Context

### Current State

The GBA emulator is **highly functional** with the following working:

| Component          | Status       | Notes                                                  |
| ------------------ | ------------ | ------------------------------------------------------ |
| CPU (ARM7TDMI)     | ✅ Complete  | ARM/Thumb instruction sets, mode switching, interrupts |
| Memory Map         | ✅ Complete  | All regions, wait states, mirroring                    |
| PPU                | ✅ Complete  | BG0-3, sprites, affine, mosaic, blending, windowing    |
| APU (Direct Sound) | ✅ Complete  | FIFO A/B, timer-based sample rate, stereo mixing       |
| APU (PSG)          | ❌ Missing   | Square/wave/noise channels not implemented             |
| DMA                | ✅ Complete  | All channels, timing, special modes                    |
| Timers             | ✅ Complete  | All 4 timers, cascade, IRQ                             |
| Interrupts         | ✅ Complete  | VBlank, HBlank, Timer, DMA IRQs                        |
| BIOS (HLE)         | ✅ ~95%      | Most SWIs implemented, some edge cases missing         |
| BIOS (LLE)         | ✅ Available | User-provided BIOS supported via AIO_GBA_BIOS env var  |
| Save Types         | ✅ Complete  | SRAM, Flash (512K/1M), EEPROM (4K/64K)                 |
| M4A Engine         | ⚠️ Partial   | ADPCM decoder done, sequence parser TODO               |
| Input              | ✅ Complete  | Keyboard + controller                                  |
| Cheats             | ✅ Complete  | GameShark/CodeBreaker                                  |

### Games Tested

| Game                  | Status            | Notes                                    |
| --------------------- | ----------------- | ---------------------------------------- |
| Super Mario Advance 2 | ✅ Fully Playable | Graphics, audio, saves all work          |
| Donkey Kong Country   | ❌ HLE / ✅ LLE   | Requires real BIOS (custom audio driver) |
| Pokemon/Metroid/Zelda | ⏳ Untested       | Should work (standard M4A)               |

### Known Gaps

1. **PSG Audio Channels** - Game Boy legacy sound (square, wave, noise) not implemented
2. **M4A Sequence Parser** - Music playback from sequence data not implemented
3. **Link Cable** - Multiplayer/trading not supported
4. **Special Hardware** - Solar sensor, motion sensor, rumble not implemented
5. **Some BIOS Edge Cases** - A few SWIs may have subtle inaccuracies

---

## Steps

### Phase 1: PSG Audio (High Impact)

Many games mix PSG with Direct Sound for sound effects.

1. [ ] **Implement PSG Square Wave Channels (1 & 2)**
   - Add `PSGChannel` struct to APU with frequency, duty cycle, envelope
   - Implement sweep (channel 1 only)
   - Generate square wave samples at the correct frequency
   - Files: [APU.h](include/emulator/gba/APU.h), [APU.cpp](src/emulator/gba/APU.cpp)

2. [ ] **Implement PSG Wave Channel (3)**
   - Add Wave RAM buffer (32 samples, 4-bit each)
   - Generate samples from Wave RAM pattern
   - Support volume control (100%, 50%, 25%, mute)
   - Files: [APU.h](include/emulator/gba/APU.h), [APU.cpp](src/emulator/gba/APU.cpp)

3. [ ] **Implement PSG Noise Channel (4)**
   - Add LFSR (Linear Feedback Shift Register) noise generator
   - Support 7-bit and 15-bit modes
   - Implement envelope and frequency dividers
   - Files: [APU.h](include/emulator/gba/APU.h), [APU.cpp](src/emulator/gba/APU.cpp)

4. [ ] **Mix PSG with Direct Sound**
   - Combine PSG output with FIFO samples in OnTimerOverflow
   - Respect SOUNDCNT_L volume/enable bits for PSG
   - Files: [APU.cpp](src/emulator/gba/APU.cpp)

5. [ ] **Add PSG I/O Register Handling**
   - Handle writes to 0x04000060-0x0400007F (sound channel registers)
   - Handle reads for channel status
   - Files: [GBAMemory.cpp](src/emulator/gba/GBAMemory.cpp)

6. [ ] **Write PSG Tests**
   - Test square wave generation at various frequencies
   - Test envelope decay
   - Test noise LFSR correctness
   - Files: `tests/APUTests.cpp` (new file)

### Phase 2: M4A Sequence Parser (Medium Impact)

Enables full music playback in Nintendo SDK games.

7. [ ] **Parse M4A Sound Bank**
   - Locate "Smsh" magic in ROM
   - Parse instrument table (sample pointers, ADSR parameters)
   - Files: [M4AEngine.cpp](src/emulator/gba/M4AEngine.cpp)

8. [ ] **Parse M4A Sequence Data**
   - Implement sequence command decoder (note on/off, tempo, loops)
   - Track position in sequence for each active song
   - Files: [M4AEngine.cpp](src/emulator/gba/M4AEngine.cpp)

9. [ ] **Implement M4A ProcessFrame**
   - Called via SWI 0x1C or direct hook
   - Advance sequence position, trigger notes
   - Mix active channels into Direct Sound FIFO
   - Files: [M4AEngine.cpp](src/emulator/gba/M4AEngine.cpp), [ARM7TDMI.cpp](src/emulator/gba/ARM7TDMI.cpp)

10. [ ] **Test M4A with Real Games**
    - Test with Pokemon Ruby/Sapphire (known M4A user)
    - Test with Metroid Fusion
    - Verify music plays correctly

### Phase 3: BIOS Accuracy (Low-Medium Impact)

Fix edge cases that affect specific games.

11. [ ] **Audit All SWI Implementations**
    - Compare against GBATEK documentation
    - Fix any parameter handling issues
    - Files: [ARM7TDMI.cpp](src/emulator/gba/ARM7TDMI.cpp) (ExecuteSWI function)

12. [ ] **Implement Missing SWIs**
    - SWI 0x06: Halt (if not fully accurate)
    - SWI 0x1F: MidiKey2Freq (verify implementation)
    - Any others referenced by games that fail

13. [ ] **Improve HLE BIOS Timing**
    - Ensure AdvanceHLECycles is called accurately
    - Some BIOS functions should consume specific cycle counts
    - Files: [ARM7TDMI.cpp](src/emulator/gba/ARM7TDMI.cpp)

### Phase 4: Game Compatibility Testing (Critical)

**ROM Library Location:** `~/Desktop/ROMs/GBA`

14. [ ] **Test All ROMs in ~/Desktop/ROMs/GBA**
    - Run each ROM through headless test (10s minimum)
    - Check for: boot success, graphics, audio, input response
    - Log any crashes, hangs, or graphical glitches
    - Command: `./build/bin/HeadlessTest ~/Desktop/ROMs/GBA/<rom>.gba`

15. [ ] **Create Compatibility Matrix**
    - For each ROM, document:
      - Boot status (boots/black screen/crash)
      - Graphics (perfect/minor glitches/broken)
      - Audio (working/partial/silent)
      - Saves (working/broken/untested)
      - Playability (fully playable/playable with issues/unplayable)
    - Update [Compatibility.md](docs/Compatibility.md) with results

16. [ ] **Categorize Issues Found**
    - Group by root cause (PSG missing, M4A issue, BIOS issue, timing, etc.)
    - Prioritize fixes by number of games affected
    - Track which features are blocking specific games

17. [ ] **Fix Game-Specific Issues (Hardware Bugs Only)**
    - Only fix issues that represent real hardware behavior
    - No game-specific hacks per project principles
    - Document any hardware quirks discovered

### Phase 5: Polish & Documentation

17. [ ] **Update Documentation**
    - Update [Audio_System.md](docs/Audio_System.md) with PSG details
    - Update [M4A_Engine.md](docs/M4A_Engine.md) with sequence parser details
    - Update [100_Percent_Compatibility.md](docs/100_Percent_Compatibility.md)

18. [ ] **Add Automated Compatibility Tests**
    - Create test ROMs that exercise specific features
    - Add to test suite for regression prevention
    - Files: `tests/` directory

19. [ ] **Performance Optimization**
    - Profile emulator with demanding games
    - Optimize hot paths if needed
    - Ensure 60fps on target hardware

---

## Files Affected

### New Files

- `tests/APUTests.cpp` — PSG audio unit tests

### Modified Files

- [APU.h](include/emulator/gba/APU.h) — Add PSG channel structures
- [APU.cpp](src/emulator/gba/APU.cpp) — Implement PSG synthesis and mixing
- [GBAMemory.cpp](src/emulator/gba/GBAMemory.cpp) — Handle PSG I/O registers
- [M4AEngine.cpp](src/emulator/gba/M4AEngine.cpp) — Sequence parser
- [ARM7TDMI.cpp](src/emulator/gba/ARM7TDMI.cpp) — BIOS accuracy fixes
- [Compatibility.md](docs/Compatibility.md) — Test results
- [Audio_System.md](docs/Audio_System.md) — PSG documentation

---

## Test Strategy

1. **Unit Tests**
   - PSG waveform generation accuracy
   - LFSR noise sequence correctness
   - M4A command parsing

2. **ROM Library Testing**
   - Test all ROMs in `~/Desktop/ROMs/GBA`
   - Use headless mode: `./build/bin/HeadlessTest <rom>`
   - Batch test script: `for rom in ~/Desktop/ROMs/GBA/*.gba; do ./build/bin/HeadlessTest "$rom" 2>&1 | tee -a test_results.log; done`

3. **Manual Testing**
   - Play through first 10 minutes of each game
   - Verify audio, graphics, saves all work
   - Document any issues

4. **Regression Prevention**
   - All existing tests must pass
   - SMA2 must remain fully playable
   - No performance regressions

---

## Verification Criteria

The emulator is "100% functional" when:

- [ ] PSG audio works (square, wave, noise channels)
- [ ] M4A music plays in standard SDK games
- [ ] Top 20 popular games boot and are playable
- [ ] No game-breaking bugs in tested games
- [ ] Save/load works correctly for all save types
- [ ] 60fps maintained on reasonable hardware
- [ ] All unit tests pass
- [ ] Documentation is complete and accurate

---

## Risks / Unknowns

1. **M4A Sequence Format Complexity** — The format is documented but complex; may take significant effort to implement fully
2. **PSG Timing Accuracy** — PSG channels have precise timing requirements; may need cycle-accurate implementation
3. **Undocumented Hardware Behavior** — Some games may rely on undocumented GBA quirks
4. **Testing Coverage** — May not have access to all top 20 games for testing

---

## Open Questions for User

1. ~~Do you have access to the top 20 GBA games for testing?~~ ✅ ROMs available at `~/Desktop/ROMs/GBA`
2. Do you have a real GBA BIOS dump for LLE testing?
3. Are there specific games you want prioritized from your ROM collection?
4. Is there a performance target (e.g., must run on Raspberry Pi)?
5. Should Link Cable support be on the roadmap (for Pokemon trading, etc.)?

---

## Priority Order

Recommended implementation order for maximum impact:

1. **Game Testing** (Phase 4) — Test all ROMs in `~/Desktop/ROMs/GBA` first to identify real blockers
2. **PSG Audio** (Phase 1) — Many games use PSG for sound effects
3. **BIOS Fixes** (Phase 3) — Fix any issues found during testing
4. **M4A Sequence** (Phase 2) — Only if testing shows music is broken in M4A games
5. **Polish** (Phase 5) — Documentation and test suite

---

**Ready for implementation!** Hand off to `@Implement` agent to execute this plan step-by-step.

## When to use

- A plan exists (in plan.md or conversation)
- Task is well-defined: specific feature, bug fix, or refactor
- Scope is implementation, not research

## Workflow

1. Read the plan and relevant docs
2. Update/create documentation for the change
3. Write tests that encode the expected behavior
4. Implement the smallest correct change
5. Run tests (narrow → broad)
6. Mark todos complete as work progresses

## Tools

- File editing (create/replace)
- Terminal (build, test)
- Test runner
- Todo list management

## Boundaries

- Does NOT make architectural decisions
- Does NOT skip documentation or tests
- Defers unclear scope to Plan agent or user

````

### Plan Agent

```markdown
# Plan Agent

Research and outline multi-step plans before implementation.

## When to use

- Complex feature requiring multiple steps
- Debugging strategy needed
- Architecture decision required
- Unclear scope or approach

## Workflow

1. Gather context (semantic search, file reads, web fetch)
2. Read existing docs and specs
3. Identify affected subsystems and files
4. Propose a plan in plan.md or conversation
5. List risks, unknowns, and open questions

## Tools

- Semantic search
- File reading
- Web fetch
- Grep search

## Boundaries

- Does NOT implement code
- Does NOT run tests or builds
- Hands off to Implement agent or user
````

---

## Notes

- The separation of Plan vs Implement helps prevent agents from making large changes without user review.
- The `memory.md` file should be updated alongside code changes to stay useful.
- Consider adding a Debug agent later if complex debugging sessions become common.
- ✅ `tests/APUTests.cpp` added and `APUTests` target registered; tests pass locally (2026-01-22).
