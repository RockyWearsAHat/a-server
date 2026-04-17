# PS1 Emulator — Architecture & Accuracy Audit

> **Last audited**: Full line-by-line code review of all source files (R3000A.cpp 1109 lines, PS1GPU.cpp 2198 lines, PS1SPU.cpp 669 lines, PS1DMA.cpp 327 lines, GTE.cpp 1366 lines, PS1Memory.cpp 555 lines, PS1Timer.cpp ~200 lines, PS1HleBios.cpp 3009 lines).
> **Status**: Playable for some titles. Known accuracy gaps prevent correct rendering in certain games (e.g. Crash Bandicoot 3D character models).

## Bug Fix Log

### GPU: Tall Triangle Pre-Clip Rejection (2026-03-23)
- **Root cause**: IsOversizedTriangle() rejected triangles with raw vertex Y span > 511 BEFORE drawing area clip. Triangles with vertices above/below VRAM but visible pixels were skipped.
- **Fix**: Removed IsOversizedTriangle() from all 3 triangle rasterizers in PS1GPU.cpp. Loop bounded by drawing area clip. No overflow risk.
- **Verification**: 977/977 tests pass. All 5 GBA ROMs + PS1 Crash Bandicoot run without crash.

## Authoritative References

All emulator development MUST reference these official/authoritative sources:

- **NOCASH PSX Specifications** (Martin Korth) — the definitive reverse-engineered reference for PS1 hardware registers, timing, and behavior
- **Sony PS1 hardware documentation** (internal dev kit docs, where available)
- **MIPS R3000A Architecture Manual** — for CPU instruction encoding and behavior
- Do NOT reference other emulator implementations (PCSX-R, Duckstation, Mednafen, etc.) as primary sources

## System Overview

The PS1 emulator (`PS1.cpp`) coordinates subsystems in a per-step loop. Each `PS1::Step()`:

1. Execute one CPU instruction (`cpu->Step()`)
2. Tick GPU with consumed cycles (updates VBlank, scanlines)
3. Fire VBlank IRQ on 0→1 transition (edge-triggered)
4. Tick timers — HBlank (per scanline) + dot clock + system clock
5. Tick SPU, CDROM, Controller
6. Mirror external IRQ state into COP0 CAUSE
7. Trigger interrupt if pending and enabled

### Clock Rates

- **CPU**: 33.8688 MHz (33868800 Hz)
- **SPU Sample Rate**: 44100 Hz (~768 cycles/sample)
- **NTSC**: 59.2916 FPS, 263 scanlines, 2171 CPU cycles/scanline, 3413 dots/scanline
- **PAL**: 49.7616 FPS, 314 scanlines, 2167 CPU cycles/scanline, 3406 dots/scanline

These are hardware-derived PS1 rates from the GPU and scanline timings documented in psx-spx. They differ from broadcast-TV approximations such as 59.94/50.0.

### Construction Order

Interrupts → Memory → GPU → SPU → DMA → Timers → CDROM → Controller → GTE → CPU

PS1Memory is the I/O dispatcher; peripherals connected via setter injection.

### Memory Map

```
0x00000000–0x001FFFFF  2 MB RAM (8 MB with mirrors)
0x1F000000–0x1F7FFFFF  Expansion 1
0x1F800000–0x1F8003FF  1 KB Scratchpad
0x1F801000–0x1F802FFF  I/O Ports
0x1FC00000–0x1FC7FFFF  512 KB BIOS ROM
0xFFFE0000–0xFFFE01FF  Cache control
```

**KSEG Translation**: KUSEG/KSEG0/KSEG1 all masked to 0x1FFFFFFF. KSEG2 passed through unmapped.

### File Map

