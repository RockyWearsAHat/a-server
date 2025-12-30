#include <gtest/gtest.h>

#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/PPU.h"
#include "support/PPUTestHelper.h"

using namespace AIO::Emulator::GBA;
namespace TestUtil = AIO::Emulator::GBA::Test;

namespace {
constexpr uint32_t kBgPaletteBase = 0x05000000u;
constexpr uint32_t kVramBase = 0x06000000u;

// Affine BG map is a flat byte array. For screen size 0, it is 16x16 tiles.
void FillAffineMapAll(GBAMemory &mem, uint32_t mapBase, uint8_t tileIndex,
                      int screenSize) {
  const int mapWidthTiles = 16 << screenSize;
  const int mapHeightTiles = 16 << screenSize;
  for (int ty = 0; ty < mapHeightTiles; ++ty) {
    for (int tx = 0; tx < mapWidthTiles; ++tx) {
      TestUtil::WriteVramPackedByteViaHalfword(
          mem, mapBase + (uint32_t)(ty * mapWidthTiles + tx), tileIndex);
    }
  }
}

void FillAffineTileAll(GBAMemory &mem, uint32_t tileBase, uint8_t tileIndex,
                       uint8_t colorIndex) {
  const uint32_t addr = tileBase + (uint32_t)tileIndex * 64u;
  for (uint32_t i = 0; i < 64u; ++i) {
    TestUtil::WriteVramPackedByteViaHalfword(mem, addr + i, colorIndex);
  }
}

void FillAffineTileRow(GBAMemory &mem, uint32_t tileBase, uint8_t tileIndex,
                       int row, const uint8_t *pixels8) {
  const uint32_t addr =
      tileBase + (uint32_t)tileIndex * 64u + (uint32_t)row * 8u;
  for (int x = 0; x < 8; ++x) {
    TestUtil::WriteVramPackedByteViaHalfword(mem, addr + (uint32_t)x,
                                             pixels8[x]);
  }
}

uint16_t BgCntAffine(uint16_t priority, uint16_t charBase, uint16_t screenBase,
                     bool overflowWrap, uint16_t screenSize) {
  // For affine BG: bits 0-1 priority, 2-3 charBase, 8-12 screenBase, 13 wrap,
  // 14-15 screen size.
  return (uint16_t)((priority & 3u) | ((charBase & 3u) << 2) |
                    ((screenBase & 31u) << 8) |
                    ((overflowWrap ? 1u : 0u) << 13) |
                    ((screenSize & 3u) << 14));
}

// Run one full frame to ensure affine BG reference points latch on VBlank.
void RunOneFrame(PPU &ppu) { ppu.Update(TestUtil::kCyclesPerScanline * 228); }

void RenderScanlineToHBlank(PPU &ppu, int scanline) {
  // Assumes we are at start-of-frame (scanline 0, cycleCounter 0).
  ppu.Update(TestUtil::kCyclesPerScanline * scanline +
             TestUtil::kCyclesToHBlankStart);
  ppu.SwapBuffers();
}

uint32_t SamplePixel(GBAMemory &mem, int scanline, int x) {
  PPU ppu(mem);
  RunOneFrame(ppu); // enter VBlank once and wrap to scanline 0.
  RenderScanlineToHBlank(ppu, scanline);
  return TestUtil::GetPixel(ppu, x, scanline);
}
} // namespace

TEST(AffineBgTest, OverflowWrapDisabled_OutOfRangeIsTransparent) {
  GBAMemory mem;
  mem.Reset();

  // Mode 2, BG2 enable, Forced Blank for setup.
  mem.Write16(0x04000000u, (uint16_t)(0x0002u | 0x0400u | 0x0080u));

  // BG2CNT: priority 0, charBase=0, screenBase=8 (avoid map/tile overlap),
  // wrap disabled, size=0 (128x128).
  mem.Write16(0x0400000Cu, BgCntAffine(0, 0, 8, false, 0));

  // Affine params: identity (pa=1.0, pb=0, pc=0, pd=1.0).
  mem.Write16(0x04000020u, 0x0100u);
  mem.Write16(0x04000022u, 0x0000u);
  mem.Write16(0x04000024u, 0x0000u);
  mem.Write16(0x04000026u, 0x0100u);

  // Reference point: X = 128.0 (out of range for sizeMask=127), Y=0.
  // BG2X/BG2Y are 28-bit signed fixed-point (24.8 stored across 32 bits).
  mem.Write32(0x04000028u, (uint32_t)(128u << 8));
  mem.Write32(0x0400002Cu, 0u);

  // Palette: idx1 = red. Backdrop remains black.
  mem.Write16(kBgPaletteBase + 2u, 0x001Fu);

  const uint32_t mapBase = kVramBase + 8u * 2048u;
  const uint32_t tileBase = kVramBase + 0u;
  FillAffineMapAll(mem, mapBase, 0u, 0);
  FillAffineTileAll(mem, tileBase, 0u, 1u);

  // Exit Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x0002u | 0x0400u));

  // Out of range and wrap disabled => BG2 doesn't draw => black.
  EXPECT_EQ(SamplePixel(mem, 0, 0), 0xFF000000u);
}

