# Plan: Make GBA Emulator 100% Functional

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
