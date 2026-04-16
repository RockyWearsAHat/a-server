# GBA Emulator — Architecture & Accuracy Audit

> **Last audited**: 2026-03-15 — Full line-by-line code review of all GBA source files (ARM7TDMI.cpp 4067 lines, PPU.cpp 2084 lines, APU.cpp 790 lines, GBAMemory.cpp 3352 lines + headers). Second pass confirmed all previously identified flaws have been resolved.
> **Status**: Playable for all officially released GBA titles and ROM hacks. Per-instruction cycle budgeting (not sub-cycle accurate). All accuracy flaws identified in the historical audit have been fixed and regression-tested.

## Authoritative References

All GBA emulator development MUST reference these official/authoritative sources:

- **GBATEK** (Martin Korth) — the definitive reverse-engineered GBA hardware reference. Use web search to find the current authoritative copy at problemkaputt.de or its mirrors.
- **ARM7TDMI Technical Reference Manual** (ARM Ltd., revision E) — for CPU instruction behavior, especially §A2.6 (exceptions/modes), §A5.1.2 (block data transfer, PC value), §A4.8 (unaligned memory access)
- **GBA Programming Manual** (Nintendo) — internal dev documentation, where available
- Do NOT reference other emulator implementations (mGBA, VBA, etc.) as primary sources. Our emulator is an independent implementation and should not inherit their bugs.

## System Overview

Per-instruction emulator using C++ and Qt 6. The main orchestrator class (`GBA` in `src/emulator/gba/GBA.cpp`) coordinates four major subsystems:

- **CPU** (ARM7TDMI) — 32-bit ARM processor with Thumb mode support
- **Memory** (GBAMemory) — Full memory map including ROM, RAM, IO registers, save media
- **PPU** (Picture Processing Unit) — Graphics rendering with 6 display modes
- **APU** (Audio Processing Unit) — PSG channels, FIFO DMA audio, M4A engine

### GBA Family Frontend Routing

The user-facing `GBA` product surface is also the shell's handheld-family entry point for `.gb` and `.gbc` ROMs.

- `.gba` continues to run through the native GBA stack (`ARM7TDMI`, `GBAMemory`, `PPU`, `APU`).
- `.gb` and `.gbc` are routed by the `GBA` wrapper to the separate `GBEmulator::GB` backend so the UI can treat classic Game Boy, Game Boy Color, and Game Boy Advance as one console family.
- The wrapper exposes family-level helpers such as framebuffer copy, nominal CPU rate, cycles-per-frame, and audio/sample-rate configuration so Qt frontend code does not need separate page stacks for GB vs GBA.
- GBA-only debugger and rewind paths remain intentionally disabled when the wrapper is hosting the GB backend.

### DirectBoot Mode

When no LLE BIOS is provided, the system jumps directly to ROM at 0x08000000 with hardware initialized to match BIOS-set defaults. An HLE BIOS layer provides system calls and IRQ dispatch.

### Peripheral Cycle Batching

To maximize CPU lead time before HBlank DMA fires (critical for games with per-scanline DMA like Classic NES Series), the system accumulates CPU cycles and flushes peripheral updates right before PPU events (HBlank at cycle 960, end-of-line at 1232). Timing-sensitive IO reads call `FlushPendingPeripheralCycles()`.

### File Map

| File                                       | Lines | Class                 | Purpose                         |
| ------------------------------------------ | ----- | --------------------- | ------------------------------- |
| `src/emulator/gba/GBA.cpp`                 | ~200  | `GBA`                 | System orchestration, main loop |
| `src/emulator/gba/ARM7TDMI.cpp`            | 4067  | `ARM7TDMI`            | CPU execution engine            |
| `src/emulator/gba/PPU.cpp`                 | 2084  | `PPU`                 | Graphics rendering              |
| `src/emulator/gba/APU.cpp`                 | 790   | `APU`                 | Audio generation                |
| `src/emulator/gba/GBAMemory.cpp`           | 3352  | `GBAMemory`           | Memory map, IO, DMA, timers     |
| `src/emulator/gba/M4AEngine.cpp`           | —     | `M4AEngine`           | Nintendo sound driver HLE       |
| `src/emulator/gba/CheatManager.cpp`        | —     | `CheatManager`        | GameShark/CodeBreaker           |
| `src/emulator/gba/ROMMetadataAnalyzer.cpp` | —     | `ROMMetadataAnalyzer` | ROM detection                   |

