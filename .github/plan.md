# Plan: Fix SemiTransparentOBJ_NoFirstTarget Test — OAM Initialization Bug

**Date:** 2026-01-23  
**Status:** 🔴 NOT STARTED  
**Goal:** Fix the failing `SemiTransparentOBJ_NoFirstTarget` test by initializing all OAM entries to disabled

---

## Context

### Root Cause Analysis

The test `PPUBlendTest.SemiTransparentOBJ_NoFirstTarget` fails because:

1. **Test trace output shows the problem:**

   ```
   [FADE_SEMITR] x=0 topLayer=4 underLayer=4 eva=8 evb=8 under=0xfff80000
   ```

   - `underLayer=4` (OBJ) when it should be `5` (backdrop)
   - `under=0xfff80000` (red/OBJ color) when it should be green (backdrop)

2. **OAM is not properly initialized.** The test only sets up sprite 0 but does NOT disable the other 127 sprites. The `PPUBlendTest` fixture's `SetUp()` also doesn't initialize OAM.

3. **GBA OAM default state:** When OAM contains zeros (uninitialized):
   - `attr0 = 0x0000`: Y=0, affine=0, disable=0 → **sprite ENABLED at Y=0**
   - This means sprites 127→1 all render at scanline 0 with tile 0
4. **RenderOBJ iteration order:** The loop goes `for (int i = 127; i >= 0; --i)`, so sprites 127→1 are drawn BEFORE sprite 0. When sprite 0 (the semi-transparent one) is drawn, `backBuffer` already contains pixels from sprites 127→1.

5. **The "under" layer tracking is correct:** `RenderOBJ()` correctly saves `backBuffer[pixelIndex]` to `underColorBuffer` before overwriting. But since other OBJs already drew there, the "under" layer is OBJ (layer 4), not backdrop (layer 5).

### The Fix

The test must disable all 128 sprites before setting up the test sprite. This matches what the passing test `SemiTransparentObjBlendingIsGatedByObjWindowEffectsEnableBit` does.

---

## Steps

### Step 1: Fix test by disabling all sprites before setup — `tests/PPUTests.cpp`

**Operation:** `REPLACE`
**Anchor:** Lines 2302-2377 (the entire `SemiTransparentOBJ_NoFirstTarget` test)

```cpp
TEST_F(PPUBlendTest, SemiTransparentOBJ_NoFirstTarget) {
  // Per GBATEK: Semi-transparent OBJs always blend, even when OBJ is NOT
  // selected as first target in BLDCNT. The only requirement is that the
  // underlying layer is in secondTarget.
  //
  // BLDCNT = 0x3F00: Mode 0, firstTarget=0x00 (none!), secondTarget=0x3F (all)
  // This is what DKC uses for its logo fade.

  // Enter Forced Blank to allow setup
  memory.Write16(0x04000000u, 0x0080u);

  // CRITICAL: Disable ALL 128 sprites first!
  // Uninitialized OAM (attr0=0) means sprites are enabled at Y=0.
  // The RenderOBJ loop iterates 127→0, so any enabled sprite will draw
  // before our test sprite and pollute the "under" layer tracking.
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    // attr0 bit 9 = 1 (with affine=0) disables the sprite
    TestUtil::WriteOam16(memory, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(memory, base + 2u, 0u);
    TestUtil::WriteOam16(memory, base + 4u, 0u);
  }

  // BLDCNT: Mode 0, firstTarget=0x00 (none), secondTarget=0x3F (all)
  memory.Write16(0x04000050u, 0x3F00u);
  // BLDALPHA: EVA=8, EVB=8 (50/50 blend)
  memory.Write16(0x04000052u, 0x0808u);

  // Mode 0 + OBJ enable (no BGs, so backdrop is the only under-layer)
  memory.Write16(0x04000000u, 0x1000u);

  // Backdrop = green (BGR555: 0x03E0)
  memory.Write16(0x05000000u, 0x03E0u);

  // OBJ palette index 1 = red (BGR555: 0x001F)
  memory.Write16(0x05000200u + 2u, 0x001Fu);

  // OBJ tile 0: fill row 0 with palette index 1 (4bpp: each byte = 0x11)
  memory.Write16(0x06010000u + 0u, 0x1111u);
  memory.Write16(0x06010000u + 2u, 0x1111u);

  // Setup sprite 0: semi-transparent OBJ at (0,0), 8x8, 4bpp
  // attr0: Y=0, objMode=1 (semi-transparent), bits 10-11 = 01
  const uint16_t spr0_attr0 = (uint16_t)(0u | (1u << 10));
  const uint16_t spr0_attr1 = 0u;
  const uint16_t spr0_attr2 = 0u;
  TestUtil::WriteOam16(memory, 0, spr0_attr0);
  TestUtil::WriteOam16(memory, 2, spr0_attr1);
  TestUtil::WriteOam16(memory, 4, spr0_attr2);

  // Exit Forced Blank before rendering
  memory.Write16(0x04000000u, 0x1000u);

  ppu.Update(960);
  ppu.SwapBuffers();

  const auto fb = ppu.GetFramebuffer();
  ASSERT_GE(fb.size(), (size_t)PPU::SCREEN_WIDTH);

  uint32_t pixel = fb[0];
  uint8_t r = (pixel >> 16) & 0xFF;
  uint8_t g = (pixel >> 8) & 0xFF;
  uint8_t b = pixel & 0xFF;

  // Expected: blended color from red OBJ + green backdrop
  // With EVA=8, EVB=8: out = (OBJ*8 + backdrop*8) / 16
  // Red OBJ:    R=31, G=0,  B=0  (BGR555: 0x001F -> RGB888: 0xF8,0,0)
  // Green BG:   R=0,  G=31, B=0  (BGR555: 0x03E0 -> RGB888: 0,0xF8,0)
  // Blended 5-bit: R=15, G=15, B=0 -> RGB888: 0x78, 0x78, 0
  EXPECT_GT(r, 0u) << "Red component should be present (from OBJ)";
  EXPECT_GT(g, 0u) << "Green component should be present (from backdrop blend)";
  EXPECT_EQ(b, 0u) << "Blue component should be zero";
}
```

**Verify:** `./build/bin/PPUTests --gtest_filter='*SemiTransparentOBJ_NoFirstTarget*'`

---

## Test Strategy

1. `make build` — compiles without errors
2. `./build/bin/PPUTests --gtest_filter='*SemiTransparentOBJ_NoFirstTarget*'` — test passes
3. `./build/bin/PPUTests --gtest_filter='*SemiTransparent*'` — all semi-transparent tests still pass

---

## Documentation Updates

No memory.md updates needed — this was a test setup bug, not a code bug.

---

## Notes

The PPU code change from the previous plan (removing `topIsFirstTarget` check for semi-transparent OBJs) was already applied and is correct. The test was failing due to improper OAM initialization, not a PPU logic bug.

---

## Handoff

Run `@Implement` to execute Step 1.