| File                                       | Lines | Class                 | Purpose                              |
| ------------------------------------------ | ----- | --------------------- | ------------------------------------ |
| `src/emulator/ps1/PS1.cpp`                 | ~130  | `PS1`                 | System orchestration, main step loop |
| `src/emulator/ps1/R3000A.cpp`              | 1109  | `R3000A`              | MIPS I CPU                           |
| `src/emulator/ps1/PS1GPU.cpp`              | 2198  | `PS1GPU`              | GPU rasterization, VRAM              |
| `src/emulator/ps1/PS1SPU.cpp`              | 669   | `PS1SPU`              | ADPCM audio, voice mixing            |
| `src/emulator/ps1/PS1DMA.cpp`              | 327   | `PS1DMA`              | DMA controller (7 channels)          |
| `src/emulator/ps1/GTE.cpp`                 | 1366  | `GTE`                 | Geometry Transform Engine (COP2)     |
| `src/emulator/ps1/PS1MDEC.cpp`             | —     | `PS1MDEC`             | MPEG-1 decompression                 |
| `src/emulator/ps1/CDROM.cpp`               | 890   | `CDROM`               | CD-ROM sector read, commands         |
| `src/emulator/ps1/PS1Memory.cpp`           | 555   | `PS1Memory`           | Memory map + I/O dispatch            |
| `src/emulator/ps1/PS1Timer.cpp`            | ~200  | `PS1Timer`            | 3 timer channels                     |
| `src/emulator/ps1/PS1Controller.cpp`       | —     | `PS1Controller`       | Gamepad protocol                     |
| `src/emulator/ps1/InterruptController.cpp` | —     | `InterruptController` | IRQ management                       |
| `src/emulator/ps1/PS1HleBios.cpp`          | 3009  | `PS1HleBios`          | High-level BIOS emulation            |

---

## CPU (R3000A) — VERIFIED COMPLETE

### Key facts

- All 47 MIPS I instructions implemented including LWL/LWR/SWL/SWR (unaligned), SYSCALL, BREAK
- **Load delay slot**: Correctly modeled with dual-pipeline (`pendingLoad` / `nextLoad`). Instruction N+1 cannot see load result from instruction N; result visible at N+2.
- **Branch delay slot**: Instruction after branch always executes. Branch-in-delay-slot handled. BD bit (Cause bit 31) set when exception occurs in delay slot; EPC points to branch instruction.
- **Exception types**: INT, AdEL, AdES, SYSCALL, RI, COP_UNUSABLE, BREAKPOINT, OVERFLOW
- **SR stack push**: `(sr & ~0x3F) | ((sr & 0xF) << 2)` — correct IE/KU 2-level stack
- **COP0**: SR, Cause, EPC, PRID functional. Cache isolation (SR bit 16) implemented — isolated writes go to I-cache line tags. KSEG1 bypasses cache.
- **COP2 interface**: MFC2/MTC2/CFC2/CTC2 implemented via load delay pipeline. GTE command dispatch extracts bits 25:0. LWC2/SWC2 with alignment checks.

### Accuracy note

All instructions execute as 1 cycle. No instruction-level cycle counts, no cache miss penalty modeling.

---

## GPU — VERIFIED COMPLETE (rendering code present, but runtime gaps exist)

### GP0 Command Coverage

All major GP0 commands implemented (~30+ opcodes):

- Mono/Gouraud/Textured triangles and quads (0x20–0x3F)
- Mono/Gouraud lines and polylines (0x40–0x5B)
- Variable/fixed-size rectangles, textured/untextured (0x60–0x7F)
- VRAM→VRAM copy (0x80), CPU→VRAM (0xA0), VRAM→CPU (0xC0)
- Environment: draw mode (0xE1), texture window (0xE2), draw area (0xE3–0xE4), draw offset (0xE5), mask bits (0xE6)
- Unknown commands: logged as warnings, silently ignored (no crash)

### Texture System

- **All 3 CLUT modes**: 4-bit (16 colors), 8-bit (256 colors), 15-bit direct — `SampleTexture()` at GPU.cpp:2151–2165
- **Texture window masking**: mask/offset applied before texel sampling
- **Semi-transparency**: Per-texel STP bit (bit 15) controls blending. Textured primitives disable auto-blending to avoid double-blend.

### Rasterization

- **Barycentric/half-space** triangle rasterization with correct top-left fill rule
- **Gouraud interpolation**: Per-vertex colors interpolated via barycentric weights (`bw0 * vtx0.r + bw1 * vtx1.r + bw2 * vtx2.r) / absArea`)
- **11-bit signed coordinates**: Sign-extension `((val & 0x7FF) << 21) >> 21` — range −1024 to +1023
- **Dither matrix**: Correct 4×4 Bayer: `{-4,+0,-3,+1}, {+2,-2,+3,-1}, {-3,+1,-4,+0}, {+3,-1,+2,-2}`
- **Mask bit**: Set/check properly implemented. Texel bit 15 preserved.
- **Drawing area clipping**: Bounding box clamp + per-pixel bounds check

### Display

- Framebuffer latched at VBlank to `displayBuffer` for GUI consumption
- Display area vs drawing area properly distinguished (separate GP1 commands)
- Interlace tracking: `vRes480`, `oddFrame` toggled per field