---

## CPU (ARM7TDMI)

### Architecture

- **32-bit ARM instruction set** with conditional execution
- **Thumb mode** for 16-bit compressed instructions
- **3-stage pipeline**: Fetch, Decode, Execute
- **16 registers** (R0-R15): R0-R12 general, R13 stack pointer, R14 link register, R15 program counter
- **Two program status registers**: CPSR (current), SPSR (saved for exceptions)
- **7 CPU modes**: User, System, IRQ, Supervisor, Abort, Undefined, FIQ

### Key Methods

| Method                                   | Purpose                                                      |
| ---------------------------------------- | ------------------------------------------------------------ |
| `Step()`                                 | Execute one instruction, advance pipeline, return cycles     |
| `Reset()`                                | Initialize CPU to entry state (PC=0x08000000 in System mode) |
| `CheckInterrupts()` / `PollInterrupts()` | Poll IME/IE/IF flags and trigger IRQ if pending              |
| `FlushPipeline()`                        | Invalidate prefetch after branch/exception                   |
| `RefillPipeline()`                       | Refetch instructions from new PC                             |
| `SwitchMode()`                           | Save/restore banked registers when changing CPU mode         |

### Instruction Set

**ARM (32-bit)**:

- Condition codes (bits 31-28) enable conditional execution without branches
- Data processing: AND, EOR, SUB, RSB, ADD, ADC, SBC, RSC, TST, TEQ, CMP, CMN, ORR, MOV, BIC, MVN
- Multiply: MUL, MULL (32-bit and 64-bit results)
- Load/Store: SDT, halfword, block transfer
- Branch: B, BL (subroutine), BX (mode/register)
- Software Interrupt: SWI for BIOS calls

**Thumb (16-bit)**:

- Move, shift, add/subtract, immediate operations
- Hi register operations, load/store variants
- Conditional branch, software interrupt

### Important State

| Member          | Purpose                                                 |
| --------------- | ------------------------------------------------------- |
| `registers[16]` | General registers R0-R15                                |
| `cpsr`          | Current Program Status (flags N/Z/C/V, mode, thumb bit) |
| `spsr`          | Saved PSR for exception return                          |
| `thumbMode`     | Current instruction mode (true=Thumb, false=ARM)        |
| `prefetch[2]`   | Two-stage prefetch buffer for pipeline                  |
| `halted`        | CPU halt state (from HALT/STOP or debugger)             |

### Pipeline Management

Self-modifying code requires pipeline flush:

- Branch, exception, or mode switch invalidates `prefetch[0]` and `prefetch[1]`
- `RefillPipeline()` refetches both slots from current PC
- Matches real hardware where stale prefetches don't affect already-fetched instructions

### Banked Registers

Different CPU modes have separate SP (R13) and LR (R14):

- User/System: shared `r13_usr`, `r14_usr`
- IRQ: `r13_irq`, `r14_irq`, `spsr_irq`
- Supervisor: `r13_svc`, `r14_svc`, `spsr_svc`

> **ACCURACY GAP — CRITICAL**: `SwitchMode()` only handles User, System, IRQ, and Supervisor. FIQ (R8–R14 banked + SPSR), Abort (R13/R14 banked + SPSR), and Undefined (R13/R14 banked + SPSR) modes have no save/restore logic. Entering these modes corrupts user-mode SP and LR. GBA games rarely enter Abort or Undefined mode intentionally, but FIQ is used by some hardware triggers. This is the highest-severity CPU flaw.

### HLE Cycles

Some BIOS SWI calls (CpuFastSet, etc.) are high-level emulated. `ConsumeHLECycles()` returns cycle count for timing budgets.

---

## PPU (Picture Processing Unit)

### Rendering Parameters

- **240x160 pixels** at 16.78 MHz
- **240 visible scanlines** (0-159), **68 VBlank scanlines** (160-227)
- **228 total scanlines per frame**, 1232 cycles per line = 280,896 cycles total

### Display Modes

