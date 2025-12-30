#include <gtest/gtest.h>

#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/PPU.h"
#include "support/PPUTestHelper.h"

using namespace AIO::Emulator::GBA;
namespace TestUtil = AIO::Emulator::GBA::Test;

namespace {
constexpr uint32_t kBgPaletteBase = 0x05000000u;
constexpr uint32_t kObjPaletteBase = 0x05000200u;

constexpr uint32_t kBgScreenBase0 = 0x06000000u;           // screenBase=0
constexpr uint32_t kBgScreenBase1 = 0x06000000u + 0x0800u; // screenBase=1

constexpr uint32_t kBgCharBase1 = 0x06004000u; // charBase=1
constexpr uint32_t kBgCharBase2 = 0x06008000u; // charBase=2

constexpr uint32_t kObjVramBase = 0x06010000u;

void FillBgRow0WithTile1(GBAMemory &mem, uint32_t screenBase) {
  for (uint32_t tx = 0; tx < 32; ++tx) {
    mem.Write16(screenBase + tx * 2u, 0x0001u);
  }
}

void FillTile1Row0WithPaletteIndex1_4bpp(GBAMemory &mem, uint32_t charBase) {
  // Tile 1, row 0 => pixels 0..7 all = 1.
  mem.Write16(charBase + 1u * 32u + 0u, 0x1111u);
  mem.Write16(charBase + 1u * 32u + 2u, 0x1111u);
}

void FillBgAllWithTile1(GBAMemory &mem, uint32_t screenBase) {
  for (uint32_t ty = 0; ty < 32; ++ty) {
    for (uint32_t tx = 0; tx < 32; ++tx) {
      mem.Write16(screenBase + (ty * 32u + tx) * 2u, 0x0001u);
    }
  }
}

void FillTile1AllRowsWithPaletteIndex1_4bpp(GBAMemory &mem, uint32_t charBase,
                                            uint8_t paletteIndex) {
  const uint32_t tileBase = charBase + 1u * 32u;
  const uint16_t packed =
      (uint16_t)(paletteIndex | (paletteIndex << 4) | (paletteIndex << 8) |
                 (paletteIndex << 12));
  for (uint32_t row = 0; row < 8; ++row) {
    mem.Write16(tileBase + row * 4u + 0u, packed);
    mem.Write16(tileBase + row * 4u + 2u, packed);
  }
}

uint16_t BgCnt(uint16_t priority, uint16_t charBase, uint16_t screenBase) {
  // Bits: 0-1 priority, 2-3 char base, 8-12 screen base.
  return (uint16_t)((priority & 3u) | ((charBase & 3u) << 2) |
                    ((screenBase & 31u) << 8));
}

} // namespace

TEST(BlendTest, AlphaBlend_EvaClampsTo16) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Mode 0, BG0+BG1 enable, Forced Blank (allows setup writes).
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x0080u));

  // BG0: priority 0, charBase=1, screenBase=0.
  // BG1: priority 1, charBase=2, screenBase=1.
  mem.Write16(0x04000008u, BgCnt(0, 1, 0));
  mem.Write16(0x0400000Au, BgCnt(1, 2, 1));

  // BG palettes: BG0 idx1 = red level 10, BG1 idx1 = black.
  mem.Write16(kBgPaletteBase + 2u, 0x000Au);
  mem.Write16(kBgPaletteBase + 4u, 0x0000u);

  FillBgRow0WithTile1(mem, kBgScreenBase0);
  FillBgRow0WithTile1(mem, kBgScreenBase1);
  FillTile1Row0WithPaletteIndex1_4bpp(mem, kBgCharBase1);
  FillTile1Row0WithPaletteIndex1_4bpp(mem, kBgCharBase2);

  // BLDCNT: effect=alpha (mode 1), first target = BG0, second target = BG1.
  mem.Write16(0x04000050u, (uint16_t)(0x0040u | 0x0001u | 0x0200u));
  // EVA=31 (must clamp to 16), EVB=0.
  mem.Write16(0x04000052u, 0x001Fu);

  // Exit Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u));

  ppu.Update(960);
  ppu.SwapBuffers();

  // With EVA clamped to 16 and EVB=0, output must equal BG0 (red level 10).
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x000Au));
}

