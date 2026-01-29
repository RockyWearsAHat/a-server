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

- Test failure: the pixel at (0,0) is not a blended color: green component is 0 or expected blended value mismatched.
- Tracing shows BLDCNT=0x3F00 (firstTarget=0x00, secondTarget=0x3F) as expected.
- Diagnostics show `topLayer=4` and `underLayer=4` for the pixel (i.e., both top and under are OBJ), meaning the under-pixel used by the blending routine is an OBJ pixel rather than the expected BG/backdrop.
- Example test output excerpts:
  - `SemiTransparentOBJ_NoFirstTarget pixel=0xfff80000` (no green component present)
  - Trace: `[FADE_SEMITR] x=0 topLayer=4 underLayer=4 eva=8 evb=8 under=0xfff80000`

### Expected behavior

- Semi-transparent OBJ pixel (OBJ mode = 01) should alpha-blend with the underlying layer's pixel (BG0 or backdrop) when that underlying layer is selected in BLDCNT secondTarget bits, even if OBJ is not in BLDCNT firstTarget. Resulting pixel should have both red (from OBJ) and green (from BG/backdrop) components when blending coefficients are 8/8.

### Files/Locations

- `src/emulator/gba/PPU.cpp` — `ApplyColorEffects()` (semi-transparent OBJ handling)
- `tests/PPUTests.cpp` — `PPUBlendTest.SemiTransparentOBJ_NoFirstTarget` (new test)
- `.github/plan.md` — plan for the change and tests

### Debugging & Attempts

- Removed incorrect `topIsFirstTarget` gating for semi-transparent OBJs in `PPU.cpp` (Step 1) and verified the repository builds and the headless DKC frame dump was produced.
- Added `SemiTransparentOBJ_NoFirstTarget` test to `tests/PPUTests.cpp` and iterated on the test to make it deterministic (used backdrop-only minimal setup, then expanded to a windowed test mirroring existing semi-transparent test conventions).
- Added trace prints in `ApplyColorEffects()` to log `underLayer`/`under`/`eva`/`evb` for semi-transparent code path.
- Observed `underLayer` remains `4` (OBJ) in the failing minimal reproduction, indicating the rendering pipeline recorded an OBJ as the underlying pixel (possible OAM rendering order / under-layer tracking issue or test setup ordering issue).
- Multiple self-fix attempts and test adjustments were made, but the test still fails.

### Suggested next steps

1. Inspect `RenderOBJ()` and the logic that sets `underColorBuffer` / `underLayerBuffer` to ensure the "under" pixel for an OBJ is the pixel present before OBJ draw, and that the backdrop or BG layers are recorded correctly when appropriate.
2. Add more tracing to `RenderOBJ()` to capture the per-pixel prior color/layer when writing an OBJ top pixel.
3. Consider reproducing the original DKC case in a headless run and visually diffing frames to validate end-to-end behavior.
4. If needed, involve `@Plan` to refine test setup or propose a small code fix to ensure `underLayerBuffer` isn't incorrectly set to OBJ for the pixel under a semi-transparent OBJ.

### Severity / Priority

- **Severity:** Medium — causes a real regression for the DKC logo fade (visual correctness) and breaks the new test intended to guard against the regression.
- **Priority:** High — affects correctness of PPU blending behavior per GBATEK.

---

_This bug was recorded automatically by the implement agent while executing `.github/plan.md` (Step 2). Additional debugging logs and traces have been appended to the test output if available._

---

## Execution log / verification attempts

- `make build` — **succeeded** (build artifacts created in `build/bin/`).
- `./build/bin/AIOServer --headless --rom DKC.gba --headless-max-ms 3000 --headless-dump-ppm /tmp/dkc_test.ppm --headless-dump-ms 1500` — **succeeded**, produced `/tmp/dkc_test.ppm` for visual inspection.
- `./build/bin/PPUTests --gtest_filter='*SemiTransparentOBJ_NoFirstTarget*'` — **failed**; trace output indicates `underLayer == 4 (OBJ)` and `under` equals OBJ color, so blending did not mix with a BG/backdrop pixel as expected. Example trace lines:

```
[FADE] firstTarget=0x0 secondTarget=0x3f
[FADEPIX] x=0 scanline=0 topLayer=4 topFirst=0 color=0xfff80000
[FADE_SEMITR] x=0 topLayer=4 underLayer=4 eva=8 evb=8 under=0xfff80000
```

- Several self-fix attempts were made (test adjustments, debug prints, minimal reproduction using backdrop-only and OBJ-only setups) but the test still fails. See "Debugging & Attempts" for details.