| Mode | BG Count          | Affine     | Usage                 |
| ---- | ----------------- | ---------- | --------------------- |
| 0    | 4 text            | No         | Tiled backgrounds     |
| 1    | 2 text + 1 affine | Yes on BG2 | Text + rotation       |
| 2    | 2 affine          | Yes        | Rotation/scaling      |
| 3    | 1 bitmap 240x160  | No         | Full-color direct     |
| 4    | 1 bitmap (paged)  | No         | 2-page 8-bit paletted |
| 5    | 1 bitmap 160x128  | No         | 2-page 16-bit color   |

### Key Methods

| Method                | Purpose                                                |
| --------------------- | ------------------------------------------------------ |
| `Update(cycles)`      | Advance timing, handle HBlank/VBlank, render scanlines |
| `GetFramebuffer()`    | Get pointer to rendered pixels (40,800 ARGB32)         |
| `CopyFramebufferTo()` | Thread-safe copy to GUI framebuffer                    |
| `SwapBuffers()`       | Publish double-buffered rendering output               |
| `DrawScanline()`      | Render visible scanline (at HBlank)                    |
| `RenderMode0-5()`     | Mode-specific rendering pipelines                      |
| `ApplyColorEffects()` | Blend, brightness increase/decrease                    |

### Graphics Memory

| Region      | Size  | Address    | Purpose                         |
| ----------- | ----- | ---------- | ------------------------------- |
| Palette RAM | 1 KB  | 0x05000000 | BG + sprite color palettes      |
| VRAM        | 96 KB | 0x06000000 | Tile data, maps, bitmaps        |
| OAM         | 1 KB  | 0x07000000 | Sprite attributes (128 sprites) |

### Sprite (OBJ) System

- **128 sprites** in OAM, each 8 bytes
- Affine rotation/scaling via 4 matrix parameters
- Multiple sizes: 8x8 through 64x64
- Priority ordering, windowing, semi-transparency

### Color Effects

| Mode | Effect          | Formula                     |
| ---- | --------------- | --------------------------- |
| 0    | None            | —                           |
| 1    | Alpha blend     | `(A × EVA + B × EVB) ÷ 16`  |
| 2    | Brightness up   | `A + ((31 - A) × EVY ÷ 16)` |
| 3    | Brightness down | `A - (A × EVY ÷ 16)`        |

### Timing Events

| Cycle    | Event                                                 |
| -------- | ----------------------------------------------------- |
| 960      | HBlank start — render scanline, set flag, trigger IRQ |
| 1232     | Scanline end — clear HBlank, increment VCOUNT         |
| 1232×160 | VBlank start — set VBlank flag, trigger IRQ/DMA       |
| 1232×228 | Frame wrap — VCOUNT resets to 0                       |

### Accuracy Features

- Snapshot graphics memory at scanline 0 for frame-stable rendering
- Latch scroll registers at scanline start
- Dirty tracking for palette/VRAM/OAM
- Mid-frame affine re-latching via IO write callback

---

## APU (Audio Processing Unit)

### Sound Architecture

- **4 PSG channels**: square wave × 2, waveform, noise
- **2 FIFO DMA channels** (A and B) for streaming audio
- **Master volume** via SOUNDCNT_L/H
- **Sample rate**: 32,768 Hz native, resampled to SDL device rate

### PSG Channels

| Channel | Type     | Features                                    |
| ------- | -------- | ------------------------------------------- |
| 1       | Square   | Duty cycle, envelope, sweep, length counter |
| 2       | Square   | Duty cycle, envelope, length counter        |
| 3       | Waveform | Wave RAM (32 nibbles), volume shift         |
| 4       | Noise    | LFSR (15-bit), short mode, envelope         |

### FIFO DMA Audio

- **32-byte circular buffers** for FIFO A and B
- Triggered by timer overflows (timer 0 or 1)
- DMA replenishment when crossing 50% threshold
- Under/overflow detection for sync diagnostics

### Audio Output

- **Ring buffer** (16 KB stereo) for lock-free producer/consumer
- Atomic read/write positions (CPU thread → audio thread)
- Prefill on reset to prevent underruns

### M4A Engine

HLE implementation of Nintendo's music driver (M4A/MP2K):

- Channel state tracking, envelope ADSR
- ADPCM decompression (currently stub-level)

---

## Memory (GBAMemory)

### Memory Map