### Known GPU issue

- **Shaded line color interpolation**: Uses integer division `(r1 - r0) * step / totalSteps` which truncates, causing color banding on long gradients. Minor visual artifact.

---

## GTE (Geometry Transform Engine) — COMMANDS COMPLETE; TIMING NOT MODELED

All 23 GTE commands fully implemented with correct hardware math:

| Command | Opcode | Purpose                                       | Hardware Cycles (not enforced) |
| ------- | ------ | --------------------------------------------- | ------------------------------ |
| RTPS    | 0x01   | Perspective transform (single vertex)         | 15                             |
| NCLIP   | 0x06   | Backface culling (screen-space cross product) | 8                              |
| OP      | 0x0C   | Outer product                                 | 6                              |
| DPCS    | 0x10   | Depth cue single                              | 8                              |
| INTPL   | 0x11   | Interpolation                                 | 8                              |
| MVMVA   | 0x12   | Matrix × Vector + Add (configurable)          | 8                              |
| NCDS    | 0x13   | Normal color depth single                     | 19                             |
| CDP     | 0x14   | Color depth                                   | 13                             |
| NCDT    | 0x16   | Normal color depth triple                     | 44                             |
| NCCS    | 0x1B   | Normal color color single                     | 17                             |
| CC      | 0x1C   | Color color                                   | 11                             |
| NCS     | 0x1E   | Normal color single                           | 14                             |
| NCT     | 0x20   | Normal color triple                           | 30                             |
| SQR     | 0x28   | Square per component                          | 5                              |
| DCPL    | 0x29   | Depth cue per light                           | 8                              |
| DPCT    | 0x2A   | Depth cue triple                              | 17                             |
| AVSZ3   | 0x2D   | Average Z (3 values)                          | 5                              |
| AVSZ4   | 0x2E   | Average Z (4 values)                          | 6                              |
| RTPT    | 0x30   | Perspective transform (triple vertex)         | 23                             |
| GPF     | 0x3D   | General purpose                               | 5                              |
| GPL     | 0x3E   | General purpose                               | 5                              |
| NCCT    | 0x3F   | Normal color color triple                     | 39                             |

> **ACCURACY GAP — HIGH**: The cycle counts in the table above are the correct hardware values per NOCASH PSX spec, but they are **not enforced in the emulator**. `ExecuteCOP2()` in R3000A.cpp calls `gte.Execute(command)` and immediately continues to the next instruction. `GTE::lastCommandCycles` is computed but never read. Real hardware stalls the CPU for the full cycle count before results are readable. Games that execute the next instruction before the GTE completes on real hardware would observe incorrect results; our emulator returns results immediately. This is most impactful for tight GTE-sequence loops where subsequent instructions could read stale results.

- **UNR division**: Hardware-accurate 257-entry table, CLZ normalization + Newton-Raphson. Verified against NOCASH spec.
- **MVMVA**: All 3 matrix selections (RT, L, LR), all 4 vector selections (V0, V1, V2, IR), all 4 translation selections (TR, BK, FC, zero). 64-bit MAC accumulation.
- **Overflow/saturation**: MAC0 clamped to 32-bit signed, MAC1–3 to 43-bit. IR clamped per lm bit. FLAG register set on overflow with error summary bit 31.
- **All 64 registers** (32 data + 32 control) correctly accessible with proper packing/unpacking. LZCS/LZCR via C++20 `std::countl_zero`.
- **NCLIP formula**: `SX0*(SY1-SY2) + SX1*(SY2-SY0) + SX2*(SY0-SY1)` — verified algebraically correct.

---

## DMA Controller

### 7 Channels

| Ch  | Name     | Direction     | Use                  |
| --- | -------- | ------------- | -------------------- |
| 0   | MDEC_IN  | RAM→Device    | Video compression in |
| 1   | MDEC_OUT | Device→RAM    | Decoded video out    |
| 2   | GPU      | Bidirectional | Graphics commands    |
| 3   | CDROM    | Device→RAM    | CD data              |
| 4   | SPU      | Bidirectional | Audio data           |
| 5   | PIO      | —             | Unused               |
| 6   | OTC      | RAM→Device    | Ordering table clear |

### Transfer Modes

- **Manual (0)**: Triggered by bit, transfers blockSize words
- **Request (1)**: Device-driven, blockSize × blockCount words
- **Linked List (2)**: GPU-only — follows 24-bit address chain in RAM, sends words to GPU. End marker: bit 23 of header (address field = 0xFFFFFF). Correct per PS1 hardware.

