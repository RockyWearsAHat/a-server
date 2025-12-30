#include <gtest/gtest.h>

#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/PPU.h"
#include "support/PPUTestHelper.h"

using namespace AIO::Emulator::GBA;
namespace TestUtil = AIO::Emulator::GBA::Test;

namespace {
void FillBgRow0(GBAMemory &mem, uint32_t screenBaseAddr, uint16_t tileIndex) {
  for (uint32_t tx = 0; tx < 32; ++tx) {
    mem.Write16(screenBaseAddr + tx * 2u, tileIndex);
  }
}

void FillBgAllWithTile(GBAMemory &mem, uint32_t screenBaseAddr,
                       uint16_t tileIndex) {
  for (uint32_t ty = 0; ty < 32; ++ty) {
    for (uint32_t tx = 0; tx < 32; ++tx) {
      mem.Write16(screenBaseAddr + (ty * 32u + tx) * 2u, tileIndex);
    }
  }
}

void FillTileRow0_AllPixels(GBAMemory &mem, uint32_t charBaseAddr,
                            uint16_t tileIndex, uint8_t paletteIndex) {
  const uint32_t tileAddr = charBaseAddr + (uint32_t)tileIndex * 32u;
  const uint16_t packed =
      (uint16_t)(paletteIndex | (paletteIndex << 4) | (paletteIndex << 8) |
                 (paletteIndex << 12));
  mem.Write16(tileAddr + 0u, packed);
  mem.Write16(tileAddr + 2u, packed);
}

void FillTileAllRows_AllPixels(GBAMemory &mem, uint32_t charBaseAddr,
                               uint16_t tileIndex, uint8_t paletteIndex) {
  const uint32_t tileAddr = charBaseAddr + (uint32_t)tileIndex * 32u;
  const uint16_t packed =
      (uint16_t)(paletteIndex | (paletteIndex << 4) | (paletteIndex << 8) |
                 (paletteIndex << 12));
  for (uint32_t row = 0; row < 8; ++row) {
    mem.Write16(tileAddr + row * 4u + 0u, packed);
    mem.Write16(tileAddr + row * 4u + 2u, packed);
  }
}

uint16_t BgCnt(uint16_t priority, uint16_t charBase, uint16_t screenBase) {
  return (uint16_t)((priority & 3u) | ((charBase & 3u) << 2) |
                    ((screenBase & 31u) << 8));
}

uint32_t SamplePixelAtScanline(GBAMemory &mem, int scanline, int x) {
  PPU ppu(mem);
  TestUtil::RenderToScanlineHBlank(ppu, scanline);
  return TestUtil::GetPixel(ppu, x, scanline);
}
} // namespace

TEST(WindowTest, WIN0_Wraparound_X1GreaterThanX2) {
  GBAMemory mem;
  mem.Reset();

  // Mode 0, BG0+BG1 enable, WIN0 enable, Forced Blank for setup.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x2000u | 0x0080u));

  // BG0: prio 0, charBase=1, screenBase=0
  // BG1: prio 1, charBase=2, screenBase=1
  mem.Write16(0x04000008u, BgCnt(0, 1, 0));
  mem.Write16(0x0400000Au, BgCnt(1, 2, 1));

  // WIN0H: X1=200, X2=40 -> wraparound window.
  mem.Write16(0x04000040u, (uint16_t)((200u << 8) | 40u));
  // WIN0V: full height.
  mem.Write16(0x04000044u, (uint16_t)((0u << 8) | 160u));

  // WININ: inside WIN0 show BG0 only. WINOUT: outside show BG1 only.
  mem.Write16(0x04000048u, (uint16_t)((0x0001u) | (0x0000u << 8)));
  mem.Write16(0x0400004Au, 0x0002u);

  // BG palette idx1=red, idx2=green.
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x03E0u);

  // BG0 uses palette idx1 in tile 1; BG1 uses palette idx2 in tile 1.
  FillBgAllWithTile(mem, 0x06000000u, 1u);
  FillBgAllWithTile(mem, 0x06000000u + 0x0800u, 1u);
  FillTileAllRows_AllPixels(mem, 0x06004000u, 1u, 1u);
  FillTileAllRows_AllPixels(mem, 0x06008000u, 1u, 2u);

  // Exit Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x2000u));

  // Wraparound means inside WIN0 when x >= 200 OR x < 40.
  EXPECT_EQ(SamplePixelAtScanline(mem, 0, 10),
            TestUtil::ARGBFromBGR555(0x001F));
  EXPECT_EQ(SamplePixelAtScanline(mem, 0, 100),
            TestUtil::ARGBFromBGR555(0x03E0));
  EXPECT_EQ(SamplePixelAtScanline(mem, 0, 210),
            TestUtil::ARGBFromBGR555(0x001F));
}