TEST(BlendTest, AlphaBlend_EvbClampsTo16) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Mode 0, BG0+BG1 enable, Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x0080u));

  // BG0 top: black (palette idx1). BG1 under: red level 10 (palette idx2).
  mem.Write16(0x04000008u, BgCnt(0, 1, 0));
  mem.Write16(0x0400000Au, BgCnt(1, 2, 1));
  mem.Write16(kBgPaletteBase + 2u, 0x0000u); // idx1 = black
  mem.Write16(kBgPaletteBase + 4u, 0x000Au); // idx2 = red level 10

  FillBgRow0WithTile1(mem, kBgScreenBase0);
  FillBgRow0WithTile1(mem, kBgScreenBase1);
  FillTile1Row0WithPaletteIndex1_4bpp(mem, kBgCharBase1);
  // For BG1, use palette index 2 so it can differ from BG0.
  mem.Write16(kBgCharBase2 + 1u * 32u + 0u, 0x2222u);
  mem.Write16(kBgCharBase2 + 1u * 32u + 2u, 0x2222u);

  // BLDCNT: alpha, first target = BG0, second target = BG1.
  mem.Write16(0x04000050u, (uint16_t)(0x0040u | 0x0001u | 0x0200u));
  // EVA=0, EVB=31 (must clamp to 16).
  mem.Write16(0x04000052u, 0x1F00u);

  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u));

  ppu.Update(960);
  ppu.SwapBuffers();

  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x000Au));
}

TEST(BlendTest, AlphaBlend_TargetGating_OnlyAppliesWhenTopIsFirstTarget) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Mode 0, BG0+BG1 enable, Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x0080u));

  // BG0 top: red (idx1). BG1 under: blue (idx2).
  mem.Write16(0x04000008u, BgCnt(0, 1, 0));
  mem.Write16(0x0400000Au, BgCnt(1, 2, 1));
  mem.Write16(kBgPaletteBase + 2u, 0x001Fu); // idx1 = red
  mem.Write16(kBgPaletteBase + 4u, 0x7C00u); // idx2 = blue

  FillBgRow0WithTile1(mem, kBgScreenBase0);
  FillBgRow0WithTile1(mem, kBgScreenBase1);
  FillTile1Row0WithPaletteIndex1_4bpp(mem, kBgCharBase1);
  // For BG1, use palette index 2 so it can differ from BG0.
  mem.Write16(kBgCharBase2 + 1u * 32u + 0u, 0x2222u);
  mem.Write16(kBgCharBase2 + 1u * 32u + 2u, 0x2222u);

  // BLDCNT: alpha, first target = BG1 only, second target = BG0 only.
  // Top pixel is BG0, so blending must NOT apply.
  mem.Write16(0x04000050u, (uint16_t)(0x0040u | 0x0002u | 0x0100u));
  mem.Write16(0x04000052u, 0x0808u);

  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u));

  ppu.Update(960);
  ppu.SwapBuffers();

  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x001Fu));
}

