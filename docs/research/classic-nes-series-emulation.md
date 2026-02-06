# Classic NES Series GBA Emulation Requirements

## Overview

The Classic NES Series (also known as Famicom Mini in Japan) are GBA games that contain an NES emulator running NES ROMs. These games are notoriously difficult to emulate correctly because they employ multiple anti-emulation techniques and rely on precise GBA hardware behavior.

This document summarizes the technical requirements for correctly emulating these games, based on mGBA developer Vicki Pfau's detailed analysis and other emulator development resources.

---

## Known Anti-Emulation Techniques

The Classic NES Series games use six specific tricks to detect and break emulators:

### Trick #1: Memory Mirroring

**What it does:** The games copy code into main RAM and then jump to a mirrored address.

**Technical details:**

- GBA main RAM is 256 KB (18 bits of address space)
- Addresses 0x02000000 to 0x0203FFFF are valid
- Addresses 0x02040000 to 0x02FFFFFF are "mirrored" - the upper bits are ignored
- The game jumps to these mirror addresses to confuse emulators

**Emulator requirement:** Correctly implement memory mirroring by masking upper unused bits in memory regions.

### Trick #2: Code Execution in VRAM

**What it does:** The games copy executable code into Video RAM (VRAM) and execute it there.

**Technical details:**

- VRAM is normally used only for graphics data
- The games treat VRAM as executable memory
- This is a deliberate anti-emulation technique

**Emulator requirement:** Allow code execution from VRAM. Don't assume VRAM is graphics-only.

**mGBA commit:** `501b6b621c6c27cbb5523f53172e00c426a8b445`

### Trick #3: STM to DMA Registers

**What it does:** Uses the ARM `STMDA` (Store Multiple Decrement After) instruction to set up DMA transfers in a single instruction.

**Technical details:**

- ARM7TDMI writes registers in a specific order during STM instructions
- Despite "decrement after", values are still written in ascending register order
- The processor pre-calculates the final address and writes from there
- DMA has three consecutive registers: source, destination, and control
- Writing the control register before addresses would start an incorrect DMA

**Emulator requirement:** Correctly implement STM instruction memory write ordering. For STMDA, compute the final address first, then write values in ascending register order from that base.

### Trick #4: Save Type Masquerading

**What it does:** Games fake accessing the wrong save type (SRAM) when they actually use EEPROM.

**Technical details:**

- Classic NES Series games all use EEPROM for saves
- Games first attempt SRAM writes to detect emulators
- If SRAM writes succeed, game shows "Game Pak Error" screen
- Emulators that auto-detect save type can be fooled

**Emulator requirement:** Force EEPROM save type for Classic NES Series games. Detect by game code and override auto-detection.

**mGBA commit:** `c52edab71a0f3465c508b554130fdccb4108a654`

### Trick #5: Prefetch/Pipeline Abuse (CRITICAL)

**What it does:** Exploits the ARM7TDMI 3-stage pipeline to detect emulator pipeline depth.

**Technical details:**

- ARM7TDMI has 3 stages: Fetch → Decode → Execute
- When an instruction is fetched, modifying that memory location is irrelevant
- The game modifies an instruction 2 positions ahead while executing
- If pipeline is correct depth (3 stages), the OLD instruction is already fetched
- If pipeline is too short (2 stages), the NEW modified instruction is fetched

**Example code from Classic NES Metroid:**

```arm
06000260:  E3A01000     mov r1, #0
06000264:  E28FE008     add lr, pc, #8
06000268:  E51F0010     ldr r0, [$06000260]
0600026C:  E58E0000     str r0, [lr, #0]      ; Modifies 06000274
06000270:  E3A010FF     mov r1, #255
06000274:  E3A010FF     mov r1, #255          ; This gets overwritten
```

- Correct pipeline: r1 = 255 (old instruction was already fetched)
- Wrong pipeline: r1 = 0 (modification hit the fetched instruction)
- Game checks r1 and fails boot if value is wrong

**Emulator requirement:** Implement a proper 3-stage pipeline with fetch, decode, and execute stages. The decode stage must exist as a separate stage, not merged with execute.

**mGBA commit:** `28ac288d2cc753aab0493a471726cc5795a09363`

### Trick #6: Audio FIFO Irregularities

**What it does:** Uses 16-bit writes to PCM audio FIFO instead of the expected 32-bit writes.

**Technical details:**

- GBA PCM channels expect 32-bit writes (4 samples at a time)
- Classic NES Series writes only 16 bits (2 samples) at a time
- Naive implementations insert garbage for the other 16 bits
- Results in completely garbled audio

**Emulator requirement:** Handle 16-bit writes to audio FIFO registers correctly, only adding the 2 samples written, not inserting garbage samples.

**mGBA commit:** `c52a5d2859a535cd439f914beffeb1f2eb49e9f6`

---

## Graphics-Specific Requirements

### Sprite Cycle Counting

Classic NES Series games (especially Famicom Mini versions) are sensitive to sprite cycle counting per scanline.