---

## Notes / Next actions

- `@Plan` may need to refine test case or propose an additional small change to `RenderOBJ()` / `underLayerBuffer` logic to ensure the under-pixel recorded for an OBJ is the pre-OBJ background/backdrop pixel (not another OBJ pixel). This is required to reproduce the DKC fade correctly and to make the test deterministic.
- I (Implement agent) recorded all steps and created this bug entry per the updated implement-agent policy.

### Implement attempts (detailed)

- Applied `.github/plan.md` Step 1: added OAM initialization (disable all sprites) to `tests/PPUTests.cpp` within `SemiTransparentOBJ_NoFirstTarget` test and verified build.
- Re-ran failing test; still failed with `underLayer=4` (OBJ). See trace: `[FADE_SEMITR] x=0 topLayer=4 underLayer=4 under=0xfff80000`.
- Added extra measures (one self-fix attempt):
  - Re-disabled sprites a second time before setting sprite 0.
  - Kept forced-blank bit set when changing DISPCNT to avoid OAM write races.
  - Switched disable writes to set sprite Y=160 (off-screen) to avoid reliance on disable bit semantics.
  - Added OAM initialization in `PPUBlendTest::SetUp()` so tests start with OAM disabled by default.
  - Inserted assertions and enabled `AIO_TRACE_OAM_CPU_WRITES` to inspect whether OAM writes were blocked.
- Observed issues during verification:
  - OAM write logs show `forcedBlank=0` for some OAM writes (writes were blocked):
    `[INFO] [OAM] OAM WRITE CHECK region=0x07 forcedBlank=0 ...`
  - `RenderOBJ()` trace shows OBJ pixels drawn before our test sprite (indicating other OAM entries still produced pixels at scanline 0).
- Result: Despite the above changes, the test still fails. Root cause appears to be either:
  1. Test setup timing / OAM write ordering causing other sprites to still draw at scanline 0, or
  2. A subtle rendering ordering/capture issue in `RenderOBJ()` / backBuffer->underColorBuffer recording that should be investigated.

- NEW: LZ77 WRAM large back-reference failure (this run)
  - Test: `CPUTest.SWI_LZ77UnCompWram_LargeBackReference` (added per `.github/plan.md`).
  - Reproduction (same-machine):
    1. `./build/bin/CPUTests --gtest_filter=CPUTest.SWI_LZ77UnCompWram_LargeBackReference`
    2. Or run full suite: `cd build/generated/cmake && ctest --output-on-failure`
  - Observed: back-reference copies into WRAM produce zeroed bytes instead of reading previously-written bytes during the same decompression stream. Multiple EXPECT_EQ(...) checks for bytes copied via back-reference returned 0x00.
  - Expected: Back-reference should copy previously-written bytes (e.g., 0x12,0x34,0x56,0x78 sequence) per GBATEK; test constructs a back-reference that refers to bytes written earlier in the decompression window.
  - Likely root causes to investigate:
    - The WRAM SWI (0x11) path is not reading from the destination address when resolving back-references (reads from an uninitialized buffer or fails to make written bytes visible).
    - Memory write granularity/side-effects or an off-by-one in the "written"/offset calculation that prevents the back-reference source from being addressed correctly.
  - Files of interest: `src/emulator/gba/ARM7TDMI.cpp` (SWI LZ77 implementation), `src/emulator/gba/GBAMemory.cpp` (WRAM visibility), related LZ77 tests in `tests/CPUTests.cpp` (VRAM cases pass).
  - Temporary diagnostics already suggested: enable SWI/LZ77 tracing and re-run the single test; add a micro-repro that writes a known pattern then issues the problematic back-reference.
  - Suggested next steps (one self-fix attempt allowed):
    1. Run the failing test with tracing to capture the decompression loop (I can do this now and attach the trace).
    2. Inspect `ARM7TDMI::ExecuteSWI` WRAM branch to ensure back-reference reads `memory.Read8(dst - offset)` from the correct region and that `memory.Write8(dst, val)` updates visible state immediately.
    3. If the bug is a simple index/offset mistake or an ordering issue, apply a minimal patch, re-run the failing test, and document the fix in this bug entry.

**Status:** ⚠️ Blocked — multiple plan steps applied; one failing test (`CPUTest.SWI_LZ77UnCompWram_LargeBackReference`) requires investigation/fix before the plan can be marked complete. Suggest I (Implement) run the failing test with LZ77/SWI tracing next (recommended) or attempt one focused self-fix (also OK).