TEST(BlendTest, AlphaBlend_UsesTruncation_NotRounding_Eva1) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Mode 0, BG0+BG1 enable, Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x0080u));

  // BG0 top: red=31 (idx1). BG1 under: black (idx1).
  mem.Write16(0x04000008u, BgCnt(0, 1, 0));
  mem.Write16(0x0400000Au, BgCnt(1, 2, 1));
  mem.Write16(kBgPaletteBase + 2u, 0x001Fu);
  mem.Write16(kBgPaletteBase + 4u, 0x0000u);

  FillBgRow0WithTile1(mem, kBgScreenBase0);
  FillBgRow0WithTile1(mem, kBgScreenBase1);
  FillTile1Row0WithPaletteIndex1_4bpp(mem, kBgCharBase1);
  FillTile1Row0WithPaletteIndex1_4bpp(mem, kBgCharBase2);

  // BLDCNT: alpha, first target = BG0, second target = BG1.
  mem.Write16(0x04000050u, (uint16_t)(0x0040u | 0x0001u | 0x0200u));
  // EVA=1, EVB=0. Output = floor(31*1/16)=1 (not 2).
  mem.Write16(0x04000052u, 0x0001u);

  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u));

  ppu.Update(960);
  ppu.SwapBuffers();

  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x0001u));
}

TEST(BlendTest, AlphaBlend_UsesTruncation_NotRounding_Eva8) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Mode 0, BG0+BG1 enable, Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x0080u));

  // BG0 top: red=31 (idx1). BG1 under: black (idx1).
  mem.Write16(0x04000008u, BgCnt(0, 1, 0));
  mem.Write16(0x0400000Au, BgCnt(1, 2, 1));
  mem.Write16(kBgPaletteBase + 2u, 0x001Fu);
  mem.Write16(kBgPaletteBase + 4u, 0x0000u);

  FillBgRow0WithTile1(mem, kBgScreenBase0);
  FillBgRow0WithTile1(mem, kBgScreenBase1);
  FillTile1Row0WithPaletteIndex1_4bpp(mem, kBgCharBase1);
  FillTile1Row0WithPaletteIndex1_4bpp(mem, kBgCharBase2);

  mem.Write16(0x04000050u, (uint16_t)(0x0040u | 0x0001u | 0x0200u));
  // EVA=8, EVB=0. Output = floor(31*8/16)=15 (not 16).
  mem.Write16(0x04000052u, 0x0008u);

  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u));

  ppu.Update(960);
  ppu.SwapBuffers();

  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x000Fu));
}

TEST(BlendTest, SemiTransparentObj_ForcesAlphaBlendEvenWhenEffectModeIsNone) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Allow setup writes.
  mem.Write16(0x04000000u, 0x0080u);

  // Disable all sprites by default to avoid incidental OAM garbage.
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // Mode 0, BG0 enable, OBJ enable.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x1000u | 0x0080u));

  // BG0: priority 0, charBase=1, screenBase=0.
  mem.Write16(0x04000008u, BgCnt(0, 1, 0));

  // BG palette idx1 = red; OBJ palette idx1 = blue.
  mem.Write16(kBgPaletteBase + 2u, 0x001Fu);
  mem.Write16(kObjPaletteBase + 2u, 0x7C00u);

  FillBgRow0WithTile1(mem, kBgScreenBase0);
  FillTile1Row0WithPaletteIndex1_4bpp(mem, kBgCharBase1);

  // BLDCNT: effectMode=0 (none), but targets allow OBJ (1st) over BG0 (2nd).
  mem.Write16(0x04000050u, (uint16_t)(0x0010u | 0x0100u));
  mem.Write16(0x04000052u, 0x0808u); // EVA=8, EVB=8

  // Sprite 0: semi-transparent OBJ at (0,0), 8x8, 4bpp, tileIndex=0.
  const uint16_t attr0 = (uint16_t)(0u | (1u << 10)); // objMode=1
  const uint16_t attr1 = 0u;
  const uint16_t attr2 = 0u;
  TestUtil::WriteOam16(mem, 0, attr0);
  TestUtil::WriteOam16(mem, 2, attr1);
  TestUtil::WriteOam16(mem, 4, attr2);

  // OBJ tile 0, row 0: pixels all = 1 (blue).
  mem.Write16(kObjVramBase + 0u, 0x1111u);
  mem.Write16(kObjVramBase + 2u, 0x1111u);

  // Exit Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x1000u));

  ppu.Update(960);
  ppu.SwapBuffers();

  // Average(blue, red) with EVA=EVB=8 => purple (0x3C0F).
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x3C0Fu));
}

