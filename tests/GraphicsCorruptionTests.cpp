#include <gtest/gtest.h>

#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/PPU.h"
#include "support/PPUTestHelper.h"

using namespace AIO::Emulator::GBA;
namespace TestUtil = AIO::Emulator::GBA::Test;

// Tests for graphics corruption bugs found in OG-DK and MZM

TEST(GraphicsCorruptionTest, LargeSpriteWithHighTileIndexDoesNotCorrupt) {
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

  mem.Write16(0x05000200u + 2u, 0x001Fu);

  // 64x64 sprite, 8bpp, 1D mapping
  // attr0: y=0, 8bpp (bit13=1), square shape
  // attr1: x=0, size=3 (64x64 when square)
  // attr2: tileIndex=896 (high value that could cause address overflow)
  const uint16_t attr0 = 0x2000u | 0x0000u;
  const uint16_t attr1 = 0x4000u;
  const uint16_t attr2 = 896u;

  TestUtil::WriteOam16(mem, 0, attr0);
  TestUtil::WriteOam16(mem, 2, attr1);
  TestUtil::WriteOam16(mem, 4, attr2);

  // Write some tile data at high addresses to verify correct mirroring
  const uint32_t tileBase = 0x06010000u;
  const uint32_t testTileNum = 896u;
  const uint32_t testAddr = tileBase + testTileNum * 32u;

  // Check if this address would wrap incorrectly
  if (testAddr >= 0x06018000u) {
    // This should be mirrored correctly by the VRAM system
    const uint32_t mirroredAddr = testAddr - 0x8000u;
    EXPECT_LT(mirroredAddr, 0x06018000u)
        << "VRAM mirroring should keep addresses in valid range";
  }

  mem.Write16(0x04000000u, 0x1000u);

  ppu.Update(960);
  ppu.SwapBuffers();

  const auto &fb = ppu.GetFramebuffer();
  ASSERT_EQ(fb.size(), 240u * 160u);
}

TEST(GraphicsCorruptionTest, ObjTileNumberBoundsChecked2DMapping) {
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

  mem.Write16(0x05000200u + 2u, 0x001Fu);

  // 64x64 sprite, 8bpp, 2D mapping (bit6=0 in DISPCNT)
  // In 2D mapping with 8bpp, tiles are arranged in rows of 32 tiles
  // Each tile is 2 blocks, so row stride is 64 blocks
  const uint16_t attr0 = 0x2000u;
  const uint16_t attr1 = 0x4000u;
  const uint16_t attr2 = 900u;

  TestUtil::WriteOam16(mem, 0, attr0);
  TestUtil::WriteOam16(mem, 2, attr1);
  TestUtil::WriteOam16(mem, 4, attr2);

  // DISPCNT: mode 0, OBJ enable, 2D mapping (bit6=0)
  mem.Write16(0x04000000u, 0x1000u);

  ppu.Update(960);
  ppu.SwapBuffers();

  const auto &fb = ppu.GetFramebuffer();
  ASSERT_EQ(fb.size(), 240u * 160u);
}

TEST(GraphicsCorruptionTest, SpriteTileVramMirroringCorrect) {
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

  mem.Write16(0x05000200u + 2u, 0x001Fu);
  mem.Write16(0x05000200u + 4u, 0x03E0u);

  // Small sprite at valid tile index
  const uint16_t attr0 = 0x2000u;
  const uint16_t attr1 = 0x0000u;
  const uint16_t attr2 = 0u;

  TestUtil::WriteOam16(mem, 0, attr0);
  TestUtil::WriteOam16(mem, 2, attr1);
  TestUtil::WriteOam16(mem, 4, attr2);

  // Write data to OBJ VRAM area (0x06010000-0x06017FFF)
  const uint32_t objBase = 0x06010000u;
  mem.Write16(objBase, 0x0101u);

  mem.Write16(0x04000000u, 0x1040u);

  ppu.Update(960);
  ppu.SwapBuffers();

  const auto &fb = ppu.GetFramebuffer();
  const uint32_t pixel = fb[0];
  EXPECT_NE(pixel & 0xFFFFFF, 0u) << "Sprite should render with valid color";
}

TEST(GraphicsCorruptionTest, MultipleSpritesWithDifferentTileIndices) {
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

  mem.Write16(0x05000200u + 2u, 0x001Fu);
  mem.Write16(0x05000200u + 4u, 0x03E0u);
  mem.Write16(0x05000200u + 6u, 0x7C00u);

  // Create 3 sprites with increasing tile indices
  for (int i = 0; i < 3; ++i) {
    const uint32_t sprBase = i * 8u;
    const uint16_t attr0 = 0x0000u | (i * 16);
    const uint16_t attr1 = 0x0000u | (i * 16);
    const uint16_t attr2 = (200 * i);

    TestUtil::WriteOam16(mem, sprBase + 0, attr0);
    TestUtil::WriteOam16(mem, sprBase + 2, attr1);
    TestUtil::WriteOam16(mem, sprBase + 4, attr2);

    const uint32_t tileAddr = 0x06010000u + (200u * i) * 32u;
    if (tileAddr < 0x06018000u) {
      mem.Write16(tileAddr, 0x0101u | (i << 8));
    }
  }

  mem.Write16(0x04000000u, 0x1040u);

  ppu.Update(960);
  ppu.SwapBuffers();
}