TEST(WindowTest, WIN0_Wraparound_Y1GreaterThanY2) {
  GBAMemory mem;
  mem.Reset();

  // Mode 0, BG0+BG1 enable, WIN0 enable, Forced Blank for setup.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x2000u | 0x0080u));

  mem.Write16(0x04000008u, BgCnt(0, 1, 0));
  mem.Write16(0x0400000Au, BgCnt(1, 2, 1));

  // WIN0H: full width.
  mem.Write16(0x04000040u, (uint16_t)((0u << 8) | 240u));
  // WIN0V: Y1=120, Y2=40 -> wraparound window.
  mem.Write16(0x04000044u, (uint16_t)((120u << 8) | 40u));

  // Inside WIN0 show BG0 only; outside show BG1 only.
  mem.Write16(0x04000048u, (uint16_t)(0x0001u));
  mem.Write16(0x0400004Au, 0x0002u);

  // BG palette idx1=red, idx2=green.
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x03E0u);

  // Fill the whole screenblock and all tile rows so any scanline samples a
  // stable color.
  FillBgAllWithTile(mem, 0x06000000u, 1u);
  FillBgAllWithTile(mem, 0x06000000u + 0x0800u, 1u);
  FillTileAllRows_AllPixels(mem, 0x06004000u, 1u, 1u);
  FillTileAllRows_AllPixels(mem, 0x06008000u, 1u, 2u);

  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x2000u));

  // Wraparound means inside WIN0 when y >= 120 OR y < 40.
  EXPECT_EQ(SamplePixelAtScanline(mem, 10, 0),
            TestUtil::ARGBFromBGR555(0x001F));
  EXPECT_EQ(SamplePixelAtScanline(mem, 80, 0),
            TestUtil::ARGBFromBGR555(0x03E0));
  EXPECT_EQ(SamplePixelAtScanline(mem, 140, 0),
            TestUtil::ARGBFromBGR555(0x001F));
}

TEST(WindowTest, WIN1_Wraparound_X1GreaterThanX2) {
  GBAMemory mem;
  mem.Reset();

  // Mode 0, BG0+BG1 enable, WIN1 enable, Forced Blank for setup.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x4000u | 0x0080u));

  // BG0: prio 0, charBase=1, screenBase=0
  // BG1: prio 1, charBase=2, screenBase=1
  mem.Write16(0x04000008u, BgCnt(0, 1, 0));
  mem.Write16(0x0400000Au, BgCnt(1, 2, 1));

  // WIN1H: X1=200, X2=40 -> wraparound window.
  mem.Write16(0x04000042u, (uint16_t)((200u << 8) | 40u));
  // WIN1V: full height.
  mem.Write16(0x04000046u, (uint16_t)((0u << 8) | 160u));

  // WININ: inside WIN1 show BG0 only. WINOUT: outside show BG1 only.
  mem.Write16(0x04000048u, (uint16_t)(0x0000u | (0x0001u << 8)));
  mem.Write16(0x0400004Au, 0x0002u);

  // BG palette idx1=red, idx2=green.
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x03E0u);

  // BG0 uses palette idx1 in tile 1; BG1 uses palette idx2 in tile 1.
  FillBgAllWithTile(mem, 0x06000000u, 1u);
  FillBgAllWithTile(mem, 0x06000000u + 0x0800u, 1u);
  FillTileAllRows_AllPixels(mem, 0x06004000u, 1u, 1u);
  FillTileAllRows_AllPixels(mem, 0x06008000u, 1u, 2u);

  // Exit Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x4000u));

  // Wraparound means inside WIN1 when x >= 200 OR x < 40.
  EXPECT_EQ(SamplePixelAtScanline(mem, 0, 10),
            TestUtil::ARGBFromBGR555(0x001F));
  EXPECT_EQ(SamplePixelAtScanline(mem, 0, 100),
            TestUtil::ARGBFromBGR555(0x03E0));
  EXPECT_EQ(SamplePixelAtScanline(mem, 0, 210),
            TestUtil::ARGBFromBGR555(0x001F));
}