TEST(BlendTest, SemiTransparentObj_DoesNotBlend_WhenObjNotFirstTarget) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Allow setup writes.
  mem.Write16(0x04000000u, 0x0080u);

  // Disable all sprites by default.
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // Mode 0, BG0 enable, OBJ enable.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x1000u | 0x0080u));
  mem.Write16(0x04000008u, BgCnt(0, 1, 0));

  // BG palette idx1 = red; OBJ palette idx1 = blue.
  mem.Write16(kBgPaletteBase + 2u, 0x001Fu);
  mem.Write16(kObjPaletteBase + 2u, 0x7C00u);

  FillBgRow0WithTile1(mem, kBgScreenBase0);
  FillTile1Row0WithPaletteIndex1_4bpp(mem, kBgCharBase1);

  // Effect mode doesn't matter for semi-transparent OBJ, but target bits do.
  // First target excludes OBJ (only BG0), second target includes BG0.
  mem.Write16(0x04000050u, (uint16_t)(0x0001u | 0x0100u));
  mem.Write16(0x04000052u, 0x0808u);

  // Sprite 0: semi-transparent OBJ at (0,0), 8x8, 4bpp, tileIndex=0.
  TestUtil::WriteOam16(mem, 0, (uint16_t)(0u | (1u << 10)));
  TestUtil::WriteOam16(mem, 2, 0u);
  TestUtil::WriteOam16(mem, 4, 0u);

  // OBJ tile 0, row 0: pixels all = 1 (blue).
  mem.Write16(kObjVramBase + 0u, 0x1111u);
  mem.Write16(kObjVramBase + 2u, 0x1111u);

  // Exit Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x1000u));

  ppu.Update(960);
  ppu.SwapBuffers();

  // With OBJ not selected as first target, no blending occurs => pure blue.
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x7C00u));
}

TEST(BlendTest, SemiTransparentObj_DoesNotBlend_WhenUnderNotSecondTarget) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  mem.Write16(0x04000000u, 0x0080u);

  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x1000u | 0x0080u));
  mem.Write16(0x04000008u, BgCnt(0, 1, 0));

  mem.Write16(kBgPaletteBase + 2u, 0x001Fu);
  mem.Write16(kObjPaletteBase + 2u, 0x7C00u);

  FillBgRow0WithTile1(mem, kBgScreenBase0);
  FillTile1Row0WithPaletteIndex1_4bpp(mem, kBgCharBase1);

  // First target includes OBJ, but second target excludes BG0.
  // (Second target mask = 0.)
  mem.Write16(0x04000050u, (uint16_t)(0x0010u));
  mem.Write16(0x04000052u, 0x0808u);

  TestUtil::WriteOam16(mem, 0, (uint16_t)(0u | (1u << 10)));
  TestUtil::WriteOam16(mem, 2, 0u);
  TestUtil::WriteOam16(mem, 4, 0u);
  mem.Write16(kObjVramBase + 0u, 0x1111u);
  mem.Write16(kObjVramBase + 2u, 0x1111u);

  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x1000u));

  ppu.Update(960);
  ppu.SwapBuffers();

  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x7C00u));
}