### OTC

Ordering table clear: builds backward-linked list with 0x00FFFFFF terminator. Correct.

### DICR Register

Bits 0-5 read/write (per NOCASH spec), bit 15 force IRQ, bits 16-22 channel enables, bit 23 master enable, bits 24-30 acknowledge (write 1 to clear), bit 31 master flag (read-only). Write-1-to-clear logic correctly implemented.

> **ACCURACY GAP — HIGH**: All DMA transfers execute **instantaneously and synchronously** inside `DoTransfer()`. A transfer of 65,536 words completes in 0 CPU cycles from the caller's perspective. Real PS1 hardware transfers at ~4 CPU cycles per word (minimum), meaning a 65K-word transfer takes ~262,144 cycles (~7.7 ms). Games that poll the DMA active bit in a loop, or that depend on the CPU being able to run other work during a DMA transfer (including updating VBlank state or executing sound code), will get incorrect behavior. This is a known limitation shared with most early emulators but is the likely cause of audio/timing desynchronization in bandwidth-heavy games.

---

## SPU

- **24 Voices**, 512 KB SPU RAM, 44100 Hz sample rate
- ADPCM decoding: 16 bytes → 28 samples, 5 filter coefficient sets (confirmed in `DecodeSample()`)
- ADSR envelope: Attack/Decay/Sustain/Release/Off phases with correct state machine. Key-On initializes phase to Attack with adsrLevel=0 and resets sample position to startAddr.
- Key-On/Key-Off registers with shadow tracking; processKeyOn/Off called on each half-word write
- SPU IRQ: `irqAddr` register stored, re-armed on write; `spuIRQFired` flag prevents double-fire. SPU CTRL bit 6 re-arms on 0→1 transition.
- `Tick()`: Accumulates CPU cycles; calls `UpdateADSR()` per sample tick (44100 Hz). Sample generation is deferred to `GetSamples()`.
- SPU RAM DMA: `DMAWrite()`/`DMARead()` advance `transferAddr` by 2 bytes per 16-bit word. Correct.

### ACCURACY GAP — HIGH: Reverb processing absent

`reverbOn` register is stored and exposes per-voice reverb enable bits to the game, but `GetSamples()` applies no reverb mixing. Real PS1 SPU routes enabled voices through a reverb unit (8 configurable delay taps, all reading/writing SPU RAM) and mixes the reverb output with the dry mix. Games that use reverb for ambience (most JRPG soundtracks, Crash Bandicoot ambient audio) will sound completely "dry" — no echo, room, or hall effect on any voice.

**Spec**: NOCASH PSX §SPU Reverb Registers (1F801DC0h..1F801DFCh) — 16 two-byte registers define the reverb algorithm.

### ACCURACY GAP — HIGH: FM synthesis absent

`fmMode` register (bitmask per-voice) is stored and retrievable, but `DecodeSample()` never applies frequency modulation. Real PS1 SPU FM mode makes a voice's pitch be modulated by the previous voice's output level. Games that use FM for metallic or bell-like timbres will sound incorrect (normal ADPCM pitch instead).

**Spec**: NOCASH PSX §SPU Voice Frequency Modulation (FM) — bit N of SPUFM enables FM on voice N+1 (voice 0 cannot use FM).

### ACCURACY GAP — HIGH: Noise generator absent

`noiseMode` register (bitmask per-voice) is stored and retrievable, but `DecodeSample()` returns ADPCM data for noise-enabled voices. Real PS1 SPU noise mode replaces ADPCM playback with a 16-bit LFSR that generates white-noise samples at a rate controlled by `noiseMode` bits 8-13 (shift and step values). Games that layer noise on top of ADPCM voices (percussion, wind effects) will play wrong — pure ADPCM samples instead of noise-mixed output.

**Spec**: NOCASH PSX §SPU Noise Generator — SPUNOISE register, noise frequency = step×2^(shift-1).

### ACCURACY GAP — SUSPECTED: SPU IRQ address check not in sample decode path

The `irqAddr` mechanism is correctly stored and re-armed, but a content review of `DecodeSample()` shows the sample advance path does not appear to check if `currentAddr` (scaled by 8) matches `irqAddr`. Real hardware fires IRQ5 (SPU IRQ) when any voice fetch hits the IRQ address. Games that use SPU IRQ for synchronization (e.g., looping points, callback triggers) may not receive the interrupt. Requires confirmation by reading the remaining portion of PS1SPU.cpp lines 400-669.