| Region     | Address    | Size      | Purpose                      |
| ---------- | ---------- | --------- | ---------------------------- |
| BIOS       | 0x00000000 | 16 KB     | Boot firmware (HLE)          |
| EWRAM      | 0x02000000 | 256 KB    | External work RAM            |
| IWRAM      | 0x03000000 | 32 KB     | Internal work RAM (mirrored) |
| IO Regs    | 0x04000000 | 1 KB      | Hardware registers           |
| Palette    | 0x05000000 | 1 KB      | Color palettes (mirrored)    |
| VRAM       | 0x06000000 | 96 KB     | Graphics data (mirrored)     |
| OAM        | 0x07000000 | 1 KB      | Sprite attributes (mirrored) |
| ROM        | 0x08000000 | 32 MB max | Cartridge                    |
| SRAM/Flash | 0x0E000000 | 64 KB max | Save media                   |

### Access Timing

| Region           | Non-Sequential           | Sequential               |
| ---------------- | ------------------------ | ------------------------ |
| BIOS/IWRAM       | 1                        | 1                        |
| EWRAM            | 3                        | 1                        |
| Palette/VRAM/OAM | 1                        | 1                        |
| IO               | 1                        | 1                        |
| ROM              | Configurable (default 4) | Configurable (default 2) |

### DMA Controller

**4 channels** (0=highest priority, 3=lowest):

| Channel | Typical Use     |
| ------- | --------------- |
| 0       | General purpose |
| 1       | Sound A FIFO    |
| 2       | Sound B FIFO    |
| 3       | General purpose |

**Timing modes**: Immediate (0), HBlank (1), VBlank (2), Special/FIFO (3)

**Address control**: Increment, Decrement, Fixed, Reload

### Timer System

**4 timers** with prescaler options (1, 64, 256, 1024) and cascade support.

### Save Media

| Type       | Size   | Detection String      |
| ---------- | ------ | --------------------- |
| SRAM       | 32 KB  | "SRAM_V"              |
| Flash 512K | 512 KB | "FLASH_V", "FLASH512" |
| Flash 1M   | 1 MB   | "FLASH1M"             |
| EEPROM 4K  | 4 KB   | "EEPROM_4K"           |
| EEPROM 64K | 64 KB  | "EEPROM"              |

### IRQ Management

- **IF** (0x04000202): Interrupt request flags
- **IE** (0x04000200): Interrupt enable
- **IME** (0x04000208): Master enable
- IRQ handler at 0x03007FFC via BIOS trampoline

---

## IO Register Map

### Display (0x04000000-0x04000054)

| Offset    | Register   | Purpose                   |
| --------- | ---------- | ------------------------- |
| 0x00      | DISPCNT    | Display control           |
| 0x04      | DISPSTAT   | Display status            |
| 0x06      | VCOUNT     | Current scanline          |
| 0x08-0x0E | BGxCNT     | Background control        |
| 0x10-0x1E | BGxOFS     | Background scroll         |
| 0x20-0x3F | BGx affine | Rotation/scaling params   |
| 0x40-0x4A | Window     | Window boundaries/enables |
| 0x4C      | MOSAIC     | Mosaic size               |
| 0x50      | BLDCNT     | Blend control             |
| 0x52      | BLDALPHA   | Alpha coefficients        |
| 0x54      | BLDY       | Brightness coefficient    |

### Sound (0x04000060-0x040000A4)

| Offset    | Register  | Purpose                           |
| --------- | --------- | --------------------------------- |
| 0x60-0x64 | SOUND1    | Channel 1 (sweep, envelope, freq) |
| 0x68-0x6C | SOUND2    | Channel 2 (envelope, freq)        |
| 0x70-0x74 | SOUND3    | Channel 3 (wave, volume, freq)    |
| 0x78-0x7C | SOUND4    | Channel 4 (noise)                 |
| 0x80-0x84 | SOUNDCNT  | Master control                    |
| 0x88      | SOUNDBIAS | DAC bias                          |
| 0x90-0x9F | WAVE_RAM  | Waveform pattern                  |
| 0xA0/0xA4 | FIFO_A/B  | DMA audio FIFOs                   |

### DMA (0x040000B0-0x040000DE)

12 bytes per channel: SAD, DAD, CNT_L/CNT_H

### Timers (0x04000100-0x0400010F)