**Issue:** Corrupted graphics with missing bottom row(s) of background tiles.

**Technical details:**

- GBA has a limit on sprite pixels per scanline (~960 pixels)
- This must be computed per-scanline, not per-frame
- The OpenGL renderer in mGBA initially did this wrong

**Emulator requirement:** Implement per-scanline sprite cycle counting.

**mGBA fix:** "Helps if I actually do this per scanline, which I wasn't." - endrift

**mGBA commit:** `e11dc3f` (closed issue #1635)

### Temporal Antialiasing / Flicker

Some Classic NES games exhibit intentional flickering on title screens (e.g., Super Mario Bros., Excitebike).

**This is by design:** The GBA's LCD screen has ghosting that blends frames together, creating a smooth image. On modern displays without ghosting, this shows as flickering.

**Not a bug:** This is correct emulation behavior. Can be mitigated with frame blending shaders.

---

## ROM Dump Quality Issues

Classic NES Series games are very sensitive to ROM dump quality.

**Problem indicators:**

- `[f1]` (bad dump) → Often shows "Game Pak Error" or crashes
- `[f_4]`, `[f_5]` (hacked) → "Wrong input" behavior - buttons barely work
- `[b1]` (bad) → Various issues

**Working dumps:**

- `[o1]` (overdump) → Works correctly after overdump handling
- `[!]` (verified good) → May have input issues on some titles
- Clean dumps without flags → Usually work

**Overdump handling:** Some "good" dumps are overdumps (larger than actual ROM). Emulators must handle out-of-bounds ROM reads correctly.

**Related log messages:**

```
GAME ERROR: Out of bounds ROM Load8: 0x083001A0
GAME ERROR: Read from write-only I/O register: 0DC
```

---

## Internal Architecture

The Classic NES Series games work as follows:

1. **GBA Bootstrap:** Normal GBA boot process
2. **Anti-emulation checks:** All 6 tricks above are executed
3. **NES Emulator Core:** Written in ARM assembly, optimized for GBA
4. **NES ROM:** Embedded in the GBA ROM, accessed via normal memory reads
5. **Audio:** PCM channel for NES audio (unusual 16-bit FIFO writes)
6. **Graphics:** GBA tiles/sprites mapped to NES PPU output

The embedded NES emulator runs entirely on the ARM7TDMI CPU, with no special hardware assist.

---

## Debugging Checklist

When Classic NES Series games have issues, check:

1. **Game Pak Error screen:** Usually save type detection or memory mirroring issue
2. **Black screen/crash:** Pipeline depth or VRAM execution issue
3. **Input not working / sluggish:** Bad ROM dump (check for `[f_4]`, `[f_5]` flags)
4. **Garbled audio:** 16-bit FIFO write handling
5. **Graphics corruption (missing rows):** Sprite cycle counting per scanline
6. **Flickering graphics:** Often intentional temporal antialiasing (not a bug)

---

## Code References

### mGBA Commits for Classic NES Series Fixes

| Issue          | Commit    | Description                         |
| -------------- | --------- | ----------------------------------- |
| VRAM execution | `501b6b6` | Allow code execution in VRAM        |
| Save type      | `c52edab` | Force EEPROM for Classic NES games  |
| Pipeline depth | `28ac288` | Add proper decode stage to pipeline |
| Audio FIFO     | `c52a5d2` | Handle 16-bit FIFO writes           |
| Sprite cycles  | `e11dc3f` | Per-scanline sprite cycle counting  |

### Relevant Source Files in mGBA

- `src/gba/memory.c` - Memory mirroring, STM handling
- `src/arm/arm.c` - ARM7TDMI pipeline implementation
- `src/gba/dma.c` - DMA register handling
- `src/gba/savedata.c` - Save type detection/override
- `src/gba/audio.c` - Audio FIFO handling
- `src/gba/renderers/software-obj.c` - Sprite cycle counting

---

## Summary

Classic NES Series games require:

1. ✅ Correct memory mirroring
2. ✅ VRAM code execution
3. ✅ Correct STM instruction ordering for DMA setup
4. ✅ Forced EEPROM save type
5. ✅ **3-stage CPU pipeline** (critical - most common failure point)
6. ✅ 16-bit audio FIFO write support
7. ✅ Per-scanline sprite cycle counting

If your emulator passes all these requirements, Classic NES Series games should work correctly with proper ROM dumps.

---

## References

- [mGBA Blog: Classic NES Series Anti-Emulation Measures](https://mgba.io/2014/12/28/classic-nes/)
- [mGBA GitHub Issues #232, #236, #1635, #3279](https://github.com/mgba-emu/mgba/issues)
- [GBATek - GBATEK Technical Reference](https://problemkaputt.de/gbatek.htm)
- [TONC - GBA Programming Tutorial](https://www.coranac.com/tonc/text/)
- [Copetti - Game Boy Advance Architecture](https://www.copetti.org/writings/consoles/game-boy-advance/)