TEST(BlendTest,
     SemiTransparentObj_CanBlendWithBackdrop_WhenBackdropIsSecondTarget) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  mem.Write16(0x04000000u, 0x0080u);

  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // OBJ enable only; backdrop is black.
  mem.Write16(0x04000000u, (uint16_t)(0x1000u | 0x0080u));

  // OBJ palette idx1 = blue.
  mem.Write16(kObjPaletteBase + 2u, 0x7C00u);

  // BLDCNT: select OBJ as first target, backdrop as second target.
  // effectMode can be 0 for semi-transparent OBJ blending path.
  mem.Write16(0x04000050u, (uint16_t)(0x0010u | (0x0020u << 8)));
  mem.Write16(0x04000052u, 0x0808u); // EVA=8, EVB=8

  // Sprite 0: semi-transparent at (0,0).
  TestUtil::WriteOam16(mem, 0, (uint16_t)(0u | (1u << 10)));
  TestUtil::WriteOam16(mem, 2, 0u);
  TestUtil::WriteOam16(mem, 4, 0u);
  mem.Write16(kObjVramBase + 0u, 0x1111u);
  mem.Write16(kObjVramBase + 2u, 0x1111u);

  mem.Write16(0x04000000u, 0x1000u);

  ppu.Update(960);
  ppu.SwapBuffers();

  // Blue blended with black at 50% => blue=15 => 0x3C00.
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x3C00u));
}

TEST(BlendTest, AlphaBlend_DisabledWhenWindowDisablesColorEffects) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Mode 0, BG0+BG1 enable, WIN0 enable, Forced Blank (setup writes).
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x2000u | 0x0080u));

  mem.Write16(0x04000008u, BgCnt(0, 1, 0));
  mem.Write16(0x0400000Au, BgCnt(1, 2, 1));

  // BG0 idx1 = red, BG1 idx2 = blue.
  mem.Write16(kBgPaletteBase + 2u, 0x001Fu);
  mem.Write16(kBgPaletteBase + 4u, 0x7C00u);

  FillBgRow0WithTile1(mem, kBgScreenBase0);
  FillBgRow0WithTile1(mem, kBgScreenBase1);
  FillTile1Row0WithPaletteIndex1_4bpp(mem, kBgCharBase1);
  // BG1 uses palette index 2.
  mem.Write16(kBgCharBase2 + 1u * 32u + 0u, 0x2222u);
  mem.Write16(kBgCharBase2 + 1u * 32u + 2u, 0x2222u);

  // Window 0 covers whole screen.
  mem.Write16(0x04000040u, (uint16_t)((0u << 8) | 240u));
  mem.Write16(0x04000044u, (uint16_t)((0u << 8) | 160u));
  // WININ: inside WIN0 enable BG0+BG1 but DISABLE color effects (bit5=0).
  mem.Write16(0x04000048u, (uint16_t)(0x0001u | 0x0002u));
  // WINOUT: outside window (not used here).
  mem.Write16(0x0400004Au, 0u);

  // BLDCNT: alpha, first target BG0, second target BG1.
  mem.Write16(0x04000050u, (uint16_t)(0x0040u | 0x0001u | 0x0200u));
  mem.Write16(0x04000052u, 0x0808u);

  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x2000u));

  ppu.Update(960);
  ppu.SwapBuffers();

  // With color effects disabled by WIN0, output must remain BG0 red.
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x001Fu));
}

TEST(BlendTest, SemiTransparentObj_DoesNotBlendWhenWindowDisablesColorEffects) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  mem.Write16(0x04000000u, 0x0080u);

  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // Mode 0, BG0 enable, OBJ enable, WIN0 enable, Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x1000u | 0x2000u | 0x0080u));
  mem.Write16(0x04000008u, BgCnt(0, 1, 0));

  // BG palette idx1 = red; OBJ palette idx1 = blue.
  mem.Write16(kBgPaletteBase + 2u, 0x001Fu);
  mem.Write16(kObjPaletteBase + 2u, 0x7C00u);

  FillBgRow0WithTile1(mem, kBgScreenBase0);
  FillTile1Row0WithPaletteIndex1_4bpp(mem, kBgCharBase1);

  // Window 0 covers whole screen; disable effects.
  mem.Write16(0x04000040u, (uint16_t)((0u << 8) | 240u));
  mem.Write16(0x04000044u, (uint16_t)((0u << 8) | 160u));
  mem.Write16(0x04000048u, (uint16_t)(0x0001u | 0x0010u));
  mem.Write16(0x0400004Au, 0u);

  // Targets would normally allow blending.
  mem.Write16(0x04000050u, (uint16_t)(0x0010u | 0x0100u));
  mem.Write16(0x04000052u, 0x0808u);

  TestUtil::WriteOam16(mem, 0, (uint16_t)(0u | (1u << 10)));
  TestUtil::WriteOam16(mem, 2, 0u);
  TestUtil::WriteOam16(mem, 4, 0u);
  mem.Write16(kObjVramBase + 0u, 0x1111u);
  mem.Write16(kObjVramBase + 2u, 0x1111u);

  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x1000u | 0x2000u));

  ppu.Update(960);
  ppu.SwapBuffers();

  // Effects disabled => no blending => pure blue.
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x7C00u));
}