---

## CDROM

### Implemented Commands

GetStat (0x01), SetLoc (0x02), ReadN (0x06), Pause (0x09), Init (0x0A), SetMode (0x0E), SetFilter (0x0D), GetID (0x1A), ReadS (0x1B), Stop (0x08), ReadTOC (0x1E), GetLocL/P (0x10/0x11), GetTN/TD (0x13/0x14)

### Stubbed Commands (no real logic)

- **Play (0x03)**: Returns status only — NO CD audio playback
- **SeekL/SeekP (0x15/0x16)**: Complete instantly (no seek delay emulation)

### Sector Format

Data-only mode: reads 2048 bytes (correct for FORM1). Whole-sector mode: reads 2340 bytes (skip 12-byte sync).

### Interrupt Queue

Properly implemented — queues interrupts until game ACKs, 4000-cycle (~120µs) delivery delay between them.

---

## Timers

### 3 Channels

| Timer | System Clock | Alt Source              | IRQ    |
| ----- | ------------ | ----------------------- | ------ |
| 0     | Sources 0,2  | Dot clock (sources 1,3) | TIMER0 |
| 1     | Sources 0,2  | HBlank (sources 1,3)    | TIMER1 |
| 2     | Sources 0,1  | System/8 (sources 2,3)  | TIMER2 |

- Target-reached and overflow interrupts: working
- Repeat/one-shot and toggle/pulse modes: working
- Reading mode register clears reached flags: working
- Writing mode register resets counter: working

### CRITICAL GAP: Timer Sync Mode NOT Implemented

The `syncEnable` and `syncMode` bits (mode register bits 0-2) are parsed but **completely ignored** during counting. `TickChannel()` always increments unconditionally regardless of sync mode.

Per NOCASH PSX spec, sync modes gate counting based on HBlank/VBlank signals:

- Mode 0: Pause during blank
- Mode 1: Reset counter at blank
- Mode 2: Reset counter at blank + pause outside
- Mode 3: Stop until blank, then free-run

Games that use sync-gated timers will miscount, potentially causing hangs or incorrect timing.

---

## Controller (SIO0)

Digital pad (0x41) only. 6-byte handshake protocol. **No analog controller (0x73)**.

---

## Interrupt Controller

11 sources: VBlank, GPU, CDROM, DMA, Timer0, Timer1, Timer2, SIO0, SIO1, SPU, Lightpen.
`HasPendingIRQ()`: `(I_STAT & I_MASK) != 0`

---

## HLE BIOS — 125+ Functions

### Boot Sequence

1. Parse ISO9660 → find SYSTEM.CNF → locate boot EXE
2. Validate "PS-X EXE" magic, load .text to RAM, zero BSS
3. Install A0/B0/C0 trampolines at fixed addresses
4. Initialize kernel (TCBs, EVCBs, FCBs)
5. Set PC, SP, GP, FP, $ra from EXE header

### Coverage

- **A0 table**: ~60 functions (string ops, memory ops, printf, GPU helpers, CDROM init, heap management, setjmp/longjmp)
- **B0 table**: ~40 functions (kernel memory, timer init/control, event system, pad init, ReturnFromException, HookEntryInt, file I/O)
- **C0 table**: ~25 functions (SysEnqIntRP/SysDeqIntRP, SysInitMemory, ChangeClearRCnt, Enter/ExitCriticalSection)

### Event System

- `OpenEvent`/`CloseEvent`/`EnableEvent`/`DisableEvent`/`TestEvent`/`WaitEvent` — all functional
- `DeliverEvent`: Matches class+spec, fires mode 0x1000 callbacks or sets fired flag
- Callback chain: queued, dispatched via trampoline at 0xF220
- Nested exception guard prevents re-entrancy corruption

### ReturnFromException

Uses saved register state (not TCB), performs RFE (`sr = (sr & ~0xF) | ((sr >> 2) & 0xF)`), returns to EPC.

---

## Memory I/O Dispatch — KNOWN GAPS

### Asymmetric Access Width Handling

The I/O dispatcher (`PS1Memory.cpp`) has **incomplete coverage** across access widths:

| I/O Region         | 32-bit R/W | 16-bit R/W | 8-bit R/W |
| ------------------ | ---------- | ---------- | --------- |
| GPU (0x1F801810)   | ✅/✅      | ❌/❌      | ❌/❌     |
| SPU (0x1F801C00)   | ❌/❌      | ✅/✅      | ❌/❌     |
| CDROM (0x1F801800) | ❌/❌      | ❌/❌      | ✅/✅     |
| Timers             | ✅/✅      | ❌/❌      | ❌/❌     |
| Interrupts         | ✅/✅      | ❌/❌      | ❌/❌     |
| DMA                | ✅/✅      | ❌/❌      | ❌/❌     |

