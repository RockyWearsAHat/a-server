# Plan: Fix GBA Emulation Issues (OG-DK, Metroid Zero Mission, DKC)

**Status:** � DIAGNOSTICS REQUIRED
**Goal:** Fix corrupted visuals/audio in OG-DK, corrupted audio in Metroid Zero Mission, and missing birds in DKC title screen

---

## Context

### Test Suite Status: ✅ ALL 137 TESTS PASS

The unit test suite is fully passing. The issues are **runtime-only** and require diagnostic investigation.

### Issue Summary

| Game                 | Symptom                       | Likely Area       | Diagnostic Approach                                                     |
| -------------------- | ----------------------------- | ----------------- | ----------------------------------------------------------------------- |
| OG-DK                | Corrupted visuals AND audio   | Timing/IRQ/Memory | Crash log shows PC=0xD1012800 (invalid); IRQ stack corruption suspected |
| Metroid Zero Mission | Corrupted audio               | APU FIFO timing   | Monitor FIFO underflows with AIO_TRACE_AUDIO_STATS=1                    |
| DKC                  | Birds missing on title screen | OBJ rendering/OAM | Trace OAM state at title screen; check sprite visibility                |

### Crash Log Analysis (crash_log.txt)

```
PC: 0xD1012800          <- INVALID ADDRESS (should be in ROM/RAM regions)
SP: 0x7CFF7FFB          <- GARBAGE (should be ~0x03007F00)
BranchLog (last 4):
  0x00000018 -> 0x00003F00   (IRQ vector -> BIOS trampoline)
  0x00003F20 -> 0x030056A4   (BIOS return -> user IRQ handler)
  0x0300842C -> 0x03019DD4   (game code call)
  0x03019FDC -> 0xD1012800   (jump to CORRUPTED address)
```

**Key Observations:**

1. CPU gets stuck looping at `0x03005710` (IWRAM) for thousands of iterations
2. Eventually takes an IRQ, handles it, then jumps to garbage address
3. Stack pointer is completely corrupted (0x7CFF7FFB)

**Root Cause Hypothesis:** IRQ stack overflow or memory corruption during interrupt handling

---

## Steps

### Step 1: Add IRQ Stack Guard Diagnostic — `src/emulator/gba/ARM7TDMI.cpp`

**Purpose:** Detect stack corruption BEFORE it causes a crash

**Operation:** `INSERT_AFTER`
**Anchor:** In `ARM7TDMI::Step()`, after `CheckInterrupts();` (~line 806)

```cpp
  // Stack guard: detect IRQ stack corruption early
  if ((cpsr & 0x1F) == 0x12) { // IRQ mode
    const uint32_t sp_irq = registers[13];
    // IRQ stack should be in range 0x03007FA0-0x03007FFF
    if (sp_irq < 0x03007F00 || sp_irq > 0x03008000) {
      std::cerr << "[IRQ_STACK_GUARD] SP_irq=0x" << std::hex << sp_irq
                << " out of range! PC=0x" << registers[15] << std::dec << std::endl;
    }
  }
```

**Verify:** `make build && AIO_TRACE_GBA_SPAM=1 ./build/bin/AIOServer` with OG-DK ROM

---

### Step 2: Add Audio Stats Diagnostic — Already exists via environment variable

**No code change needed.** Use:

```bash
AIO_TRACE_AUDIO_STATS=1 ./build/bin/AIOServer
```

Look for:

- `fifoAUnderflow/s` or `fifoBUnderflow/s` > 0 indicates timing issue
- `ringUnderrunSamples/s` > 0 indicates emulation too slow
- `upsample` ratio should be stable around 1.4-2.5

---

### Step 3: Investigate DKC OAM State — Add OAM dump at title screen

**Operation:** `INSERT_AFTER`
**Anchor:** In PPU::RenderOBJ(), after the `if (scanline == 0)` trace block (around line 1050)

Add a diagnostic to dump OAM entries when environment variable is set:

```cpp
  // DKC Birds diagnostic: dump OAM state at specific frames
  static const bool dumpOAM = (std::getenv("AIO_DUMP_OAM") != nullptr);
  if (dumpOAM && scanline == 0) {
    static int oamDumpCount = 0;
    if (oamDumpCount < 10 && frameCount >= 300 && frameCount <= 310) {
      oamDumpCount++;
      std::cout << "[OAM_DUMP Frame " << frameCount << "]" << std::endl;
      for (int i = 0; i < 128; ++i) {
        uint32_t oamBase = 0x07000000 + i * 8;
        uint16_t attr0 = memory.Read16(oamBase);
        uint16_t attr1 = memory.Read16(oamBase + 2);
        uint16_t attr2 = memory.Read16(oamBase + 4);

        int y = attr0 & 0xFF;
        int objMode = (attr0 >> 8) & 3;
        int shape = (attr0 >> 14) & 3;
        int x = attr1 & 0x1FF;
        int size = (attr1 >> 14) & 3;
        int tileNum = attr2 & 0x3FF;
        int priority = (attr2 >> 10) & 3;

        // Only dump non-hidden sprites
        if (objMode != 2 && tileNum != 0) {
          std::cout << "  OBJ[" << i << "] x=" << x << " y=" << y
                    << " tile=" << tileNum << " pri=" << priority
                    << " mode=" << objMode << std::endl;
        }
      }
    }
  }
```