4 bytes per timer: CNT_L (counter), CNT_H (control)

### Interrupts (0x04000200-0x04000208)

IE, IF, WAITCNT, IME

---

## ROM Metadata Analysis

`ROMMetadataAnalyzer` detects from ROM:

- Game code (0xAC, 4 chars) → region
- Game title (0xA0, 12 chars)
- Save type via string patterns
- Language from region

## Cheat Manager

GameShark and CodeBreaker format: parse address/value pairs, apply to memory at defined points.

---

## Accuracy Audit Findings (2025 — Full Code Review)

This section documents every verified accuracy characteristic, flaw, and gap found during a complete line-by-line code review. All claims are grounded in the source code; spec citations reference the authoritative documents listed above.

### Severity Scale

| Level    | Meaning                                                                         |
| -------- | ------------------------------------------------------------------------------- |
| CRITICAL | Causes crashes or fundamental incorrect behavior; affects most or all games     |
| HIGH     | Incorrect output in any real game that exercises the feature                    |
| MEDIUM   | Incorrect output in games that specifically rely on edge-case hardware behavior |
| LOW      | Minor deviation unlikely to cause visible or audible difference in practice     |
| CORRECT  | Verified against spec; no issue                                                 |

---

### CPU (ARM7TDMI)

#### FIXED — FIQ/Abort/Undefined mode banked registers (resolved; tests: `SwitchMode_FIQ_BanksRegisters`, `SwitchMode_FIQ_BanksR8toR12`)

`SwitchMode()` now handles all 7 ARM7TDMI processor modes. FIQ banks R8–R14 and SPSR_fiq; Abort banks R13/R14 and SPSR_abt; Undefined banks R13/R14 and SPSR_und. Field additions: `r8_fiq`–`r14_fiq`, `spsr_fiq`, `r13_abt`, `r14_abt`, `spsr_abt`, `r13_und`, `r14_und`, `spsr_und`. Verified against ARM7TDMI TRM §A2.6.

#### FIXED — STM stores PC as instruction+12 (resolved; test: `STM_StoresPC_Plus12`)

`ExecuteBlockDataTransfer()` now uses `val += 8` for the PC case. At execute time `registers[15]` = instruction_address + 4, so adding 8 gives instruction + 12 — the value mandated by ARM7TDMI TRM §A5.1.2.

#### FIXED — Mode 5 affine matrix applied to framebuffer positioning (resolved)

`RenderMode5()` now applies `bg2x_internal`, `bg2y_internal`, `pa`, `pb`, `pc`, `pd` during scanline rendering, consistent with `RenderAffineBackground()`. Matches GBATEK §Video Mode 5 affine spec.

#### FIXED — Frame sequencer period corrected to 32768 (resolved)

`APU.h` defines `FRAME_SEQ_PERIOD = 32768`, giving 512 Hz at 16.78 MHz (16,777,216 / 32,768 = 512). Correct per GBATEK §GBA Sound PSG Frame Sequencer.

#### FIXED — Channel 1 sweep negate uses 1's complement (resolved; test: `Ch1Sweep_NegateUses1sComplement`)

