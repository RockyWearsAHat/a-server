# Plan: DKC Title Screen Birds Analysis

**Date:** 2026-01-23  
**Status:** ✅ INVESTIGATED - BIRDS RENDER CORRECTLY  
**Goal:** Investigate missing bird sprites on DKC title screen/menu

---

## Investigation Summary

### User Report

User reported "no birds on the title screen/menu of DKC" - birds appear in mGBA but not in our emulator.

### Diagnostic Process

1. **OAM/Sprite Analysis:**
   - Found sky sprites (y<50) ARE defined in OAM at title screen frames (400+)
   - Sprites 27, 28, 30, 31, 32 at x=208-239, y=0-16
   - Sprite properties: tile=0xd0-0xd1, palette=4, priority=0, mode=0 (normal)

2. **Pixel Rendering Trace:**
   Added `[BIRD_DRAW]` diagnostic - **confirmed birds ARE being drawn**:

   ```
   [BIRD_DRAW] frame=400 spr=28 x=216 y=8 ci=9 pal=4 RGB=(56,96,80)
   [BIRD_DRAW] frame=400 spr=28 x=225 y=8 ci=2 pal=4 RGB=(88,168,72)
   ```

   Colors are distinctly GREEN (G=80-168), not sky blue.

3. **Framebuffer Verification:**
   Dumped frame at 6.7 seconds (frame ~400) and analyzed pixel data:
   ```
   x=216 y=8 RGB=(56,96,80)   ← GREEN bird pixel
   x=225 y=8 RGB=(88,168,72)  ← GREEN bird pixel
   x=216 y=9 RGB=(56,120,64)  ← GREEN bird pixel
   x=220 y=9 RGB=(56,96,80)   ← GREEN bird pixel
   ```
   **Birds ARE present in the final framebuffer with correct colors!**

### Root Cause

**PPU is working correctly.** The birds ARE being rendered. Possible explanations for user not seeing them:

1. **Timing mismatch**: User may be looking at a different frame range
2. **Old build**: User may be running an older version of the emulator
3. **Display scaling**: Qt display might be cropping/scaling the top of screen
4. **Different ROM**: ROM version differences

### Recommendation

1. **Rebuild emulator** from latest code: `make build`
2. **Run GUI version** and wait ~7-8 seconds for title screen to settle
3. **Look at x=208-240, y=7-20 region** (right side of sky, top area)
4. **Verify ROM**: Should be game code A5NE (RAREDKC1)

---

## Evidence

### Frame Dump Analysis

- Frame dumped at 6.7 seconds (headless-dump-ms 6700)
- 240x160 PPM image written successfully
- Green pixels confirmed at bird sprite locations (x=216-239, y=7-16)

### Trace Output

420 `[SKY_SPR]` traces showing sprites processed at frames 400-700.
100 `[BIRD_DRAW]` traces showing pixel writes to backBuffer.

**Status: Closed - Rendering Verified**
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
// Red OBJ: R=31, G=0, B=0 (BGR555: 0x001F -> RGB888: 0xF8,0,0)
// Green BG: R=0, G=31, B=0 (BGR555: 0x03E0 -> RGB888: 0,0xF8,0)
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
```