TEST(WindowTest, WIN1_Wraparound_Y1GreaterThanY2) {
  GBAMemory mem;
  mem.Reset();

  // Mode 0, BG0+BG1 enable, WIN1 enable, Forced Blank for setup.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x4000u | 0x0080u));

  mem.Write16(0x04000008u, BgCnt(0, 1, 0));
  mem.Write16(0x0400000Au, BgCnt(1, 2, 1));

  // WIN1H: full width.
  mem.Write16(0x04000042u, (uint16_t)((0u << 8) | 240u));
  // WIN1V: Y1=120, Y2=40 -> wraparound window.
  mem.Write16(0x04000046u, (uint16_t)((120u << 8) | 40u));

  // Inside WIN1 show BG0 only; outside show BG1 only.
  mem.Write16(0x04000048u, (uint16_t)(0x0000u | (0x0001u << 8)));
  mem.Write16(0x0400004Au, 0x0002u);

  // BG palette idx1=red, idx2=green.
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x03E0u);

  FillBgAllWithTile(mem, 0x06000000u, 1u);
  FillBgAllWithTile(mem, 0x06000000u + 0x0800u, 1u);
  FillTileAllRows_AllPixels(mem, 0x06004000u, 1u, 1u);
  FillTileAllRows_AllPixels(mem, 0x06008000u, 1u, 2u);

  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x4000u));

  // Wraparound means inside WIN1 when y >= 120 OR y < 40.
  EXPECT_EQ(SamplePixelAtScanline(mem, 10, 0),
            TestUtil::ARGBFromBGR555(0x001F));
  EXPECT_EQ(SamplePixelAtScanline(mem, 80, 0),
            TestUtil::ARGBFromBGR555(0x03E0));
  EXPECT_EQ(SamplePixelAtScanline(mem, 140, 0),
            TestUtil::ARGBFromBGR555(0x001F));
}

TEST(WindowTest, WIN0_Empty_WhenX1EqualsX2) {
  GBAMemory mem;
  mem.Reset();

  // Mode 0, BG0+BG1 enable, WIN0 enable, Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x2000u | 0x0080u));
  mem.Write16(0x04000008u, BgCnt(0, 1, 0));
  mem.Write16(0x0400000Au, BgCnt(1, 2, 1));

  // WIN0H: X1==X2 => empty window.
  mem.Write16(0x04000040u, (uint16_t)((100u << 8) | 100u));
  // WIN0V: full height.
  mem.Write16(0x04000044u, (uint16_t)((0u << 8) | 160u));

  // Inside WIN0 show BG0 only; outside show BG1 only.
  mem.Write16(0x04000048u, (uint16_t)(0x0001u));
  mem.Write16(0x0400004Au, 0x0002u);

  mem.Write16(0x05000002u, 0x001Fu); // red
  mem.Write16(0x05000004u, 0x03E0u); // green

  FillBgAllWithTile(mem, 0x06000000u, 1u);
  FillBgAllWithTile(mem, 0x06000000u + 0x0800u, 1u);
  FillTileAllRows_AllPixels(mem, 0x06004000u, 1u, 1u);
  FillTileAllRows_AllPixels(mem, 0x06008000u, 1u, 2u);

  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x2000u));

  // Empty WIN0 => always outside => BG1.
  EXPECT_EQ(SamplePixelAtScanline(mem, 0, 10),
            TestUtil::ARGBFromBGR555(0x03E0));
  EXPECT_EQ(SamplePixelAtScanline(mem, 0, 210),
            TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(WindowTest, WIN0_Empty_WhenY1EqualsY2) {
  GBAMemory mem;
  mem.Reset();

  // Mode 0, BG0+BG1 enable, WIN0 enable, Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x2000u | 0x0080u));
  mem.Write16(0x04000008u, BgCnt(0, 1, 0));
  mem.Write16(0x0400000Au, BgCnt(1, 2, 1));

  // WIN0H: full width.
  mem.Write16(0x04000040u, (uint16_t)((0u << 8) | 240u));
  // WIN0V: Y1==Y2 => empty window.
  mem.Write16(0x04000044u, (uint16_t)((50u << 8) | 50u));

  mem.Write16(0x04000048u, (uint16_t)(0x0001u));
  mem.Write16(0x0400004Au, 0x0002u);

  mem.Write16(0x05000002u, 0x001Fu); // red
  mem.Write16(0x05000004u, 0x03E0u); // green

  FillBgAllWithTile(mem, 0x06000000u, 1u);
  FillBgAllWithTile(mem, 0x06000000u + 0x0800u, 1u);
  FillTileAllRows_AllPixels(mem, 0x06004000u, 1u, 1u);
  FillTileAllRows_AllPixels(mem, 0x06008000u, 1u, 2u);

  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x2000u));

  EXPECT_EQ(SamplePixelAtScanline(mem, 10, 0),
            TestUtil::ARGBFromBGR555(0x03E0));
  EXPECT_EQ(SamplePixelAtScanline(mem, 140, 0),
            TestUtil::ARGBFromBGR555(0x03E0));
}