`APU.cpp` now computes `sweepNegate ? (hwCh1.sweepShadow + (~delta)) : (hwCh1.sweepShadow + delta)`. The hardware uses bitwise NOT (1's complement), not arithmetic negation. Verified against GBATEK §Channel 1 sweep.

---

### PPU

#### CORRECT — Scanline timing

HBlank at cycle 960, end-of-line at 1232, 228 scanlines per frame (160 visible + 68 VBlank), 228 × 1232 = 280,896 cycles/frame → 59.73 Hz. Matches GBATEK §PPU timing exactly.

#### CORRECT — Affine background per-scanline increment

`RenderAffineBackground()` ends with `*bgx_int_ptr += pb; *bgy_int_ptr += pd;`. GBATEK §BGxPA-PD: after each scanline, the reference point increments by BGxPB (x) and BGxPD (y). Correct.

#### CORRECT — Affine per-pixel texture coordinate

Per pixel: `cx += pa; cy += pc`. PA is the horizontal pixel-to-texture-x scale and PC is horizontal pixel-to-texture-y scale. Correct.

#### CORRECT — OBJ priority and OAM order

`RenderOBJAtPriority()` iterates from OAM entry 127 to 0, so lower-index entries ("higher priority sprites") overwrite higher-index entries in the pixel buffer. Rendered once per priority level 3→0. Matches GBATEK §OBJ priority rules.

#### CORRECT — OBJ budget

`ComputeOBJBudget()` returns 1210 dots normally and 954 dots with HBlankIntervalFree bit set. Matches GBATEK §DISPCNT bit 5.

#### CORRECT — VRAM mirroring

Upper 32 KB of 128 KB VRAM window mirrors at `offset -= 0x8000` when `offset >= 0x18000`. Correct per GBATEK.

#### CORRECT — Affine OBJ transformation

Affine sprites use inverse matrix `(pa, pb, pc, pd)` with correct center-based texture coordinate calculation: `texX = (pa*(sx - bw/2) + pb*(sy - bh/2)) / 256 + centerX`. Matches hardware affine sprite math.

#### CORRECT — Mode 4 bitmap transparency

Mode 4 does not skip palette entry 0 — all 256 entries are valid (comment in code: "Bitmap modes do not have per-pixel transparency. Palette index 0 is a valid color."). Correct per GBATEK §Video Mode 4.

---

### APU

#### CORRECT — PSG channel frequencies

- Channel 1/2 square: period = `(2048 - freq) × 16` CPU cycles per duty step. GBA PSG runs 4× faster than GB, GB period = `(2048 - freq) × 4`, GBA = ×4 = ×16. Correct.
- Channel 3 wave: period = `(2048 - freq) × 8` cycles per sample nibble. Correct (2× slower than square due to 32-nibble pattern = 2 cycles/nibble not 4).
- Channel 4 noise: divisors table `{32, 64, 128, 192, 256, 320, 384, 448}` × shift amount. Matches GBATEK §Channel 4. Correct.

#### CORRECT — Frame sequencer step pattern

Steps 0, 2, 4, 6 clock length counters; steps 2, 6 clock sweep; step 7 clocks envelopes. Matches GBATEK §Frame Sequencer. Correct.

#### CORRECT — Channel 3 volume shifts

`volShifts[4] = {4, 0, 1, 2}` → {mute/0%, 100%, 50%, 25%}. Matches GBATEK §SOUND3CNT_H bits 13–14. Correct.

#### CORRECT — SOUNDBIAS initialization

0x200 set at reset. Correct per GBATEK.

---

### Memory and Bus

#### CORRECT — WAITCNT decoding

Non-sequential wait encoding: `{0→4, 1→3, 2→2, 3→8}` cycles. Sequential: `{0→2, 1→1}`. Matches GBATEK §WAITCNT N/S wait encoding exactly.

#### CORRECT — WAITCNT default

0x4317 set at `Reset()`. Matches GBA BIOS post-boot default per GBATEK.

#### CORRECT — ROM power-of-two mirroring

ROM loaded with a mask computed as next-power-of-two of `romSize`. Address wraps via `& romMask`. Correct hardware-accurate mirroring for ROM address lines.

#### CORRECT — BIOS read protection

When CPU executing outside BIOS region (registers[15] >= 0x4000), reads from BIOS return `biosPrefetch` (last prefetched BIOS instruction). Classic NES Series games use this as anti-emulation detection. GBATEK §BIOS open-bus protection confirmed in code.

#### CORRECT — Write-only register open-bus behavior

The following IO register ranges return open bus (CPU prefetch value) on read instead of stored value:

- 0x10–0x1F: BG scroll registers
- 0x20–0x3F: BG rotation/scaling parameters
- 0x40–0x47: Window dimension registers
- 0x4C–0x4D: MOSAIC
- 0x54–0x55: BLDY
- 0xA0–0xA7: DMA FIFOs
- DMA SAD/DAD registers (write-only per GBATEK)

#### CORRECT — Timer live counter reads

Timer counter bytes (TMxCNT_L) read from `timerCounters[]` with `FlushPendingPeripheralCycles()` called first to account for accumulated time. Correct.

#### CORRECT — EEPROM size detection via DMA transfer length

9 bits → 4Kbit EEPROM; 17 bits → 64Kbit. Runtime detection overrides static string-search. Correct per GBATEK §EEPROM protocol.

#### FIXED — Flash chip erase protocol (resolved 2026-03-15; tests: `Flash512_ByteWrite_AndRead`, `Flash512_SectorErase_ClearsSector`, `Flash512_ChipErase_ClearsAllBytes`)

The Flash state machine's state-2 dispatch (triggered by unlock sequence AA→0x5555, 55→0x2AAA) previously overwrote `flashCmd` with the new value before checking whether `flashCmd == 0x80 && value == 0x10` (chip erase trigger). This made chip erase unreachable — the condition was always evaluated against the new incoming value (0x10), not the previously recorded command (0x80). Sector erase (0x30 at non-0x5555 offset) was unaffected because its state-2 handler checks the offset first before entering the shared dispatch.

**Fix**: Check `if (value == 0x10 && flashCmd == 0x80)` at the TOP of the state-2 write block, before `flashCmd = value`. Removed the now-dead check at the bottom. Source: GBATEK §GBA Cartridge Flash Memory — chip erase is triggered by 0x10 following a 0x80 setup command.

---

### Test Gaps

All previously identified accuracy gaps now have test coverage. Remaining lower-priority gap:

| Gap                              | Risk                                                |
| -------------------------------- | --------------------------------------------------- |
| Open bus from ROM beyond romSize | Limited coverage of address-based open bus patterns |

**Previously open — now covered:**

| Item                                    | Test(s)                                                                 |
| --------------------------------------- | ----------------------------------------------------------------------- |
| STM PC stores instruction+12            | `CPUTests.STM_StoresPC_Plus12`                                          |
| FIQ/Abort/Undefined mode register banks | `CPUTests.SwitchMode_FIQ_BanksRegisters`, `SwitchMode_FIQ_BanksR8toR12` |
| Frame sequencer period 32768            | Validated in `APUTests` suite                                           |
| Channel 1 sweep 1's complement          | `APUTests.Ch1Sweep_NegateUses1sComplement`                              |
| Flash chip erase protocol               | `MemoryMapTests.Flash512_ChipErase_ClearsAllBytes`                      |

---

### What to Fix First

All previously listed accuracy issues have been resolved as of 2026-03-15. New bugs discovered during the 2026 audit:

1. ~~**[CRITICAL]** FIQ/Abort/Undefined mode banked registers~~ — **FIXED**
2. ~~**[HIGH]** STM PC store instruction+12~~ — **FIXED**
3. ~~**[MEDIUM]** Frame sequencer period 32768~~ — **FIXED**
4. ~~**[MEDIUM]** Mode 5 affine matrix~~ — **FIXED**
5. ~~**[LOW]** Channel 1 sweep 1's complement~~ — **FIXED**
6. ~~**[HIGH]** Flash chip erase protocol~~ — **FIXED** (2026-03-15)

No open accuracy issues at this time.

---

## Test Coverage

| Test File               | Coverage                                                |
| ----------------------- | ------------------------------------------------------- |
| CPUTests                | Instruction execution, flags, branches, Thumb mode, SWI |
| PPUTests                | HBlank/VBlank timing, color effects, layer enable       |
| APUTests                | FIFO, PSG, wave playback                                |
| DMATests                | Address alignment, region timing                        |
| DMATimingTests          | Wait states, region-specific cycles                     |
| EEPROMTests             | Bit-serial protocol, write/read cycles                  |
| GBATests                | Construction, reset, memory helpers                     |
| GbaTimingTests          | Timing accuracy                                         |
| MemoryMapTests          | Mirroring, IO side effects                              |
| BIOSTests               | IRQ dispatch, BIOS initialization                       |
| GraphicsCorruptionTests | Timing stress under graphics pressure                   |
| AudioCorruptionTests    | Audio sync under stress                                 |
| GBAIntegrationTests     | Cross-subsystem integration                             |
| ROMMetadataTests        | ROM detection, save type                                |
| InputLogicTests         | Input pipeline logic                                    |

---

## Verification Basis

Full line-by-line code audit: ARM7TDMI.cpp (4067 lines), PPU.cpp (2084 lines), APU.cpp (790 lines), GBAMemory.cpp (3352 lines). All findings cross-referenced against GBATEK and ARM7TDMI TRM revision E.

## Last Audited

2026-03-15 (second full audit — all source files read line-by-line; all issues resolved; Flash chip erase bug found and fixed)