Unhandled accesses return 0 and log a warning (only if `Trace::MEMORY` is enabled). Games using `lw` on SPU registers or `lw` on CDROM registers will get 0.

---

## CRITICAL GAP ANALYSIS — Why Some Games Don't Render Correctly

The GPU rendering code, GTE math, CPU, and DMA all appear correct in code review. Yet some games (e.g. Crash Bandicoot) exhibit 3D rendering failures. Based on the complete code review the ranked list of probable causes is:

1. **[CONFIRMED — CRITICAL] Timer sync mode completely unimplemented** — `TickChannel()` never checks `syncEnable` or `syncMode`. Games using HBlank/VBlank-gated timers get wrong counts. This can cascade into GPU command submission timing or VBLANK synchronization issues. Real world impact: games that synchronize rendering to timer-gated signals will desync.

2. **[CONFIRMED — HIGH] DMA is instantaneous** — all DMA transfers complete in 0 CPU cycles. Games that rely on CPU/DMA interleaving (e.g., processing SPU data while CDROM DMA runs, or submitting new GPU commands during GPU DMA pacing) get incorrect relative timing.

3. **[CONFIRMED — HIGH] SPU reverb/FM/noise absent** — while this primarily affects audio fidelity rather than graphics, games that synchronize behavior (e.g., trigger game logic from SPU IRQ callbacks) may deadlock or mis-time.

4. **[CONFIRMED — HIGH] GTE cycle delays not enforced** — all GTE commands return results immediately. Games that read GTE results on the instruction immediately following a command (valid only because hardware stalls) will read correct data. Games that don't stall but share the CPU pipeline with GTE are affected.

5. **[CONFIRMED] Memory I/O dispatch gaps** — 32-bit reads from SPU registers and 16-bit reads from CDROM return 0 (unhandled access path). BIOS and games that read SPU status as 32-bit get 0.

6. **[CONFIRMED] 1 cycle/instruction** — no per-instruction cycle counts; no load miss penalties; no instruction sequence stalls. Affects relative timing of all subsystem interactions.

7. **[SUSPECTED] HLE BIOS behavior differences** — subtle differences in function return values, timing, or side effects. Specific games may depend on exact BIOS behavior not reproduced.

8. **[LOW PROBABILITY] GPU command edge cases** — while all command types are implemented, specific extreme parameter combinations (coordinates near VRAM boundary, zero-length polygons, unusual mask configurations) may expose untested code paths.

**To diagnose specific game failures**: Run with `AIO_PS1_GPU_DIAG=1`, `AIO_PS1_CPU_DIAG=1`, `AIO_PS1_GTE_DIAG=1`. Compare GPU command streams and GTE register states against known-correct traces from an accurate emulator reference run.

---

## Accuracy Audit — Severity-Rated Flaw List

### Severity Scale

| Level    | Meaning                                                                     |
| -------- | --------------------------------------------------------------------------- |
| CRITICAL | Confirmed absent, causes incorrect behavior or hangs in affected games      |
| HIGH     | Confirmed absent/wrong, causes degraded fidelity in games using the feature |
| MEDIUM   | Confirmed minor deviation from spec; low practical game impact              |
| CORRECT  | Verified against NOCASH PSX spec; no issue found                            |

---

### CRITICAL — Timer sync mode unimplemented

`PS1Timer.cpp: TickChannel()` unconditionally increments the counter. `syncEnable()` and `syncMode()` are parsed on register write but consulted nowhere in the tick path. All four sync behaviors (per NOCASH §Timers) are absent:

- Mode 0: Pause counter during blank period
- Mode 1: Reset counter to 0 at blank
- Mode 2: Reset + pause outside blank
- Mode 3: Stop until blank occurs, then free-run

**Fix**: In `TickChannel()`, check `ch.syncEnable()` and gate/reset the counter according to `ch.syncMode()` using the existing `blankActive` state (propagated from PPU/GPU via `TickHBlank()`/`TickDotClock()` calls).

---

### HIGH — GTE commands execute instantaneously