TEST(BlendTest, BrightnessIncrease_EvyClampsTo16) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Mode 0, BG0 enable, Forced Blank (setup writes).
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0080u));

  // BG0: priority 0, charBase=1, screenBase=0.
  mem.Write16(0x04000008u, BgCnt(0, 1, 0));

  // BG0 palette idx1 = black.
  mem.Write16(kBgPaletteBase + 2u, 0x0000u);
  FillBgAllWithTile1(mem, kBgScreenBase0);
  FillTile1AllRowsWithPaletteIndex1_4bpp(mem, kBgCharBase1, 1u);

  // GBATEK: EVY is 0..16; values 17..31 are treated as 16.
  // BLDCNT: brightness increase (mode 2), first target = BG0.
  mem.Write16(0x04000050u, (uint16_t)((2u << 6) | 0x0001u));
  mem.Write16(0x04000054u, 0x001Fu); // EVY=31 -> clamp to 16.

  // Exit Forced Blank.
  mem.Write16(0x04000000u, 0x0100u);

  ppu.Update(960);
  ppu.SwapBuffers();

  // Black with brightness increase EVY=16 => white.
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x7FFFu));
}

TEST(BlendTest, BrightnessDecrease_EvyClampsTo16) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Mode 0, BG0 enable, Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0080u));
  mem.Write16(0x04000008u, BgCnt(0, 1, 0));

  // BG0 palette idx1 = white.
  mem.Write16(kBgPaletteBase + 2u, 0x7FFFu);
  FillBgAllWithTile1(mem, kBgScreenBase0);
  FillTile1AllRowsWithPaletteIndex1_4bpp(mem, kBgCharBase1, 1u);

  // BLDCNT: brightness decrease (mode 3), first target = BG0.
  mem.Write16(0x04000050u, (uint16_t)((3u << 6) | 0x0001u));
  mem.Write16(0x04000054u, 0x001Fu); // EVY=31 -> clamp to 16.

  mem.Write16(0x04000000u, 0x0100u);

  ppu.Update(960);
  ppu.SwapBuffers();

  // White with brightness decrease EVY=16 => black.
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x0000u));
}

TEST(BlendTest,
     BrightnessIncrease_TargetGating_OnlyAppliesWhenTopIsFirstTarget) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Mode 0, BG0 enable, Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0080u));
  mem.Write16(0x04000008u, BgCnt(0, 1, 0));

  // BG0 palette idx1 = black.
  mem.Write16(kBgPaletteBase + 2u, 0x0000u);
  FillBgAllWithTile1(mem, kBgScreenBase0);
  FillTile1AllRowsWithPaletteIndex1_4bpp(mem, kBgCharBase1, 1u);

  // Brightness increase, but first target is BG1 (not BG0), so effect must NOT
  // apply.
  mem.Write16(0x04000050u, (uint16_t)((2u << 6) | 0x0002u));
  mem.Write16(0x04000054u, 0x0010u);

  mem.Write16(0x04000000u, 0x0100u);

  ppu.Update(960);
  ppu.SwapBuffers();

  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x0000u));
}