**Verify:** `AIO_DUMP_OAM=1 ./build/bin/AIOServer` with DKC ROM, check if bird sprites appear in dump

---

### Step 4: Check Timer Cascade Behavior — `src/emulator/gba/GBAMemory.cpp`

**Analysis Required:** Timer cascade (bit 2 of TMxCNT_H) behavior may affect audio timing.

Search for timer cascade handling in `UpdateTimers()`:

```bash
grep -n "cascade\|count-up" src/emulator/gba/GBAMemory.cpp
```

If cascade is implemented incorrectly, Timer 0 overflow might not properly trigger Timer 1, affecting sound FIFOs that use Timer 1.

**Expected behavior (GBATEK):**

- When TMxCNT_H bit 2 = 1, timer increments when PREVIOUS timer overflows, not on prescaler
- Cascaded timer ignores prescaler setting

---

### Step 5: Verify BIOS IRQ Handler Address — Check `0x03007FFC` writes

The crash shows the CPU jumping to garbage after IRQ. The user IRQ handler is read from `0x03007FFC` (mirrored at `0x03FFFFFC`).

**Diagnostic:** Add trace for writes to IRQ handler address:

**Operation:** `INSERT_AFTER`  
**Anchor:** In `GBAMemory::Write32()` around the IWRAM write handling

```cpp
  // Trace writes to IRQ handler address
  if ((address & 0x03FFFFF8) == 0x03007FF8) {
    const uint32_t pc = cpu ? cpu->GetRegister(15) : 0;
    std::cerr << "[IRQ_HANDLER_WRITE] addr=0x" << std::hex << address
              << " val=0x" << value << " PC=0x" << pc << std::dec << std::endl;
  }
```

This will show if the IRQ handler address is being corrupted.

---

## Runtime Diagnostic Commands

### OG-DK (Corrupted visuals + audio)

```bash
# Full diagnostic run
AIO_TRACE_GBA_SPAM=1 AIO_TRACE_AUDIO_STATS=1 ./build/bin/AIOServer 2>&1 | tee ogdk_diag.log

# Check for stack corruption
grep -E "SP_irq|IRQ_HANDLER_WRITE|IRQ_STACK_GUARD" ogdk_diag.log
```

### Metroid Zero Mission (Corrupted audio)

```bash
# Audio-focused diagnostic
AIO_TRACE_AUDIO_STATS=1 ./build/bin/AIOServer 2>&1 | tee metroid_audio.log

# Check for FIFO issues
grep -E "fifo.*Underflow|upsample|ringUnder" metroid_audio.log
```

### DKC (Missing birds)

```bash
# OAM diagnostic at title screen (wait ~5 seconds after boot)
AIO_DUMP_OAM=1 ./build/bin/AIOServer 2>&1 | tee dkc_oam.log

# Check what sprites are present
grep -E "OBJ\[" dkc_oam.log | head -50
```

---

## Test Strategy

1. `make build` — compiles without errors ✅ (verified)
2. `cd build && ctest --output-on-failure` — all 137 tests pass ✅ (verified)
3. **Diagnostic runs required** — see commands above for each game
4. **Post-diagnostic:** Write specific code fixes based on findings

---

## Documentation Updates

No memory.md updates until specific fixes are identified and implemented.

---

## Handoff

**This plan requires DIAGNOSTIC DATA before implementation.**

Current state:

- ✅ Tests pass (137/137)
- ✅ Build succeeds
- ⏳ Runtime issues need investigation

**Next steps for `@Implement`:**

1. **DO NOT** implement the diagnostic code insertions yet
2. **RUN** the emulator with each problematic ROM and collect diagnostic output:

   ```bash
   # OG-DK
   AIO_TRACE_AUDIO_STATS=1 ./build/bin/AIOServer
   # Load OG-DK.gba, play for 10 seconds, note any crashes/corruption

   # Metroid Zero Mission
   AIO_TRACE_AUDIO_STATS=1 ./build/bin/AIOServer
   # Load Metroid - Zero Mission (USA).gba, play for 30 seconds

   # DKC
   ./build/bin/AIOServer
   # Load DKC.gba, wait at title screen, screenshot and note if birds appear
   ```

3. **REPORT** findings back to `@Plan` for specific fixes

The diagnostic instrumentation (Steps 1, 3, 5) can be added if initial runs don't reveal the issue.
