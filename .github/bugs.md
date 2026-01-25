# Bugs

## DKC Logo Fade: Semi-Transparent OBJ Blending Fails When OBJ Not Selected as FirstTarget

**Status:** Open
**Date:** 2026-01-23
**Reporter:** Implement agent (@Implement)

### Summary

The new test `PPUBlendTest.SemiTransparentOBJ_NoFirstTarget` fails: semi-transparent OBJ pixels do not appear to alpha-blend with the underlying layer when BLDCNT firstTarget bits do not include OBJ (BLDCNT firstTarget=0). Per GBATEK, semi-transparent OBJs should blend regardless of OBJ being selected in firstTarget; only the underlying layer must be selected in secondTarget. The code change to remove the incorrect firstTarget gating was applied, but the test still shows non-blended pixels in the minimal reproduction.

### How to reproduce

1. Build project: `make build`
2. Run the failing test: `./build/bin/PPUTests --gtest_filter='*SemiTransparentOBJ_NoFirstTarget*'`
3. Or run full PPU tests: `./build/bin/PPUTests --gtest_filter='*SemiTransparentOBJ*'`
4. For verbose diagnostics, run with trace: `AIO_TRACE_GBA_SPAM=1 ./build/bin/PPUTests --gtest_filter='*SemiTransparentOBJ_NoFirstTarget*'`

### Observed behavior

- `SemiTransparentOBJ_NoFirstTarget pixel=0xfff80000` (no green component present)
- Trace: `[FADE_SEMITR] x=0 topLayer=4 underLayer=4 eva=8 evb=8 under=0xfff80000`

### Expected behavior

### Files/Locations

### Debugging & Attempts

### Suggested next steps

1. Inspect `RenderOBJ()` and the logic that sets `underColorBuffer` / `underLayerBuffer` to ensure the "under" pixel for an OBJ is the pixel present before OBJ draw, and that the backdrop or BG layers are recorded correctly when appropriate.
2. Add more tracing to `RenderOBJ()` to capture the per-pixel prior color/layer when writing an OBJ top pixel.
3. Consider reproducing the original DKC case in a headless run and visually diffing frames to validate end-to-end behavior.
4. If needed, involve `@Plan` to refine test setup or propose a small code fix to ensure `underLayerBuffer` isn't incorrectly set to OBJ for the pixel under a semi-transparent OBJ.

### Severity / Priority

_This bug was recorded automatically by the implement agent while executing `.github/plan.md` (Step 2). Additional debugging logs and traces have been appended to the test output if available._

## Execution log / verification attempts

```
[FADE] firstTarget=0x0 secondTarget=0x3f
[FADEPIX] x=0 scanline=0 topLayer=4 topFirst=0 color=0xfff80000
[FADE_SEMITR] x=0 topLayer=4 underLayer=4 eva=8 evb=8 under=0xfff80000
```

## Notes / Next actions

### Implement attempts (detailed)

- Re-disabled sprites a second time before setting sprite 0.
- Kept forced-blank bit set when changing DISPCNT to avoid OAM write races.
- Switched disable writes to set sprite Y=160 (off-screen) to avoid reliance on disable bit semantics.
- Added OAM initialization in `PPUBlendTest::SetUp()` so tests start with OAM disabled by default.
- Inserted assertions and enabled `AIO_TRACE_OAM_CPU_WRITES` to inspect whether OAM writes were blocked.
- OAM write logs show `forcedBlank=0` for some OAM writes (writes were blocked):
  `[INFO] [OAM] OAM WRITE CHECK region=0x07 forcedBlank=0 ...`
- `RenderOBJ()` trace shows OBJ pixels drawn before our test sprite (indicating other OAM entries still produced pixels at scanline 0).

1. Test setup timing / OAM write ordering causing other sprites to still draw at scanline 0, or
2. A subtle rendering ordering/capture issue in `RenderOBJ()` / backBuffer->underColorBuffer recording that should be investigated.

**Status:** ⚠️ Blocked — Step 1 applied but verification failed. Suggest `@Plan` refinement to propose either a small, safe change to `RenderOBJ()` to ensure the "under" pixel for an OBJ is the pre-OBJ pixel (backdrop/BG), or a test refinement that guarantees no other sprites can draw (e.g., fence OAM writes at a known-latch time).

---

## HLE BIOS IRQ trampoline: enabling IRQs in System mode causes re-entry loop

**Status:** ⚠️ Blocked
**Date:** 2026-01-24
**Reporter:** Implement agent (@Implement)

### Summary

After applying `.github/plan.md` “Fix HLE BIOS IRQ Trampoline CPSR.I Flag”, the unit test `BIOSTest.IRQReturnRestoresThumbState` fails. The CPU does not return from the BIOS IRQ trampoline to the interrupted Thumb stream.

### How to reproduce

1. Build project: `make build`
2. Run: `./build/bin/BIOSTests --gtest_filter='BIOSTest.IRQReturnRestoresThumbState'`
3. Or: `cd build/generated/cmake && ctest --output-on-failure -R BIOSTest.IRQReturnRestoresThumbState`

### Observed behavior

- Final PC remains in BIOS trampoline (`PC=0x00003F04` / 16132) after bounded stepping.

### Expected behavior

- Trampoline returns to ROM (`PC=0x08000002`) and restores Thumb state (T flag set).

### Change applied

- `src/emulator/gba/GBAMemory.cpp`: IRQ trampoline at 0x3F0C updated from `0xE3C3301F` to `0xE3C3309F` to clear CPSR.I when switching to System mode.

### Hypothesis

- IF is still pending at the moment CPSR.I is cleared (during the System-mode transition), so the CPU immediately re-enters IRQ before the trampoline reaches the user handler call.

### Attempted self-fix (1)

- `tests/BIOSTests.cpp`: installed a minimal IWRAM handler that writes `1` to IF and returns; failure persists (suggesting re-entry occurs before handler is reached).

### Suggested next steps

- Update the HLE BIOS IRQ trampoline ordering to match real BIOS behavior (acknowledge/clear IF, or otherwise prevent same-source re-entry, before unmasking IRQs in System mode), then adjust/add BIOS tests accordingly.