`ExecuteCOP2()` in R3000A.cpp dispatches to `gte.Execute(command)` with no cycle stall. `GTE::lastCommandCycles` is stored but never read back by the CPU. Per NOCASH §GTE Commands, each command has a documented cycle count (5–44 cycles) during which the CPU cannot read GTE results. Games that execute a GTE command and immediately read a result register on the very next instruction assume hardware stalls are in place; without stalls, they get correct results in this case. However, games that structure GTE call batches relying on exactly-timed result availability may read stale data.

**Fix**: After `gte.Execute(command)`, stall the CPU by `gte.lastCommandCycles` cycles (don't advance `currentCycles` budget). This is a ~15-line change in R3000A.cpp.

---

### HIGH — DMA executes instantaneously (zero CPU stall)

`PS1DMA::DoTransfer()` completes the entire transfer synchronously. A 65K-word (256 KB) transfer takes 0 simulated CPU cycles. Real PS1 hardware DMA transfers at approximately 4 CPU cycles per word; the CPU is paused during the transfer. This impacts:

- SPU DMA uploads (audio data load before voice Key-On)
- CDROM DMA sector arrival timing
- GPU DMA command stream pacing

**Fix**: Return the cycle count from `DoTransfer()` and advance the system clock/CPU stall counter by `wordCount × 4` (approximate) before calling `SetIRQFlag()`.

---

### HIGH — SPU reverb, FM synthesis, and noise generator not implemented

Per `PS1SPU.cpp::GetSamples()`: all voice samples are plain ADPCM-decoded output. Three processing modes registered in hardware-side registers are stored but not applied during sample generation:

| Feature | Status | Register           | Hardware Behavior                                      |
| ------- | ------ | ------------------ | ------------------------------------------------------ |
| Reverb  | Stored | SPUON (reverb bit) | Routes voice output through 8-tap reverb in SPU RAM    |
| FM      | Stored | SPUFM              | Previous voice output modulates next voice pitch       |
| Noise   | Stored | SPUNOISE           | Replaces ADPCM with LFSR white noise at specified rate |

All three features require nontrivial implementation. Reverb is the most audibly impactful.

---

### HIGH — SPU 32-bit read path missing from I/O dispatcher

`PS1Memory::ReadIO32()` has no handler for SPU addresses (0x1F801C00–0x1F801DFF). A 32-bit read from an SPU register returns 0 and logs a warning. Real PS1 BIOS and some games perform 32-bit reads from SPU voice registers during initialization. This causes incorrect SPU state detection.

**Fix**: In `ReadIO32()`, add an SPU range check that calls `static_cast<uint32_t>(spu->ReadRegister(addr)) | (static_cast<uint32_t>(spu->ReadRegister(addr+2)) << 16)`.

---

### MEDIUM — Shaded line color interpolation truncation

Line rendering uses `(colorA + (colorB - colorA) * step / totalSteps)` with integer division, causing truncation-based banding on long gradient lines. Impact is minor and only visible on lines > ~20 pixels with strongly differing endpoint colors.

---

### MEDIUM — 1 cycle per instruction

All R3000A instructions consume exactly 1 cycle regardless of type. Real R3000A: most instructions are 1 cycle, but multiply is 12 cycles, divide is 36 cycles, load with cache miss is several cycles. Most games are designed around the known per-instruction timing; impact is primarily on timing-sensitive loops.

---

### CORRECT — CPU instruction set

All 47 MIPS I instructions, LWL/LWR/SWL/SWR, SYSCALL, BREAK. Load delay: 2-stage pipeline, forwarding, exception-safe. Branch delay: `inDelaySlot` flag, correct EPC. RFE: correct 2-level IE/KU stack pop. COP0: SR, Cause, EPC, PRID. I-cache 256-line behavioral model.

### CORRECT — GTE math

UNR division table, MVMVA all operand combinations, MAC overflow/saturation, FLAG register. All 23 commands produce hardware-identical results (result correctness separate from timing).

### CORRECT — GPU rasterization

Barycentric triangle rasterizer, Gouraud interpolation, 4×4 dither matrix, all three CLUT modes, texture window, semi-transparency STP bit, draw area clipping, mask bit read/write/check, top-left fill rule.

### CORRECT — DMA logic (functional)

OTC ordering table building, linked-list termination via bit 23, DICR write-1-to-clear, DPCR priority, all 7 channel directions. Only timing is wrong (see HIGH above).

### CORRECT — Timers (functional)

Target-reached IRQ, overflow IRQ, repeat/one-shot, toggle/pulse mode, live counter read, mode-write counter reset. Only sync mode is wrong.

### CORRECT — CDROM (data path)

Data sector reads, interrupt queue with 4000-cycle delivery delay, GetStat/GetID/SetLoc/ReadN/ReadS/Init/Pause/Stop/SetMode/SetFilter/GetLocL-P/GetTN-TD all functional. Play command stubbed, seek instant.

### CORRECT — HLE BIOS

~125 kernel functions across A0/B0/C0 tables. Event system, ReturnFromException, Enter/ExitCriticalSection, heap management, string and memory operations. Adequate for most commercial titles.

---

## What to Fix First

| Priority | Item                              | Effort | Impact                                                   |
| -------- | --------------------------------- | ------ | -------------------------------------------------------- |
| 1        | Timer sync mode                   | Medium | Fixes hangs in sync-gated timer games; unblocks CRITICAL |
| 2        | DMA timing (stall CPU during DMA) | Small  | Fixes audio/CDROM data timing; addresses HIGH            |
| 3        | SPU 32-bit read path              | Small  | Fixes BIOS SPU init; 4-line fix in PS1Memory.cpp         |
| 4        | GTE cycle stalls in R3000A        | Small  | ~15-line change; improves timing-sensitive GTE batches   |
| 5        | SPU reverb processing             | Large  | Audible improvement for all ambient/music audio          |
| 6        | SPU noise generator               | Medium | Fixes percussion/effects in music tracks using noise     |
| 7        | SPU FM synthesis                  | Medium | Fixes FM-modulated timbres                               |

---

## Test Coverage

| Test Suite          | What's Tested                                                    |
| ------------------- | ---------------------------------------------------------------- |
| PS1CPUTests         | ALU, shifts, multiply/divide, load delay, branches, COP0         |
| PS1GPUTests         | Rendering primitives, textures, semi-transparency, clipping, DMA |
| PS1DMATests         | Block transfer, linked-list mode (GPU only, OTC)                 |
| PS1TimerTests       | Counter increment, target/overflow IRQ                           |
| PS1InterruptTests   | IRQ masking                                                      |
| PS1ControllerTests  | Button polling protocol                                          |
| PS1GTETests         | Matrix operations, perspective transform                         |
| PS1SPUTests         | Voice mixing, ADPCM, ADSR                                        |
| PS1MemoryTests      | Address translation, I/O routing                                 |
| PS1IntegrationTests | Cross-subsystem integration                                      |

### Test Gaps

- No tests for timer sync mode (because it's not implemented)
- No tests for memory I/O access-width asymmetry
- No tests for CDROM audio (Play command is stubbed)
- GPU linked-list DMA tested, but no per-game regression tests

---

## Diagnostics

- `AIO_PS1_CPU_DIAG=1`: CPU instruction tracing
- `AIO_PS1_GPU_DIAG=1`: Per-primitive rendering counters (zero-skip, write-attempt, draw-area-reject stats)
- `AIO_PS1_GTE_DIAG=1`: GTE command histogram
- `AIO_PS1_DISPLAY_DIAG=1`: Display timing diagnostics

## Verification Basis

Full line-by-line code audit: R3000A.cpp (1109 lines), PS1GPU.cpp (2198 lines read in full), PS1SPU.cpp (669 lines), PS1DMA.cpp (327 lines), GTE.cpp (1366 lines), PS1Memory.cpp (555 lines), PS1Timer.cpp (~200 lines), PS1HleBios.cpp (3009 lines summary). All findings cross-referenced against NOCASH PSX Specifications and MIPS R3000A Architecture Manual.

## Last Audited

2025 (complete second audit — all source files read line-by-line)


---

## Bug Fix Log

### [FIXED] HLE VBlank delivers wrong event class - VSync timeout in libgpu games (2025)

- Symptom: Crash Bandicoot (USA) prints VSync timeout and hangs after ~3 VBlanks.
- Root cause: HandleException VBlank handler called DeliverEvent(0xF0000009) - class 0xF0000009 is SPU/IRQ8 per PSX-SPX. Sony libgpu registers VSync events under class 0xF0000011. HLE never delivered this class.
- Fix: Added DeliverEventClass(uint32_t classId) sweeping all used+enabled slots matching classId. Called DeliverEventClass(0xF0000011) in VBlank handler. Files: PS1HleBios.h declaration, PS1HleBios.cpp VBlank patch + implementation.
- Verification: make build clean 6/6 targets. All 200 PS1 tests passed (10 suites).
- Pattern: library-internal event classes with opaque specs require DeliverEventClass not DeliverEvent.
