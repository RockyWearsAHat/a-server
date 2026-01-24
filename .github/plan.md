# Plan: Fix Critical Black Screen Regression

**Status:** � COMPLETE
**Goal:** Fix two bugs causing all games to render black screens

---

## Root Cause Analysis (2026-01-24)

### Two Critical Bugs Found

**Bug 1: Broken IF Polling "Fix" (GBAMemory.cpp lines 1577-1591)**

The code added an aggressive "fix" for Classic NES Series that advances the PPU by up to 228 scanlines EVERY TIME the IF register is read:

```cpp
if (offset == IORegs::IF && gba && ppu) {
  // Advance PPU until VBlank (max 228 scanlines = full frame)
  for (int i = 0; i < 228 && !(dispstat & 1); i++) {
    ppu->Update(1232); // One scanline = 1232 cycles
    ...
  }
}
```

This completely breaks timing for ALL games.

**Bug 2: Redundant Read16() calls in CheckInterrupts (ARM7TDMI.cpp lines 443-444)**

After reading IE/IF via the fast path, the code OVERWRITES them by calling Read16() again:

```cpp
memory.ReadInterruptRegs(ime, ie, if_reg);  // Fast path
// ... trace code ...
ie = memory.Read16(IORegs::REG_IE);         // OVERWRITE!
if_reg = memory.Read16(IORegs::REG_IF);     // OVERWRITE → triggers Bug 1!
```

This means CheckInterrupts() (called every CPU instruction) triggers Bug 1, advancing PPU by a full frame on EVERY instruction.

---

## Steps

### Step 1: Remove catastrophic IF polling fix - src/emulator/gba/GBAMemory.cpp

**Operation:** DELETE
**Target:** Lines 1577-1591 (the IF polling block)

Remove this entire block:

```cpp
    // IF (0x202) polling fix for Classic NES Series and similar games:
    // These games busy-wait for IF VBlank flag but don't use interrupts.
    // When IF is polled and we're not in VBlank, advance PPU to VBlank.
    // This is semantically correct because the CPU is just spinning anyway.
    if (offset == IORegs::IF && gba && ppu) {
      gba->FlushPendingPeripheralCycles();
      uint16_t dispstat = ReadIORegister16Internal(IORegs::REG_DISPSTAT);
      if (!(dispstat & 1)) { // Not in VBlank
        // Advance PPU until VBlank (max 228 scanlines = full frame)
        for (int i = 0; i < 228 && !(dispstat & 1); i++) {
          ppu->Update(1232); // One scanline = 1232 cycles
          dispstat = ReadIORegister16Internal(IORegs::REG_DISPSTAT);
        }
        // Make sure any deferred VRAM/palette writes that became visible
        // during the fast-forward are applied immediately.
        ApplyDeferredWrites();
      }
    }
```

**Verify:** `make build`

---

### Step 2: Remove redundant Read16() calls - src/emulator/gba/ARM7TDMI.cpp

**Operation:** DELETE
**Target:** Lines 443-444

Remove these two lines that overwrite the fast-path values:

```cpp
  ie = memory.Read16(IORegs::REG_IE);
  if_reg = memory.Read16(IORegs::REG_IF);
```

**Verify:** `make build`

---

## Test Strategy

1. `make build` — compiles without errors
2. `./build/bin/AIOServer --headless --rom DKC.gba --headless-max-ms 2000 --headless-dump-ppm /tmp/dkc_test.ppm --headless-dump-ms 1500` — produces non-black frame
3. `python3 -c "from PIL import Image; img=Image.open('/tmp/dkc_test.ppm'); p=list(img.getdata()); n=sum(1 for x in p if sum(x)>10); print(f'{n} non-black pixels')"` — should show thousands of non-black pixels
4. Test other ROMs (SMA2, Metroid) similarly

---

## Documentation Updates

None needed - these were bugs introduced in recent commits, not architectural changes.

---

## Handoff

Run `@Implement` to execute Steps 1-2. 2. Regression test DKC: time timeout 10 ./build/bin/AIOServer --headless --rom DKC.gba --headless-max-ms 5000
Expected: Similar or better performance (~4.7s user time) 3. Performance test OG-DK: time timeout 30 ./build/bin/AIOServer --headless --rom OG-DK.gba --headless-max-ms 30000
Expected: Significantly faster than previous ~9.3s 4. Full test suite: cd build/generated/cmake && ctest --output-on-failure

---

## Documentation Updates

Add to .github/instructions/memory.md (Timing and Performance section):

### Interrupt Register Fast Path

ARM7TDMI::CheckInterrupts() uses GBAMemory::ReadInterruptRegs() to read IE/IF/IME directly from the io_regs[] array. This bypasses the full Read16() machinery (region dispatch, environment flag checks, tracer logic) and is critical for performance in tight CPU loops.

Why this is safe:

- IE/IF/IME are simple latched registers with no special read behavior
- No PPU flush is needed for interrupt status checks
- The register offsets (0x200, 0x202, 0x208) are compile-time constants

---

## Handoff

Run @Implement to execute all steps.