TEST(AffineBgTest, OverflowWrapEnabled_WrapsCoordinates) {
  GBAMemory mem;
  mem.Reset();

  mem.Write16(0x04000000u, (uint16_t)(0x0002u | 0x0400u | 0x0080u));

  // Wrap enabled.
  mem.Write16(0x0400000Cu, BgCntAffine(0, 0, 8, true, 0));

  mem.Write16(0x04000020u, 0x0100u);
  mem.Write16(0x04000022u, 0x0000u);
  mem.Write16(0x04000024u, 0x0000u);
  mem.Write16(0x04000026u, 0x0100u);

  // X = 128.0 wraps to 0.0 when wrap enabled.
  mem.Write32(0x04000028u, (uint32_t)(128u << 8));
  mem.Write32(0x0400002Cu, 0u);

  mem.Write16(kBgPaletteBase + 2u, 0x001Fu);

  const uint32_t mapBase = kVramBase + 8u * 2048u;
  const uint32_t tileBase = kVramBase + 0u;
  FillAffineMapAll(mem, mapBase, 0u, 0);
  FillAffineTileAll(mem, tileBase, 0u, 1u);

  mem.Write16(0x04000000u, (uint16_t)(0x0002u | 0x0400u));

  EXPECT_EQ(SamplePixel(mem, 0, 0), TestUtil::ARGBFromBGR555(0x001Fu));
}

TEST(AffineBgTest, ReferencePointAdvancesPerScanline_ByPB_PD) {
  GBAMemory mem;
  mem.Reset();

  // Mode 2, BG2 enable, Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x0002u | 0x0400u | 0x0080u));

  // Wrap enabled so we don't have to worry about bounds.
  mem.Write16(0x0400000Cu, BgCntAffine(0, 0, 8, true, 0));

  // pa=0 (no x advance across screen), pc=0 (no y advance across screen).
  // pb=+1.0 => next scanline samples +1 x.
  // pd=0.
  mem.Write16(0x04000020u, 0x0000u);
  mem.Write16(0x04000022u, 0x0100u);
  mem.Write16(0x04000024u, 0x0000u);
  mem.Write16(0x04000026u, 0x0000u);

  // Start at (0,0).
  mem.Write32(0x04000028u, 0u);
  mem.Write32(0x0400002Cu, 0u);

  ASSERT_EQ(mem.Read16(0x04000028u), 0u);
  ASSERT_EQ(mem.Read16(0x0400002Au), 0u);
  ASSERT_EQ(mem.Read16(0x0400002Cu), 0u);
  ASSERT_EQ(mem.Read16(0x0400002Eu), 0u);
  ASSERT_EQ(mem.Read16(0x04000022u), 0x0100u);

  // Palette: idx1=red, idx2=green.
  mem.Write16(kBgPaletteBase + 2u, 0x001Fu);
  mem.Write16(kBgPaletteBase + 4u, 0x03E0u);

  const uint32_t mapBase = kVramBase + 8u * 2048u;
  const uint32_t tileBase = kVramBase + 0u;
  FillAffineMapAll(mem, mapBase, 0u, 0);

  ASSERT_EQ(mem.Read8(mapBase + 0u), 0u);

  // Tile 0 row 0: x=0 -> idx1, x=1 -> idx2, rest 0.
  uint8_t row0[8] = {1u, 2u, 0u, 0u, 0u, 0u, 0u, 0u};
  FillAffineTileRow(mem, tileBase, 0u, 0, row0);

  // Ensure other rows are also defined (avoid accidental garbage reads).
  FillAffineTileAll(mem, tileBase, 0u, 0u);
  FillAffineTileRow(mem, tileBase, 0u, 0, row0);

  ASSERT_EQ(mem.Read8(tileBase + 0u * 64u + 0u), 1u);
  ASSERT_EQ(mem.Read8(tileBase + 0u * 64u + 1u), 2u);

  mem.Write16(0x04000000u, (uint16_t)(0x0002u | 0x0400u));

  // Scanline 0 samples x=0 => red.
  EXPECT_EQ(SamplePixel(mem, 0, 0), TestUtil::ARGBFromBGR555(0x001Fu));
  // Scanline 1 should sample x=1 => green if PB/PD scanline stepping is
  // implemented.
  EXPECT_EQ(SamplePixel(mem, 1, 0), TestUtil::ARGBFromBGR555(0x03E0u));
}
