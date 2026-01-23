#include <gtest/gtest.h>

#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/PPU.h"
#include "support/PPUTestHelper.h"

using namespace AIO::Emulator::GBA;
namespace TestUtil = AIO::Emulator::GBA::Test;

// PPU timing tests (GBATEK compliance)
class PPUTimingTest : public ::testing::Test {
protected:
  GBAMemory memory;
  PPU ppu{memory};
};

// GBATEK: HBlank starts at cycle 960 of each scanline
TEST_F(PPUTimingTest, HBlankStartsAtCycle960) {
  ppu.Update(959);
  uint16_t dispstat = memory.Read16(0x04000004);
  EXPECT_EQ(dispstat & 0x02, 0) << "HBlank flag should be clear before cycle 960";

  ppu.Update(1);
  dispstat = memory.Read16(0x04000004);
  EXPECT_NE(dispstat & 0x02, 0) << "HBlank flag should be set at cycle 960";
}

// GBATEK: HBlank clears at start of next scanline
TEST_F(PPUTimingTest, HBlankClearsAtScanlineEnd) {
  ppu.Update(1232);
  uint16_t dispstat = memory.Read16(0x04000004);
  EXPECT_EQ(dispstat & 0x02, 0) << "HBlank flag should clear at scanline boundary";
}

// GBATEK: VBlank starts at scanline 160
TEST_F(PPUTimingTest, VBlankStartsAtScanline160) {
  ppu.Update(1232 * 160);
  uint16_t dispstat = memory.Read16(0x04000004);
  EXPECT_NE(dispstat & 0x01, 0) << "VBlank flag should be set at scanline 160";
  EXPECT_EQ(memory.Read16(0x04000006), 160) << "VCOUNT should be 160";
}

// GBATEK: Frame wraps at scanline 228
TEST_F(PPUTimingTest, FrameWrapsAtScanline228) {
  ppu.Update(1232 * 228);
  EXPECT_EQ(memory.Read16(0x04000006), 0) << "VCOUNT should wrap to 0";
}

// GBATEK: VBlank IRQ fires on rising edge
TEST_F(PPUTimingTest, VBlankIRQFiresAtScanline160) {
  memory.Write16(0x04000004, 0x0008); // Enable VBlank IRQ
  ppu.Update(1232 * 160);
  EXPECT_NE(memory.Read16(0x04000202) & 0x01, 0) << "VBlank IRQ should fire";
}

// Frame timing constant
TEST_F(PPUTimingTest, FrameTotalCycles) {
  EXPECT_EQ(1232 * 228, 280896) << "Frame must be exactly 280,896 cycles";
}

// PPU blend tests (GBATEK color effects)
class PPUBlendTest : public ::testing::Test {
protected:
  GBAMemory memory;
  PPU ppu{memory};

  void SetUp() override {
    memory.Write16(0x04000000, 0x0100); // DISPCNT: BG0 enable
    memory.Write16(0x05000000, 0x001F); // Backdrop = Red (31,0,0)
  }
};

// GBATEK: Mode 2 brightness increase
TEST_F(PPUBlendTest, BrightnessIncrease_EVY16_FullWhite) {
  // BLDCNT: Mode 2, Backdrop as first target (bit 5)
  memory.Write16(0x04000050, 0x00A0); // 0b10100000
  memory.Write16(0x04000054, 0x0010); // EVY = 16

  ppu.Update(1232); // Render scanline 0
  ppu.SwapBuffers();
  uint32_t pixel = ppu.GetFramebuffer()[0];

  // Red (31,0,0) -> White (31,31,31) at EVY=16
  uint8_t g = (pixel >> 8) & 0xFF;
  uint8_t b = pixel & 0xFF;
  EXPECT_GE(g, 248) << "G should be ~255 (full fade to white)";
  EXPECT_GE(b, 248) << "B should be ~255 (full fade to white)";
}

// GBATEK: Mode 3 brightness decrease
TEST_F(PPUBlendTest, BrightnessDecrease_EVY16_FullBlack) {
  memory.Write16(0x04000050, 0x00E0); // Mode 3, Backdrop target
  memory.Write16(0x04000054, 0x0010); // EVY = 16

  ppu.Update(1232);
  ppu.SwapBuffers();
  uint32_t pixel = ppu.GetFramebuffer()[0];

  // Red (31,0,0) -> Black (0,0,0) at EVY=16
  uint8_t r = (pixel >> 16) & 0xFF;
  EXPECT_LE(r, 8) << "R should be ~0 (full fade to black)";
}

// GBATEK: EVY clamped at 16
TEST_F(PPUBlendTest, EVYClampedAt16) {
  // Verify clamping in the brightness application math directly using a
  // known backdrop color (Red = BGR555 0x001F).
  const uint32_t backdrop = TestUtil::ARGBFromBGR555(0x001F); // Red (31,0,0)

  // Brightness increase: EVY > 16 should equal EVY = 16
  uint32_t inc31 = PPU::ApplyBrightnessIncrease(backdrop, 31);
  uint32_t inc16 = PPU::ApplyBrightnessIncrease(backdrop, 16);
  EXPECT_EQ(inc31, inc16) << "Brightness increase EVY should be clamped to 16";

  // Brightness decrease: EVY > 16 should equal EVY = 16
  uint32_t dec31 = PPU::ApplyBrightnessDecrease(backdrop, 31);
  uint32_t dec16 = PPU::ApplyBrightnessDecrease(backdrop, 16);
  EXPECT_EQ(dec31, dec16) << "Brightness decrease EVY should be clamped to 16";
}

// Effect only applies to first-target layers
TEST_F(PPUBlendTest, EffectOnlyAppliesToFirstTarget) {
  // Backdrop NOT set as first target
  memory.Write16(0x04000050, 0x0080); // Mode 2, NO targets
  memory.Write16(0x04000054, 0x0010);

  ppu.Update(1232);
  ppu.SwapBuffers();
  uint32_t pixel = ppu.GetFramebuffer()[0];

  // Backdrop should NOT be affected (not a first target)
  uint8_t g = (pixel >> 8) & 0xFF;
  EXPECT_LE(g, 8) << "G should still be 0 (no fade applied)";
}

TEST(GBAMemoryTest, VramByteWrites_BgDuplicates_ObjIgnored) {
  GBAMemory mem;
  mem.Reset();

  const uint32_t bgAddrMode0 = 0x06000000u;
  const uint32_t objAddrMode0 = 0x06010000u;

  // Mode 0 + Forced Blank: BG VRAM byte writes duplicate; OBJ VRAM byte writes
  // are ignored.
  mem.Write16(0x04000000u, 0x0080u);
  EXPECT_EQ(mem.Read8(bgAddrMode0), 0u);
  mem.Write8(bgAddrMode0, 0x12u);
  EXPECT_EQ(mem.Read8(bgAddrMode0), 0x12u);
  EXPECT_EQ(mem.Read8(bgAddrMode0 + 1u), 0x12u);

  EXPECT_EQ(mem.Read8(objAddrMode0), 0u);
  mem.Write8(objAddrMode0, 0x34u);
  EXPECT_EQ(mem.Read8(objAddrMode0), 0u);
  EXPECT_EQ(mem.Read8(objAddrMode0 + 1u), 0u);

  // Mode 4 + Forced Blank: BG VRAM byte writes still duplicate; OBJ VRAM byte
  // writes are ignored.
  mem.Reset();
  mem.Write16(0x04000000u, 0x0084u);
  const uint32_t bgAddrMode4 = 0x06000000u;
  const uint32_t objAddrMode4 = 0x06014000u;

  EXPECT_EQ(mem.Read8(bgAddrMode4), 0u);
  EXPECT_EQ(mem.Read8(bgAddrMode4 + 1u), 0u);
  mem.Write8(bgAddrMode4, 0x56u);
  EXPECT_EQ(mem.Read8(bgAddrMode4), 0x56u);
  EXPECT_EQ(mem.Read8(bgAddrMode4 + 1u), 0x56u);

  EXPECT_EQ(mem.Read8(objAddrMode4), 0u);
  mem.Write8(objAddrMode4, 0x7Au);
  EXPECT_EQ(mem.Read8(objAddrMode4), 0u);
  EXPECT_EQ(mem.Read8(objAddrMode4 + 1u), 0u);
}

TEST(PPUTest, Obj2DMapping8bppUses64BlockRowStride) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // DISPCNT: mode 0, OBJ enable, OBJ mapping = 2D (bit6=0)
  mem.Write16(0x04000000u, 0x1080u);

  // OBJ palette: index 1 = red-ish, index 2 = green-ish
  mem.Write16(0x05000200u + 2u, 0x001Fu); // idx1
  mem.Write16(0x05000200u + 4u, 0x03E0u); // idx2

  // One sprite at OAM entry 0:
  // attr0: y=0, 8bpp (bit13), square (shape 0)
  // attr1: x=0, size=1 -> 16x16 when square
  // attr2: tileIndex=0, prio=0
  const uint16_t attr0 = (uint16_t)(0u | (1u << 13));
  const uint16_t attr1 = (uint16_t)(0u | (1u << 14));
  const uint16_t attr2 = 0u;
  TestUtil::WriteOam16(mem, 0, attr0);
  TestUtil::WriteOam16(mem, 2, attr1);
  TestUtil::WriteOam16(mem, 4, attr2);

  // Populate OBJ VRAM (tileBase = 0x06010000). In 2D mapping, a tile-row step
  // is 64 blocks in 8bpp.
  const uint32_t tileBase = 0x06010000u;
  TestUtil::WriteVramPackedByteViaHalfword(mem, tileBase + 0u, 1u);
  TestUtil::WriteVramPackedByteViaHalfword(mem, tileBase + 2048u, 2u);

  // Exit Forced Blank before rendering.
  mem.Write16(0x04000000u, 0x1000u);

  // Render scanline 0 (top half of sprite)
  ppu.Update(960);
  ppu.SwapBuffers();
  const auto fb0 = ppu.GetFramebuffer();
  ASSERT_GE(fb0.size(), (size_t)PPU::SCREEN_WIDTH);
  EXPECT_EQ(fb0[0], TestUtil::ARGBFromBGR555(0x001F));

  // Advance to scanline 8
  ppu.Update(1232 - 960);     // finish line 0
  ppu.Update(1232 * 7 + 960); // 7 full lines + hblank of line 8
  ppu.SwapBuffers();
  const auto fb8 = ppu.GetFramebuffer();
  const size_t idx8 = (size_t)8 * (size_t)PPU::SCREEN_WIDTH;
  ASSERT_GT(fb8.size(), idx8);
  EXPECT_EQ(fb8[idx8 + 0], TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, BgTileFetchDoesNotReadFromObjVram_Mode0) {
  GBAMemory mem;
  mem.Reset();

  // Perform all VRAM setup before constructing the PPU. This keeps unit tests
  // deterministic and avoids timing-dependent VRAM/OAM access restrictions
  // during setup.
  mem.Write16(0x04000000u, 0x0100u); // mode 0, BG0 enabled

  // Background palette: idx1=red, idx2=green.
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x03E0u);

  // BG0CNT:
  // - priority 0
  // - char base block 3 => tileBase=0x0600C000
  // - screen base block 31 => mapBase=0x0600F800 (keeps map away from tile
  // data)
  // - 4bpp
  const uint16_t bg0cnt = (uint16_t)((3u << 2) | (31u << 8) | 0u);
  mem.Write16(0x04000008u, bg0cnt);
  EXPECT_EQ(mem.Read16(0x04000008u), bg0cnt);

  // Put a map entry at (0,0) with tile index 512 (0x200). With char base block
  // 3 (tileBase=0x0600C000), that would address 0x06010000 (OBJ VRAM) if BG
  // fetches were not restricted. Hardware behavior is that BG fetches in modes
  // 0-2 are limited to BG VRAM (64KB) and wrap within that window (mask
  // 0xFFFF), which maps this back to tile #0.
  const uint16_t tileEntry = 0x0200u;
  const uint32_t mapBase = 0x0600F800u;
  mem.Write16(mapBase + 0u, tileEntry);
  EXPECT_EQ(mem.Read16(mapBase + 0u), tileEntry);

  // Fill BG VRAM tile #0 (0x06000000) with palette index 1 (red).
  for (uint32_t o = 0; o < 32; o += 2) {
    mem.Write16(0x06000000u + o, 0x1111u);
  }
  EXPECT_EQ(mem.Read16(0x06000000u), 0x1111u);

  // Fill OBJ VRAM at 0x06010000 with palette index 2 (green). If the BG fetch
  // incorrectly samples OBJ VRAM, we'll see green.
  for (uint32_t o = 0; o < 32; o += 2) {
    mem.Write16(0x06010000u + o, 0x2222u);
  }
  EXPECT_EQ(mem.Read16(0x06010000u), 0x2222u);

  PPU ppu(mem);

  // Render scanline 0 and sample pixel (0,0). Correct behavior: wrap within BG
  // VRAM => reads tile #0 => red.
  ppu.Update(TestUtil::kCyclesToHBlankStart);
  ppu.SwapBuffers();
  EXPECT_EQ(TestUtil::GetPixel(ppu, 0, 0), TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, Obj2DMapping4bppUses32BlockRowStride) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // DISPCNT: mode 0, OBJ enable, OBJ mapping = 2D (bit6=0), Forced Blank.
  mem.Write16(0x04000000u, 0x1080u);

  // OBJ palette: index 1 = red-ish, index 2 = green-ish
  mem.Write16(0x05000200u + 2u, 0x001Fu); // idx1
  mem.Write16(0x05000200u + 4u, 0x03E0u); // idx2

  // Sprite 0: 16x16, 4bpp, at (0,0), tileIndex=0.
  const uint16_t attr0 = 0u;                   // y=0, 4bpp, square
  const uint16_t attr1 = (uint16_t)(1u << 14); // size=1 => 16x16
  const uint16_t attr2 = 0u;                   // tileIndex=0
  TestUtil::WriteOam16(mem, 0, attr0);
  TestUtil::WriteOam16(mem, 2, attr1);
  TestUtil::WriteOam16(mem, 4, attr2);

  // In 2D mapping, for 4bpp, a tile-row step is 32 blocks => 1024 bytes.
  const uint32_t tileBase = 0x06010000u;
  mem.Write16(tileBase + 0u, 0x1111u);    // scanline 0 => palette idx 1
  mem.Write16(tileBase + 1024u, 0x2222u); // scanline 8 => palette idx 2

  // Exit Forced Blank before rendering.
  mem.Write16(0x04000000u, 0x1000u);

  // Scanline 0.
  ppu.Update(960);
  ppu.SwapBuffers();
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x001F));

  // Advance to scanline 8.
  ppu.Update(1232 - 960);
  ppu.Update(1232 * 7 + 960);
  ppu.SwapBuffers();
  const auto fb8 = ppu.GetFramebuffer();
  const size_t idx8 = (size_t)8 * (size_t)PPU::SCREEN_WIDTH;
  EXPECT_EQ(fb8[idx8 + 0], TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, Obj1DMapping4bppUsesSpriteWidthForRowStride) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // DISPCNT: mode 0, OBJ enable, OBJ mapping = 1D (bit6=1), Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x1000u | 0x0080u | 0x0040u));

  // OBJ palette: index 1 = red-ish, index 2 = green-ish
  mem.Write16(0x05000200u + 2u, 0x001Fu); // idx1
  mem.Write16(0x05000200u + 4u, 0x03E0u); // idx2

  // Sprite 0: 16x16, 4bpp, at (0,0), tileIndex=0.
  const uint16_t attr0 = 0u;                   // y=0, 4bpp, square
  const uint16_t attr1 = (uint16_t)(1u << 14); // size=1 => 16x16
  const uint16_t attr2 = 0u;                   // tileIndex=0
  TestUtil::WriteOam16(mem, 0, attr0);
  TestUtil::WriteOam16(mem, 2, attr1);
  TestUtil::WriteOam16(mem, 4, attr2);

  // In 1D mapping, row stride depends on sprite width in tiles.
  // For 16x16, width=2 tiles => one 8px tile-row step = 2 tiles = 64 bytes.
  const uint32_t tileBase = 0x06010000u;
  mem.Write16(tileBase + 0u, 0x1111u);
  mem.Write16(tileBase + 64u, 0x2222u);

  // Exit Forced Blank before rendering.
  mem.Write16(0x04000000u, (uint16_t)(0x1000u | 0x0040u));

  ppu.Update(960);
  ppu.SwapBuffers();
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x001F));

  ppu.Update(1232 - 960);
  ppu.Update(1232 * 7 + 960);
  ppu.SwapBuffers();
  const auto fb8 = ppu.GetFramebuffer();
  const size_t idx8 = (size_t)8 * (size_t)PPU::SCREEN_WIDTH;
  EXPECT_EQ(fb8[idx8 + 0], TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, Obj1DMapping8bppUsesSpriteWidthForRowStride) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // DISPCNT: mode 0, OBJ enable, OBJ mapping = 1D (bit6=1), Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x1000u | 0x0080u | 0x0040u));

  // OBJ palette: index 1 = red-ish, index 2 = green-ish
  mem.Write16(0x05000200u + 2u, 0x001Fu); // idx1
  mem.Write16(0x05000200u + 4u, 0x03E0u); // idx2

  // Sprite 0: 16x16, 8bpp (bit13), at (0,0), tileIndex=0.
  const uint16_t attr0 = (uint16_t)(0u | (1u << 13));
  const uint16_t attr1 = (uint16_t)(0u | (1u << 14));
  const uint16_t attr2 = 0u;
  TestUtil::WriteOam16(mem, 0, attr0);
  TestUtil::WriteOam16(mem, 2, attr1);
  TestUtil::WriteOam16(mem, 4, attr2);

  // In 1D mapping, row stride depends on sprite width. For 16x16, width=2
  // tiles, and in 8bpp each tile is 64 bytes => one tile-row step = 128 bytes.
  const uint32_t tileBase = 0x06010000u;
  TestUtil::WriteVramPackedByteViaHalfword(mem, tileBase + 0u, 1u);
  TestUtil::WriteVramPackedByteViaHalfword(mem, tileBase + 128u, 2u);

  // Exit Forced Blank before rendering.
  mem.Write16(0x04000000u, (uint16_t)(0x1000u | 0x0040u));

  ppu.Update(960);
  ppu.SwapBuffers();
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x001F));

  ppu.Update(1232 - 960);
  ppu.Update(1232 * 7 + 960);
  ppu.SwapBuffers();
  const auto fb8 = ppu.GetFramebuffer();
  const size_t idx8 = (size_t)8 * (size_t)PPU::SCREEN_WIDTH;
  EXPECT_EQ(fb8[idx8 + 0], TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, Obj1DMapping4bpp_32x32UsesSpriteWidthForRowStride) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // DISPCNT: mode 0, OBJ enable, OBJ mapping = 1D (bit6=1), Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x1000u | 0x0080u | 0x0040u));

  // OBJ palette: idx1=red, idx2=green.
  mem.Write16(0x05000200u + 2u, 0x001Fu);
  mem.Write16(0x05000200u + 4u, 0x03E0u);

  // Sprite 0: 32x32 (square size=2), 4bpp, at (0,0).
  const uint16_t attr0 = 0u;
  const uint16_t attr1 = (uint16_t)(2u << 14);
  const uint16_t attr2 = 0u;
  TestUtil::WriteOam16(mem, 0, attr0);
  TestUtil::WriteOam16(mem, 2, attr1);
  TestUtil::WriteOam16(mem, 4, attr2);

  // In 1D mapping, a tile-row step is spriteWidthInTiles tiles.
  // For 32x32, width=4 tiles; 4bpp tile size=32 bytes => row step=128 bytes.
  const uint32_t tileBase = 0x06010000u;
  mem.Write16(tileBase + 0u, 0x1111u);   // scanline 0 => red
  mem.Write16(tileBase + 128u, 0x2222u); // scanline 8 => green

  // Exit Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x1000u | 0x0040u));

  // Scanline 0.
  ppu.Update(960);
  ppu.SwapBuffers();
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x001F));

  // Advance to scanline 8.
  ppu.Update(1232 - 960);
  ppu.Update(1232 * 7 + 960);
  ppu.SwapBuffers();
  const auto fb8 = ppu.GetFramebuffer();
  const size_t idx8 = (size_t)8 * (size_t)PPU::SCREEN_WIDTH;
  EXPECT_EQ(fb8[idx8 + 0], TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, Obj1DMapping8bpp_32x32UsesSpriteWidthForRowStride) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // DISPCNT: mode 0, OBJ enable, OBJ mapping = 1D (bit6=1), Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x1000u | 0x0080u | 0x0040u));

  // OBJ palette: idx1=red, idx2=green.
  mem.Write16(0x05000200u + 2u, 0x001Fu);
  mem.Write16(0x05000200u + 4u, 0x03E0u);

  // Sprite 0: 32x32 (square size=2), 8bpp, at (0,0).
  const uint16_t attr0 = (uint16_t)(0u | (1u << 13));
  const uint16_t attr1 = (uint16_t)(2u << 14);
  const uint16_t attr2 = 0u;
  TestUtil::WriteOam16(mem, 0, attr0);
  TestUtil::WriteOam16(mem, 2, attr1);
  TestUtil::WriteOam16(mem, 4, attr2);

  // For 32x32, width=4 tiles; 8bpp tile size=64 bytes => row step=256 bytes.
  const uint32_t tileBase = 0x06010000u;
  TestUtil::WriteVramPackedByteViaHalfword(mem, tileBase + 0u, 1u);
  TestUtil::WriteVramPackedByteViaHalfword(mem, tileBase + 256u, 2u);

  // Exit Forced Blank.
  mem.Write16(0x04000000u, (uint16_t)(0x1000u | 0x0040u));

  ppu.Update(960);
  ppu.SwapBuffers();
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x001F));

  ppu.Update(1232 - 960);
  ppu.Update(1232 * 7 + 960);
  ppu.SwapBuffers();
  const auto fb8 = ppu.GetFramebuffer();
  const size_t idx8 = (size_t)8 * (size_t)PPU::SCREEN_WIDTH;
  EXPECT_EQ(fb8[idx8 + 0], TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, ObjXCoordinateWrapsAsSigned9Bit) {
  GBAMemory mem;
  mem.Reset();

  // Mode 0, OBJ enable, Forced Blank for setup.
  mem.Write16(0x04000000u, 0x1080u);

  // OBJ palette idx1 = red.
  mem.Write16(0x05000200u + 2u, 0x001Fu);

  // Disable all sprites.
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // Sprite 0: 8x8, 4bpp, y=0.
  // X is 9-bit; values 256..511 represent x-512 (negative positions).
  // Set x=511 => -1, so the sprite should appear starting at x=-1.
  const uint16_t attr0 = 0u;
  const uint16_t attr1 = (uint16_t)511u; // x=511
  const uint16_t attr2 = 0u;
  TestUtil::WriteOam16(mem, 0u, attr0);
  TestUtil::WriteOam16(mem, 2u, attr1);
  TestUtil::WriteOam16(mem, 4u, attr2);

  // Tile 0 row 0: make only sprite pixel X=1 visible (idx1), others 0.
  // Pixel1 is nibble1 of the first halfword.
  mem.Write16(0x06010000u + 0u, 0x0010u);

  // Exit Forced Blank.
  mem.Write16(0x04000000u, 0x1000u);
  PPU ppu(mem);
  ppu.Update(960);
  ppu.SwapBuffers();

  // Screen x=0 corresponds to sprite pixel x=1 (because sprite starts at -1).
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, ObjYCoordinateWrapsAsSigned8Bit) {
  GBAMemory mem;
  mem.Reset();

  // Mode 0, OBJ enable, Forced Blank for setup.
  mem.Write16(0x04000000u, 0x1080u);

  // OBJ palette idx1 = red.
  mem.Write16(0x05000200u + 2u, 0x001Fu);

  // Disable all sprites.
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // Sprite 0: 8x8, 4bpp, x=0.
  // Y is 8-bit; values 160..255 represent y-256 (negative positions).
  // Set y=255 => -1, so scanline 0 samples sprite row 1.
  const uint16_t attr0 = (uint16_t)255u; // y=255
  const uint16_t attr1 = 0u;
  const uint16_t attr2 = 0u;
  TestUtil::WriteOam16(mem, 0u, attr0);
  TestUtil::WriteOam16(mem, 2u, attr1);
  TestUtil::WriteOam16(mem, 4u, attr2);

  // Tile 0: row 1 all idx1 (red), row 0 is 0.
  const uint32_t tile0 = 0x06010000u;
  // Row 1 starts at byte 4.
  mem.Write16(tile0 + 4u, 0x1111u);
  mem.Write16(tile0 + 6u, 0x1111u);

  // Exit Forced Blank.
  mem.Write16(0x04000000u, 0x1000u);
  PPU ppu(mem);
  ppu.Update(960);
  ppu.SwapBuffers();

  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, ObjPriority0_DrawsInFrontOfBgPriority1) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Forced blank for setup.
  mem.Write16(0x04000000u, 0x0080u);

  // Disable all sprites by default (OAM reset-to-zero would otherwise create
  // many active sprites once we write non-zero tile data).
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // Mode 0, BG0 enabled, OBJ enabled.
  // BG0 priority = 1, charBase=1, screenBase=0.
  mem.Write16(0x04000008u, (uint16_t)(1u | (1u << 2)));

  // Palettes: BG idx1 = green, OBJ idx1 = red.
  mem.Write16(0x05000002u, 0x03E0u);
  mem.Write16(0x05000200u + 2u, 0x001Fu);

  // BG0 tilemap row 0 -> tile 1 everywhere.
  for (uint32_t tx = 0; tx < 32; ++tx) {
    mem.Write16(0x06000000u + tx * 2u, 0x0001u);
  }
  // BG0 tile 1 row 0 -> palette index 1.
  mem.Write16(0x06004000u + 1u * 32u + 0u, 0x1111u);
  mem.Write16(0x06004000u + 1u * 32u + 2u, 0x1111u);

  // Sprite 0: 8x8, 4bpp, at (0,0), tileIndex=0, priority=0.
  const uint16_t obj_attr0 = 0u;
  const uint16_t obj_attr1 = 0u;
  const uint16_t obj_attr2 = (uint16_t)(0u << 10);
  TestUtil::WriteOam16(mem, 0, obj_attr0);
  TestUtil::WriteOam16(mem, 2, obj_attr1);
  TestUtil::WriteOam16(mem, 4, obj_attr2);

  // OBJ tile 0 row 0 -> palette index 1 (red).
  mem.Write16(0x06010000u + 0u, 0x1111u);
  mem.Write16(0x06010000u + 2u, 0x1111u);

  // Exit forced blank and render.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x1000u));
  ppu.Update(960);
  ppu.SwapBuffers();

  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, ObjPriority2_DrawsBehindBgPriority1) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Forced blank for setup.
  mem.Write16(0x04000000u, 0x0080u);

  // Disable all sprites by default.
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // Mode 0, BG0 enabled, OBJ enabled.
  // BG0 priority = 1, charBase=1, screenBase=0.
  mem.Write16(0x04000008u, (uint16_t)(1u | (1u << 2)));

  // Palettes: BG idx1 = green, OBJ idx1 = red.
  mem.Write16(0x05000002u, 0x03E0u);
  mem.Write16(0x05000200u + 2u, 0x001Fu);

  // BG0 tilemap row 0 -> tile 1 everywhere.
  for (uint32_t tx = 0; tx < 32; ++tx) {
    mem.Write16(0x06000000u + tx * 2u, 0x0001u);
  }
  // BG0 tile 1 row 0 -> palette index 1.
  mem.Write16(0x06004000u + 1u * 32u + 0u, 0x1111u);
  mem.Write16(0x06004000u + 1u * 32u + 2u, 0x1111u);

  // Sprite 0: 8x8, 4bpp, at (0,0), tileIndex=0, priority=2.
  const uint16_t obj_attr0 = 0u;
  const uint16_t obj_attr1 = 0u;
  const uint16_t obj_attr2 = (uint16_t)(2u << 10);
  TestUtil::WriteOam16(mem, 0, obj_attr0);
  TestUtil::WriteOam16(mem, 2, obj_attr1);
  TestUtil::WriteOam16(mem, 4, obj_attr2);

  // OBJ tile 0 row 0 -> palette index 1 (red).
  mem.Write16(0x06010000u + 0u, 0x1111u);
  mem.Write16(0x06010000u + 2u, 0x1111u);

  // Exit forced blank and render.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x1000u));
  ppu.Update(960);
  ppu.SwapBuffers();

  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, ObjPriority1_TiesWithBgPriority1_DrawsInFront) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Forced blank for setup.
  mem.Write16(0x04000000u, 0x0080u);

  // Disable all sprites by default.
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // Mode 0, BG0 enabled, OBJ enabled.
  // BG0 priority = 1, charBase=1, screenBase=0.
  mem.Write16(0x04000008u, (uint16_t)(1u | (1u << 2)));

  // Palettes: BG idx1 = green, OBJ idx1 = red.
  mem.Write16(0x05000002u, 0x03E0u);
  mem.Write16(0x05000200u + 2u, 0x001Fu);

  // BG0 tilemap row 0 -> tile 1 everywhere.
  for (uint32_t tx = 0; tx < 32; ++tx) {
    mem.Write16(0x06000000u + tx * 2u, 0x0001u);
  }
  // BG0 tile 1 row 0 -> palette index 1.
  mem.Write16(0x06004000u + 1u * 32u + 0u, 0x1111u);
  mem.Write16(0x06004000u + 1u * 32u + 2u, 0x1111u);

  // Sprite 0: 8x8, 4bpp, at (0,0), tileIndex=0, priority=1 (ties BG0).
  const uint16_t obj_attr0 = 0u;
  const uint16_t obj_attr1 = 0u;
  const uint16_t obj_attr2 = (uint16_t)(1u << 10);
  TestUtil::WriteOam16(mem, 0, obj_attr0);
  TestUtil::WriteOam16(mem, 2, obj_attr1);
  TestUtil::WriteOam16(mem, 4, obj_attr2);

  // OBJ tile 0 row 0 -> palette index 1 (red).
  mem.Write16(0x06010000u + 0u, 0x1111u);
  mem.Write16(0x06010000u + 2u, 0x1111u);

  // Exit forced blank and render.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x1000u));
  ppu.Update(960);
  ppu.SwapBuffers();

  // GBATEK: OBJ is drawn on top of BG when priorities are equal.
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, ObjOverlapSamePriority_LowerOamIndexWins) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Forced blank for setup.
  mem.Write16(0x04000000u, 0x0080u);

  // Disable all sprites by default.
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // OBJ palette: idx1 = red, idx2 = green.
  mem.Write16(0x05000200u + 2u, 0x001Fu);
  mem.Write16(0x05000200u + 4u, 0x03E0u);

  // Tile 0 row 0 => palette idx 1 (red).
  mem.Write16(0x06010000u + 0u, 0x1111u);
  mem.Write16(0x06010000u + 2u, 0x1111u);

  // Tile 1 row 0 => palette idx 2 (green).
  mem.Write16(0x06010000u + 32u + 0u, 0x2222u);
  mem.Write16(0x06010000u + 32u + 2u, 0x2222u);

  // Sprite 0 (OAM index 0): tile 0, prio 0, at (0,0).
  TestUtil::WriteOam16(mem, 0u * 8u + 0u, 0u);
  TestUtil::WriteOam16(mem, 0u * 8u + 2u, 0u);
  TestUtil::WriteOam16(mem, 0u * 8u + 4u, 0u);

  // Sprite 1 (OAM index 1): tile 1, prio 0, same position.
  TestUtil::WriteOam16(mem, 1u * 8u + 0u, 0u);
  TestUtil::WriteOam16(mem, 1u * 8u + 2u, 0u);
  TestUtil::WriteOam16(mem, 1u * 8u + 4u, 1u);

  // Exit forced blank (OBJ enabled) and render.
  mem.Write16(0x04000000u, 0x1000u);
  ppu.Update(960);
  ppu.SwapBuffers();

  // GBATEK: lower OAM index has higher priority among overlapping OBJs.
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, OamWritesBlockedDuringVisiblePeriod) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Forced blank for setup (OBJ enabled).
  mem.Write16(0x04000000u, 0x1080u);

  // Disable all sprites.
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // OBJ palette idx1 = red; tile 0 draws red at (0,0).
  mem.Write16(0x05000200u + 2u, 0x001Fu);
  mem.Write16(0x06010000u + 0u, 0x1111u);
  mem.Write16(0x06010000u + 2u, 0x1111u);

  // Exit forced blank.
  mem.Write16(0x04000000u, 0x1000u);

  // Advance into visible period of scanline 0 (scanline 0 has already been
  // rendered).
  ppu.Update(100);

  // Attempt to enable sprite 0 for scanline 1 during the visible period.
  // This should be blocked.
  TestUtil::WriteOam16(mem, 0u, 1u); // attr0: y=1, normal OBJ
  TestUtil::WriteOam16(mem, 2u, 0u); // attr1: x=0
  TestUtil::WriteOam16(mem, 4u, 0u); // attr2: tile=0, prio=0

  // Finish scanline 0 without rendering scanline 1 yet.
  ppu.Update(1232 - 100);

  // Render scanline 1.
  ppu.Update(960);
  ppu.SwapBuffers();

  const size_t idx1 = (size_t)1 * (size_t)PPU::SCREEN_WIDTH;
  EXPECT_EQ(ppu.GetFramebuffer()[idx1 + 0], 0xFF000000u);
}

TEST(PPUTest, OamWritesDuringHBlankRequireHBlankIntervalFree) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Forced blank for setup (OBJ enabled).
  mem.Write16(0x04000000u, 0x1080u);

  // Disable all sprites.
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // OBJ palette idx1 = red; tile 0 draws red at (0,0).
  mem.Write16(0x05000200u + 2u, 0x001Fu);
  mem.Write16(0x06010000u + 0u, 0x1111u);
  mem.Write16(0x06010000u + 2u, 0x1111u);

  // Exit forced blank.
  mem.Write16(0x04000000u, 0x1000u);

  // Reach HBlank of scanline 0.
  ppu.Update(960);

  // With DISPCNT bit5 clear, this HBlank write should be blocked.
  TestUtil::WriteOam16(mem, 0u, 1u);
  TestUtil::WriteOam16(mem, 2u, 0u);
  TestUtil::WriteOam16(mem, 4u, 0u);

  // Finish scanline 0 without rendering scanline 1.
  ppu.Update(1232 - 960);

  // Render scanline 1.
  ppu.Update(960);
  ppu.SwapBuffers();

  const size_t idx1 = (size_t)1 * (size_t)PPU::SCREEN_WIDTH;
  EXPECT_EQ(ppu.GetFramebuffer()[idx1 + 0], 0xFF000000u);

  // Now enable H-Blank Interval Free and retry on the next line's HBlank.
  // Reset back to scanline 0 with a fresh PPU instance.
  GBAMemory mem2;
  mem2.Reset();
  PPU ppu2(mem2);

  mem2.Write16(0x04000000u, 0x1080u);
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem2, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem2, base + 2u, 0u);
    TestUtil::WriteOam16(mem2, base + 4u, 0u);
  }
  mem2.Write16(0x05000200u + 2u, 0x001Fu);
  mem2.Write16(0x06010000u + 0u, 0x1111u);
  mem2.Write16(0x06010000u + 2u, 0x1111u);

  // DISPCNT: OBJ enable + H-Blank Interval Free.
  mem2.Write16(0x04000000u, (uint16_t)(0x1000u | 0x0020u));

  // Reach HBlank of scanline 0.
  ppu2.Update(960);

  // HBlank write should be permitted now.
  TestUtil::WriteOam16(mem2, 0u, 1u);
  TestUtil::WriteOam16(mem2, 2u, 0u);
  TestUtil::WriteOam16(mem2, 4u, 0u);

  // Finish scanline 0; then render scanline 1.
  ppu2.Update(1232 - 960);
  ppu2.Update(960);
  ppu2.SwapBuffers();

  const size_t idx1b = (size_t)1 * (size_t)PPU::SCREEN_WIDTH;
  EXPECT_EQ(ppu2.GetFramebuffer()[idx1b + 0], TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, VramWritesBlockedDuringVisible_AllowedDuringHBlank) {
  GBAMemory mem;
  mem.Reset();
  PPU ppu(mem);

  // Forced blank for setup.
  mem.Write16(0x04000000u, 0x0080u);
  mem.Write16(0x06000000u, 0x0000u);

  // Exit forced blank.
  mem.Write16(0x04000000u, 0x0000u);

  // Enter visible period of scanline 0.
  ppu.Update(10);
  mem.Write16(0x06000000u, 0x1234u);
  EXPECT_EQ(mem.Read16(0x06000000u), 0x0000u);

  // Enter HBlank of scanline 0.
  ppu.Update(960 - 10);
  mem.Write16(0x06000000u, 0x1234u);
  EXPECT_EQ(mem.Read16(0x06000000u), 0x1234u);
}

TEST(PPUTest, PaletteWritesBlockedDuringVisible_AllowedDuringHBlank) {
  GBAMemory mem;
  mem.Reset();
  PPU ppu(mem);

  // Forced blank for setup.
  mem.Write16(0x04000000u, 0x0080u);
  mem.Write16(0x05000000u, 0x0000u);

  // Exit forced blank.
  mem.Write16(0x04000000u, 0x0000u);

  // Enter visible period of scanline 0.
  ppu.Update(10);
  mem.Write16(0x05000000u, 0x7FFFu);
  EXPECT_EQ(mem.Read16(0x05000000u), 0x0000u);

  // Enter HBlank of scanline 0.
  ppu.Update(960 - 10);
  mem.Write16(0x05000000u, 0x7FFFu);
  EXPECT_EQ(mem.Read16(0x05000000u), 0x7FFFu);
}

TEST(PPUTest, VramWritesAllowedDuringVBlank) {
  GBAMemory mem;
  mem.Reset();
  PPU ppu(mem);

  // Forced blank for setup.
  mem.Write16(0x04000000u, 0x0080u);
  mem.Write16(0x06000000u, 0x0000u);

  // Exit forced blank.
  mem.Write16(0x04000000u, 0x0000u);

  // Advance to start of VBlank (scanline 160).
  ppu.Update(1232 * 160);
  mem.Write16(0x06000000u, 0xBEEFu);
  EXPECT_EQ(mem.Read16(0x06000000u), 0xBEEFu);
}

TEST(PPUTest, PaletteWritesAllowedDuringVBlank) {
  GBAMemory mem;
  mem.Reset();
  PPU ppu(mem);

  // Forced blank for setup.
  mem.Write16(0x04000000u, 0x0080u);
  mem.Write16(0x05000000u, 0x0000u);

  // Exit forced blank.
  mem.Write16(0x04000000u, 0x0000u);

  // Advance to start of VBlank (scanline 160).
  ppu.Update(1232 * 160);
  mem.Write16(0x05000000u, 0x1234u);
  EXPECT_EQ(mem.Read16(0x05000000u), 0x1234u);
}

TEST(PPUTest, TextBg4bpp_TilemapPaletteBank_SelectsCorrectBgPalette) {
  GBAMemory mem;
  mem.Reset();
  PPU ppu(mem);

  // Forced blank for setup.
  mem.Write16(0x04000000u, 0x0080u);

  // BG palette: bank0 idx1=red, bank1 idx1=green.
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000022u, 0x03E0u);

  // BG0CNT: priority0, charBase=1, screenBase=0, 4bpp, size0.
  mem.Write16(0x04000008u, (uint16_t)(0u | (1u << 2) | (0u << 8) | (0u << 14)));

  // Tilemap entry at (0,0): tile 1, palette bank 1.
  mem.Write16(0x06000000u + 0u, (uint16_t)(1u | (1u << 12)));

  // Tile 1 (charBase=1 => 0x06004000), row0 pixel0 uses color index 1.
  const uint32_t tileBase = 0x06004000u;
  const uint32_t tile1 = tileBase + 1u * 32u;
  mem.Write16(tile1 + 0u, 0x0001u);
  mem.Write16(tile1 + 2u, 0x0000u);

  // Enable BG0, exit forced blank.
  mem.Write16(0x04000000u, 0x0100u);
  ppu.Update(960);
  ppu.SwapBuffers();

  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x03E0u));
}

TEST(PPUTest, TextBg4bpp_TilemapHFlip_Bit10FlipsTilePixels) {
  GBAMemory mem;
  mem.Reset();
  PPU ppu(mem);

  // Forced blank for setup.
  mem.Write16(0x04000000u, 0x0080u);

  // BG palette: idx1=red, idx2=green.
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x03E0u);

  // BG0CNT: priority0, charBase=1, screenBase=0, 4bpp, size0.
  mem.Write16(0x04000008u, (uint16_t)(0u | (1u << 2)));

  // Tilemap entry (0,0): tile 1 with HFlip.
  mem.Write16(0x06000000u + 0u, (uint16_t)(1u | (1u << 10)));

  // Tile 1 row0: pixel0=color1, pixel7=color2.
  const uint32_t tile1 = 0x06004000u + 1u * 32u;
  mem.Write16(tile1 + 0u, 0x0001u); // bytes: 01 00
  mem.Write16(tile1 + 2u, 0x2000u); // bytes: 00 20

  // Enable BG0, exit forced blank.
  mem.Write16(0x04000000u, 0x0100u);
  ppu.Update(960);
  ppu.SwapBuffers();

  // With HFlip, x=0 samples original x=7 => idx2 (green).
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x03E0u));
}

TEST(PPUTest, TextBg4bpp_TilemapVFlip_Bit11FlipsTileRows) {
  GBAMemory mem;
  mem.Reset();
  PPU ppu(mem);

  // Forced blank for setup.
  mem.Write16(0x04000000u, 0x0080u);

  // BG palette: idx1=red, idx2=green.
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x03E0u);

  // BG0CNT: priority0, charBase=1, screenBase=0, 4bpp, size0.
  mem.Write16(0x04000008u, (uint16_t)(0u | (1u << 2)));

  // Tilemap entry (0,0): tile 1 with VFlip.
  mem.Write16(0x06000000u + 0u, (uint16_t)(1u | (1u << 11)));

  const uint32_t tile1 = 0x06004000u + 1u * 32u;
  // Row0: pixel0 = idx1.
  mem.Write16(tile1 + 0u, 0x0001u);
  mem.Write16(tile1 + 2u, 0x0000u);
  // Row7: pixel0 = idx2.
  mem.Write16(tile1 + 7u * 4u + 0u, 0x0002u);
  mem.Write16(tile1 + 7u * 4u + 2u, 0x0000u);

  // Enable BG0, exit forced blank.
  mem.Write16(0x04000000u, 0x0100u);
  ppu.Update(960);
  ppu.SwapBuffers();

  // With VFlip, scanline0 samples original row7 => idx2 (green).
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x03E0u));
}

TEST(PPUTest, TextBg_CharBaseBlock_SelectsCorrectTileDataRegion) {
  GBAMemory mem;
  mem.Reset();
  PPU ppu(mem);

  // Forced blank for setup.
  mem.Write16(0x04000000u, 0x0080u);

  // BG palette: idx1=red, idx2=green.
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x03E0u);

  // Tilemap entry (0,0): tile 1.
  mem.Write16(0x06000000u + 0u, 0x0001u);

  // Put tile 1 in charBase=0 as red, and in charBase=1 as green.
  const uint32_t tile1_cb0 = 0x06000000u + 1u * 32u;
  mem.Write16(tile1_cb0 + 0u, 0x0001u);
  mem.Write16(tile1_cb0 + 2u, 0x0000u);
  const uint32_t tile1_cb1 = 0x06004000u + 1u * 32u;
  mem.Write16(tile1_cb1 + 0u, 0x0002u);
  mem.Write16(tile1_cb1 + 2u, 0x0000u);

  // BG0CNT: charBase=1, screenBase=0.
  mem.Write16(0x04000008u, (uint16_t)(0u | (1u << 2)));

  // Enable BG0, exit forced blank.
  mem.Write16(0x04000000u, 0x0100u);
  ppu.Update(960);
  ppu.SwapBuffers();

  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x03E0u));
}

TEST(PPUTest, TextBg_ScreenSize3_UsesCorrectHorizontalScreenBlock) {
  GBAMemory mem;
  mem.Reset();
  PPU ppu(mem);

  // Forced blank for setup.
  mem.Write16(0x04000000u, 0x0080u);

  // BG palette: idx1=red, idx2=green.
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x03E0u);

  // BG0CNT: priority0, charBase=1, screenBase=0, 4bpp, size=3 (512x512).
  mem.Write16(0x04000008u, (uint16_t)(0u | (1u << 2) | (0u << 8) | (3u << 14)));

  // Tilemap block 0 (top-left) entry (0,0) => tile 1.
  mem.Write16(0x06000000u + 0u, 0x0001u);
  // Tilemap block 1 (top-right) base is +0x800 => entry (0,0) => tile 2.
  mem.Write16(0x06000000u + 0x0800u + 0u, 0x0002u);

  // Tile 1 => red, tile 2 => green in charBase=1.
  const uint32_t tileBase = 0x06004000u;
  mem.Write16(tileBase + 1u * 32u + 0u, 0x0001u);
  mem.Write16(tileBase + 1u * 32u + 2u, 0x0000u);
  mem.Write16(tileBase + 2u * 32u + 0u, 0x0002u);
  mem.Write16(tileBase + 2u * 32u + 2u, 0x0000u);

  // Scroll X by 256 so x=0 falls into the right-hand screen block.
  mem.Write16(0x04000010u, 256u);

  // Enable BG0, exit forced blank.
  mem.Write16(0x04000000u, 0x0100u);
  ppu.Update(960);
  ppu.SwapBuffers();

  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x03E0u));
}

TEST(PPUTest, ObjAffine_UsesAffineIndexFromAttr1Bits9To13) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Forced blank for setup.
  mem.Write16(0x04000000u, 0x0080u);

  // Disable all sprites by default.
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // OBJ palette idx1 = red.
  mem.Write16(0x05000200u + 2u, 0x001Fu);

  // OBJ tile 0 row 0: only pixel0 is color 1; everything else is transparent.
  mem.Write16(0x06010000u + 0u, 0x0001u);
  mem.Write16(0x06010000u + 2u, 0x0000u);

  // Sprite 0: affine enabled, 8x8, at (0,0), tileIndex=0.
  const uint16_t attr0 = (uint16_t)(0u | (1u << 8));
  const uint16_t affineIndex = 1u;
  const uint16_t attr1 = (uint16_t)(0u | (affineIndex << 9));
  const uint16_t attr2 = 0u;
  TestUtil::WriteOam16(mem, 0, attr0);
  TestUtil::WriteOam16(mem, 2, attr1);
  TestUtil::WriteOam16(mem, 4, attr2);

  // Param set 0: identity.
  const uint32_t base0 = 0x07000006u + 0u * 32u;
  mem.Write16(base0 + 0u, 0x0100u);  // pa
  mem.Write16(base0 + 8u, 0x0000u);  // pb
  mem.Write16(base0 + 16u, 0x0000u); // pc
  mem.Write16(base0 + 24u, 0x0100u); // pd

  // Param set 1: pa=0, pb=1.0 (x depends on y), so on scanline 0 all x sample
  // spriteX=0 and should be red.
  const uint32_t base1 = 0x07000006u + 1u * 32u;
  mem.Write16(base1 + 0u, 0x0000u);  // pa
  mem.Write16(base1 + 8u, 0x0100u);  // pb
  mem.Write16(base1 + 16u, 0x0000u); // pc
  mem.Write16(base1 + 24u, 0x0100u); // pd

  // Exit forced blank and render.
  mem.Write16(0x04000000u, 0x1000u);
  ppu.Update(960);
  ppu.SwapBuffers();

  // If the affine index were ignored (using identity), x=7 would be
  // transparent.
  EXPECT_EQ(ppu.GetFramebuffer()[7], TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, ObjAffineDoubleSize_CentersSpriteInDoubledBounds) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Forced blank for setup.
  mem.Write16(0x04000000u, 0x0080u);

  // Disable all sprites by default.
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // OBJ palette idx1 = red.
  mem.Write16(0x05000200u + 2u, 0x001Fu);

  // OBJ tile 0 row 0: pixels 0..7 all color 1.
  mem.Write16(0x06010000u + 0u, 0x1111u);
  mem.Write16(0x06010000u + 2u, 0x1111u);

  // Sprite 0: affine + double-size, 8x8, at (0,0), tileIndex=0.
  const uint16_t attr0 = (uint16_t)(0u | (1u << 8) | (1u << 9));
  const uint16_t attr1 = 0u; // affineIndex=0
  const uint16_t attr2 = 0u;
  TestUtil::WriteOam16(mem, 0, attr0);
  TestUtil::WriteOam16(mem, 2, attr1);
  TestUtil::WriteOam16(mem, 4, attr2);

  // Param set 0: identity.
  const uint32_t base0 = 0x07000006u;
  mem.Write16(base0 + 0u, 0x0100u);  // pa
  mem.Write16(base0 + 8u, 0x0000u);  // pb
  mem.Write16(base0 + 16u, 0x0000u); // pc
  mem.Write16(base0 + 24u, 0x0100u); // pd

  // Exit forced blank and render.
  mem.Write16(0x04000000u, 0x1000u);
  TestUtil::RenderToScanlineHBlank(ppu, 4);

  // With double-size, identity mapping centers the 8x8 sprite within a 16x16
  // bounding box (i.e., shifted right by 4).
  EXPECT_NE(TestUtil::GetPixel(ppu, 0, 4), TestUtil::ARGBFromBGR555(0x001F));
  EXPECT_EQ(TestUtil::GetPixel(ppu, 4, 4), TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, ObjVramUpperWindowMirrorsToObjRegion) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // DISPCNT: mode 0, OBJ enable, OBJ mapping = 2D (bit6=0)
  mem.Write16(0x04000000u, 0x1080u);

  // OBJ palette: index 1 = visible color
  mem.Write16(0x05000200u + 2u, 0x001Fu);

  // Sprite 0: 64x64, 4bpp, at (0,0), tileIndex=1023.
  const uint16_t attr0 = 0u;                   // y=0, 4bpp, square
  const uint16_t attr1 = (uint16_t)(3u << 14); // size=3 => 64x64
  const uint16_t attr2 = 1023u;                // tileIndex=1023
  TestUtil::WriteOam16(mem, 0, attr0);
  TestUtil::WriteOam16(mem, 2, attr1);
  TestUtil::WriteOam16(mem, 4, attr2);

  // For scanline 56 (spriteY=56 => ty=7), computed tileNum=1247 (mirrors to
  // 223)
  const uint32_t tileBase = 0x06010000u;
  const uint32_t mirroredTileNum = 1247u - 1024u;
  const uint32_t mirroredAddr = tileBase + mirroredTileNum * 32u;
  mem.Write16(mirroredAddr, 0x0001u);

  // Exit Forced Blank before rendering.
  mem.Write16(0x04000000u, 0x1000u);

  ppu.Update(1232 * 56 + 960);
  ppu.SwapBuffers();

  const auto fb = ppu.GetFramebuffer();
  const size_t idx = (size_t)56 * (size_t)PPU::SCREEN_WIDTH;
  ASSERT_GT(fb.size(), idx);
  EXPECT_EQ(fb[idx + 0], TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, UnalignedIoWrite16AlignsToEvenAddress) {
  GBAMemory mem;
  mem.Reset();

  mem.Write16(0x04000041u, 0xFFFEu);
  EXPECT_EQ(mem.Read16(0x04000040u), 0xFFFEu);
}

TEST(PPUTest, UnalignedVramWritesAlign) {
  GBAMemory mem;
  mem.Reset();

  // Allow VRAM writes in this unit test.
  mem.Write16(0x04000000u, 0x0080u);

  mem.Write16(0x06000001u, 0xBBAAu);
  EXPECT_EQ(mem.Read8(0x06000000u), 0xAAu);
  EXPECT_EQ(mem.Read8(0x06000001u), 0xBBu);

  mem.Reset();

  // Allow VRAM writes in this unit test.
  mem.Write16(0x04000000u, 0x0080u);

  mem.Write32(0x06000002u, 0xDDCCBBAAu);
  EXPECT_EQ(mem.Read8(0x06000000u), 0xAAu);
  EXPECT_EQ(mem.Read8(0x06000001u), 0xBBu);
  EXPECT_EQ(mem.Read8(0x06000002u), 0xCCu);
  EXPECT_EQ(mem.Read8(0x06000003u), 0xDDu);
}

TEST(PPUTest, PaletteWrite8_DuplicatesByteToHalfword) {
  GBAMemory mem;
  mem.Reset();

  // Palette is on a 16-bit bus; 8-bit writes duplicate the byte.
  mem.Write8(0x05000001u, 0x12u);
  EXPECT_EQ(mem.Read8(0x05000000u), 0x12u);
  EXPECT_EQ(mem.Read8(0x05000001u), 0x12u);

  mem.Write8(0x05000000u, 0xABu);
  EXPECT_EQ(mem.Read8(0x05000000u), 0xABu);
  EXPECT_EQ(mem.Read8(0x05000001u), 0xABu);
}

TEST(PPUTest, VramWrite8_BgDuplicates_ObjIgnored) {
  GBAMemory mem;
  mem.Reset();

  // Mode 0 + Forced Blank: BG byte writes duplicate; OBJ byte writes are
  // ignored.
  mem.Write16(0x04000000u, 0x0080u);
  mem.Write8(0x06000001u, 0x34u);
  EXPECT_EQ(mem.Read8(0x06000000u), 0x34u);
  EXPECT_EQ(mem.Read8(0x06000001u), 0x34u);

  mem.Write8(0x06010001u, 0x7Au);
  EXPECT_EQ(mem.Read8(0x06010000u), 0x00u);
  EXPECT_EQ(mem.Read8(0x06010001u), 0x00u);

  // Bitmap mode 4 + Forced Blank: BG byte writes still duplicate; OBJ byte
  // writes are ignored.
  mem.Reset();
  mem.Write16(0x04000000u, 0x0084u);

  mem.Write8(0x06000001u, 0xCDu);
  EXPECT_EQ(mem.Read8(0x06000000u), 0xCDu);
  EXPECT_EQ(mem.Read8(0x06000001u), 0xCDu);

  mem.Write8(0x06014001u, 0x5Au);
  EXPECT_EQ(mem.Read8(0x06014000u), 0x00u);
  EXPECT_EQ(mem.Read8(0x06014001u), 0x00u);
}

TEST(PPUTest, OamWrite8_IsIgnored) {
  GBAMemory mem;
  mem.Reset();

  mem.Write8(0x07000000u, 0x77u);
  EXPECT_EQ(mem.Read8(0x07000000u), 0x00u);
  EXPECT_EQ(mem.Read8(0x07000001u), 0x00u);

  // Halfword write should still work (OAM is not read-only), so this also
  // guards against accidentally treating all OAM writes as ignored.
  mem.Write16(0x07000000u, 0xBBAAu);
  EXPECT_EQ(mem.Read8(0x07000000u), 0xAAu);
  EXPECT_EQ(mem.Read8(0x07000001u), 0xBBu);
}

TEST(PPUTest, VramUpperWindowMirrorsObjRegion_ForReadWriteMultipleSizes) {
  GBAMemory mem;
  mem.Reset();

  // Allow VRAM writes in this unit test.
  // Use bitmap mode so the mirrored window falls into BG VRAM (0x06010000+).
  mem.Write16(0x04000000u, 0x0084u);

  // 0x06018000-0x0601FFFF mirrors to 0x06010000-0x06017FFF.
  const uint32_t upper = 0x06018000u;
  const uint32_t lower = 0x06010000u;

  mem.Write16(upper, 0x2211u);
  EXPECT_EQ(mem.Read16(lower), 0x2211u);
  EXPECT_EQ(mem.Read8(lower + 0u), 0x11u);
  EXPECT_EQ(mem.Read8(lower + 1u), 0x22u);

  // Word writes to VRAM are aligned on hardware; in this emulator the address
  // is forced to a 4-byte boundary for VRAM/OAM/Palette.
  mem.Write32(upper + 0u, 0xDDCCBBAAu);
  EXPECT_EQ(mem.Read32(lower + 0u), 0xDDCCBBAAu);

  // 8-bit writes should also apply through the mirror.
  // In this mode, the mirrored window lands in BG VRAM, so byte writes
  // duplicate.
  mem.Write8(upper + 7u, 0x5Au);
  EXPECT_EQ(mem.Read8(lower + 6u), 0x5Au);
  EXPECT_EQ(mem.Read8(lower + 7u), 0x5Au);
}

TEST(PPUTest, TextBgScreenSize1SelectsSecondHorizontalScreenBlock) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  mem.Write16(0x04000000u, 0x0180u);
  mem.Write16(0x04000008u, 0x4000u);
  mem.Write16(0x04000010u, 256u);
  mem.Write16(0x04000012u, 0u);
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x06000000u + 2048u, 0x0001u);
  mem.Write16(0x06000000u + 1u * 32u, 0x0001u);

  // Exit Forced Blank before rendering.
  mem.Write16(0x04000000u, 0x0100u);

  ppu.Update(960);
  ppu.SwapBuffers();

  const auto fb = ppu.GetFramebuffer();
  ASSERT_GE(fb.size(), (size_t)PPU::SCREEN_WIDTH);
  EXPECT_EQ(fb[0], TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, TextBgScreenSize2SelectsSecondVerticalScreenBlock) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  mem.Write16(0x04000000u, 0x0180u);
  mem.Write16(0x04000008u, 0x8000u);
  mem.Write16(0x04000010u, 0u);
  mem.Write16(0x04000012u, 256u);
  mem.Write16(0x05000002u, 0x03E0u);
  // GBATEK: for ScreenSize=2 (32x64 tiles), the second vertical screenblock
  // is screen base + 2 (not +1) => +0x1000 bytes.
  mem.Write16(0x06000000u + 4096u, 0x0001u);
  mem.Write16(0x06000000u + 1u * 32u, 0x0001u);

  // Exit Forced Blank before rendering.
  mem.Write16(0x04000000u, 0x0100u);

  ppu.Update(960);
  ppu.SwapBuffers();

  const auto fb = ppu.GetFramebuffer();
  ASSERT_GE(fb.size(), (size_t)PPU::SCREEN_WIDTH);
  EXPECT_EQ(fb[0], TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, TextBgMosaicRepeatsPixelsHorizontally) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  mem.Write16(0x04000000u, 0x0180u);
  mem.Write16(0x04000008u, 0x0040u);
  mem.Write16(0x0400004Cu, 0x0001u);
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x03E0u);
  mem.Write16(0x06000000u, 0x0001u);
  mem.Write16(0x06000000u + 1u * 32u, 0x2121u);

  // Exit Forced Blank before rendering.
  mem.Write16(0x04000000u, 0x0100u);

  ppu.Update(960);
  ppu.SwapBuffers();

  const auto fb = ppu.GetFramebuffer();
  ASSERT_GE(fb.size(), (size_t)PPU::SCREEN_WIDTH);
  EXPECT_EQ(fb[0], TestUtil::ARGBFromBGR555(0x001F));
  EXPECT_EQ(fb[1], TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, ObjMosaicRepeatsPixelsHorizontally) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  mem.Write16(0x04000000u, 0x1080u);
  mem.Write16(0x0400004Cu, 0x0100u);
  mem.Write16(0x05000200u + 2u, 0x001Fu);
  mem.Write16(0x05000200u + 4u, 0x03E0u);

  const uint16_t attr0 = (uint16_t)(0u | (1u << 12));
  const uint16_t attr1 = 0u;
  const uint16_t attr2 = 0u;
  TestUtil::WriteOam16(mem, 0, attr0);
  TestUtil::WriteOam16(mem, 2, attr1);
  TestUtil::WriteOam16(mem, 4, attr2);

  mem.Write16(0x06010000u + 0u, 0x0021u);

  // Exit Forced Blank before rendering.
  mem.Write16(0x04000000u, 0x1000u);

  ppu.Update(960);
  ppu.SwapBuffers();

  const auto fb = ppu.GetFramebuffer();
  ASSERT_GE(fb.size(), (size_t)PPU::SCREEN_WIDTH);
  EXPECT_EQ(fb[0], TestUtil::ARGBFromBGR555(0x001F));
  EXPECT_EQ(fb[1], TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, TextBgMosaicSize0DoesNotRepeatHorizontally) {
  GBAMemory mem;
  mem.Reset();

  // Mode 0, BG0 enable, Forced Blank for setup.
  mem.Write16(0x04000000u, 0x0180u);

  // BG0: mosaic enable (bit6), charBase=1, screenBase=0.
  mem.Write16(0x04000008u, (uint16_t)(0x0040u | (1u << 2) | (0u << 8)));

  // MOSAIC: BG H size=0 (=> size 1, no effect).
  mem.Write16(0x0400004Cu, 0x0000u);

  // Palette: idx1=red, idx2=green.
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x03E0u);

  // Screenblock 0 entry 0 uses tile 1.
  mem.Write16(0x06000000u, 0x0001u);

  // Tile 1 (4bpp) in charBase=1 => 0x06004000.
  // Row 0: pixel0=idx1 (red), pixel1=idx2 (green).
  // Packed nibbles: low nibble is pixel0, next nibble pixel1.
  mem.Write16(0x06004000u + 1u * 32u + 0u, 0x0021u);
  mem.Write16(0x06004000u + 1u * 32u + 2u, 0x0000u);

  // Exit Forced Blank before rendering.
  mem.Write16(0x04000000u, 0x0100u);

  PPU ppu(mem);
  TestUtil::RenderToScanlineHBlank(ppu, 0);
  EXPECT_EQ(TestUtil::GetPixel(ppu, 0, 0), TestUtil::ARGBFromBGR555(0x001F));
  EXPECT_EQ(TestUtil::GetPixel(ppu, 1, 0), TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, TextBgMosaicRepeatsPixelsVertically) {
  GBAMemory mem;
  mem.Reset();

  // Mode 0, BG0 enable, Forced Blank for setup.
  mem.Write16(0x04000000u, 0x0180u);

  // BG0: mosaic enable, charBase=1, screenBase=0.
  mem.Write16(0x04000008u, (uint16_t)(0x0040u | (1u << 2) | (0u << 8)));

  // MOSAIC: BG V size=1 (=> group size 2 scanlines).
  mem.Write16(0x0400004Cu, 0x0010u);

  // Palette: idx1=red, idx2=green.
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x03E0u);

  // Screenblock 0 entry 0 uses tile 1.
  mem.Write16(0x06000000u, 0x0001u);

  // Tile 1 in charBase=1 (0x06004000):
  // - Row 0 all idx1 (red)
  // - Row 2 all idx2 (green)
  const uint32_t tile1 = 0x06004000u + 1u * 32u;
  // Row 0.
  mem.Write16(tile1 + 0u, 0x1111u);
  mem.Write16(tile1 + 2u, 0x1111u);
  // Row 2.
  mem.Write16(tile1 + 8u, 0x2222u);
  mem.Write16(tile1 + 10u, 0x2222u);

  // Exit Forced Blank.
  mem.Write16(0x04000000u, 0x0100u);

  auto Sample = [&](int scanline) -> uint32_t {
    PPU ppu(mem);
    TestUtil::RenderToScanlineHBlank(ppu, scanline);
    return TestUtil::GetPixel(ppu, 0, scanline);
  };

  // With V mosaic size=2, y=1 samples source y=0.
  EXPECT_EQ(Sample(0), TestUtil::ARGBFromBGR555(0x001F));
  EXPECT_EQ(Sample(1), TestUtil::ARGBFromBGR555(0x001F));
  // y=2 samples source y=2.
  EXPECT_EQ(Sample(2), TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, ObjMosaicRepeatsPixelsVertically) {
  GBAMemory mem;
  mem.Reset();

  // Mode 0, OBJ enable, Forced Blank for setup.
  mem.Write16(0x04000000u, 0x1080u);

  // MOSAIC: OBJ V size=1 (=> group size 2 scanlines).
  mem.Write16(0x0400004Cu, 0x1000u);

  // OBJ palette: idx1=red, idx2=green.
  mem.Write16(0x05000200u + 2u, 0x001Fu);
  mem.Write16(0x05000200u + 4u, 0x03E0u);

  // Sprite 0: 8x8, 4bpp, mosaic enabled (attr0 bit12), at (0,0).
  const uint16_t attr0 = (uint16_t)(0u | (1u << 12));
  const uint16_t attr1 = 0u;
  const uint16_t attr2 = 0u;
  TestUtil::WriteOam16(mem, 0, attr0);
  TestUtil::WriteOam16(mem, 2, attr1);
  TestUtil::WriteOam16(mem, 4, attr2);

  // OBJ tile 0 in OBJ VRAM base 0x06010000:
  // - Row 0: idx1 (red)
  // - Row 2: idx2 (green)
  const uint32_t tile0 = 0x06010000u;
  // Row 0.
  mem.Write16(tile0 + 0u, 0x1111u);
  mem.Write16(tile0 + 2u, 0x1111u);
  // Row 2.
  mem.Write16(tile0 + 8u, 0x2222u);
  mem.Write16(tile0 + 10u, 0x2222u);

  // Exit Forced Blank.
  mem.Write16(0x04000000u, 0x1000u);

  auto Sample = [&](int scanline) -> uint32_t {
    PPU ppu(mem);
    TestUtil::RenderToScanlineHBlank(ppu, scanline);
    return TestUtil::GetPixel(ppu, 0, scanline);
  };

  EXPECT_EQ(Sample(0), TestUtil::ARGBFromBGR555(0x001F));
  EXPECT_EQ(Sample(1), TestUtil::ARGBFromBGR555(0x001F));
  EXPECT_EQ(Sample(2), TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, ObjTransparencyPaletteIndex0IsTransparent) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  mem.Write16(0x04000000u, 0x1180u); // mode 0, BG0 enable, OBJ enable
  mem.Write16(0x04000008u, (uint16_t)(3u | (1u << 2))); // BG0CNT charBase=1
  mem.Write16(0x05000002u, 0x03E0u);                    // BG pal idx1=green
  mem.Write16(0x06000000u, 0x0001u);                    // tile1
  mem.Write16(0x06004000u + 1u * 32u, 0x1111u);

  mem.Write16(0x05000200u + 2u, 0x001Fu); // OBJ pal idx1=red
  TestUtil::WriteOam16(mem, 0, 0u);
  TestUtil::WriteOam16(mem, 2, 0u);
  TestUtil::WriteOam16(mem, 4, 0u);
  mem.Write16(0x06010000u + 0u, 0x0010u); // pixel0=0, pixel1=1

  // Exit Forced Blank before rendering.
  mem.Write16(0x04000000u, 0x1100u);

  ppu.Update(960);
  ppu.SwapBuffers();

  const auto fb = ppu.GetFramebuffer();
  ASSERT_GE(fb.size(), (size_t)PPU::SCREEN_WIDTH);
  EXPECT_EQ(fb[0], TestUtil::ARGBFromBGR555(0x03E0));
  EXPECT_EQ(fb[1], TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, Obj4bppPaletteBankSelectsCorrectPalette) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  mem.Write16(0x04000000u, 0x1080u);            // OBJ enable
  mem.Write16(0x05000200u + 2u, 0x001Fu);       // bank0 idx1=red
  mem.Write16(0x05000200u + 32u + 2u, 0x03E0u); // bank1 idx1=green

  const uint16_t attr2 = (uint16_t)(0u | (1u << 12));
  TestUtil::WriteOam16(mem, 0, 0u);
  TestUtil::WriteOam16(mem, 2, 0u);
  TestUtil::WriteOam16(mem, 4, attr2);
  mem.Write16(0x06010000u + 0u, 0x0001u);

  // Exit Forced Blank before rendering.
  mem.Write16(0x04000000u, 0x1000u);

  ppu.Update(960);
  ppu.SwapBuffers();

  const auto fb = ppu.GetFramebuffer();
  ASSERT_GE(fb.size(), (size_t)PPU::SCREEN_WIDTH);
  EXPECT_EQ(fb[0], TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, Obj8bpp_IgnoresPaletteBankBitsInAttr2) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Mode 0, OBJ enable, Forced Blank for setup.
  mem.Write16(0x04000000u, 0x1080u);

  // OBJ palette:
  // - index 1 = red
  // - index 17 = green
  // If palette-bank bits (attr2[12..15]) were incorrectly applied to 8bpp,
  // index 1 might be treated like index (1 + bank*16).
  mem.Write16(0x05000200u + 2u * 1u, 0x001Fu);
  mem.Write16(0x05000200u + 2u * 17u, 0x03E0u);

  // Disable all sprites to avoid OAM reset-to-zero creating overlapping ones.
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // Sprite 0: 8x8, 8bpp (attr0 bit13), at (0,0), tileIndex=0.
  // Set palette-bank bits to 1 (should be ignored for 8bpp).
  const uint16_t attr0 = (uint16_t)(0u | (1u << 13));
  const uint16_t attr1 = 0u;
  const uint16_t attr2 = (uint16_t)(0u | (1u << 12));
  TestUtil::WriteOam16(mem, 0u, attr0);
  TestUtil::WriteOam16(mem, 2u, attr1);
  TestUtil::WriteOam16(mem, 4u, attr2);

  // OBJ tile 0 byte0 = palette index 1.
  TestUtil::WriteVramPackedByteViaHalfword(mem, 0x06010000u + 0u, 1u);

  // Exit Forced Blank.
  mem.Write16(0x04000000u, 0x1000u);

  ppu.Update(960);
  ppu.SwapBuffers();
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, ObjHFlip_Bit12FlipsTilePixels) {
  GBAMemory mem;
  mem.Reset();

  // Forced Blank for setup.
  mem.Write16(0x04000000u, 0x1180u); // mode0, BG0+OBJ enable, forced blank

  // BG0: charBase=1, screenBase=0.
  mem.Write16(0x04000008u, (uint16_t)(0u | (1u << 2) | (0u << 8)));

  // BG palette idx1 = blue.
  mem.Write16(0x05000002u, 0x7C00u);
  // BG tilemap entry 0 uses tile 1; tile 1 row 0 all idx1.
  mem.Write16(0x06000000u + 0u, 0x0001u);
  mem.Write16(0x06004000u + 1u * 32u + 0u, 0x1111u);
  mem.Write16(0x06004000u + 1u * 32u + 2u, 0x1111u);

  // OBJ palette idx1 = red, idx2 = green.
  mem.Write16(0x05000200u + 2u, 0x001Fu);
  mem.Write16(0x05000200u + 4u, 0x03E0u);

  // Disable all sprites.
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // Sprite 0: 8x8, 4bpp, hflip enabled (attr1 bit12), at (0,0).
  TestUtil::WriteOam16(mem, 0u, 0u);
  TestUtil::WriteOam16(mem, 2u, (uint16_t)(1u << 12));
  TestUtil::WriteOam16(mem, 4u, 0u);

  // OBJ tile 0 row 0 pixels: [1,1,1,1,1,1,1,2]
  // (so hflip makes pixel0 become idx2).
  mem.Write16(0x06010000u + 0u, 0x1111u);
  mem.Write16(0x06010000u + 2u, 0x2111u);

  // Exit forced blank and render.
  mem.Write16(0x04000000u, 0x1100u);
  PPU ppu(mem);
  ppu.Update(960);
  ppu.SwapBuffers();

  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, ObjVFlip_Bit13FlipsTileRows) {
  GBAMemory mem;
  mem.Reset();

  // Forced Blank for setup.
  mem.Write16(0x04000000u, 0x1080u);

  // OBJ palette idx1 = red, idx2 = green.
  mem.Write16(0x05000200u + 2u, 0x001Fu);
  mem.Write16(0x05000200u + 4u, 0x03E0u);

  // Disable all sprites.
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // Sprite 0: 8x8, 4bpp, vflip enabled (attr1 bit13), at (0,0).
  TestUtil::WriteOam16(mem, 0u, 0u);
  TestUtil::WriteOam16(mem, 2u, (uint16_t)(1u << 13));
  TestUtil::WriteOam16(mem, 4u, 0u);

  // OBJ tile 0:
  // - Row 0 all idx1 (red)
  // - Row 7 all idx2 (green)
  const uint32_t tile0 = 0x06010000u;
  // Row 0.
  mem.Write16(tile0 + 0u, 0x1111u);
  mem.Write16(tile0 + 2u, 0x1111u);
  // Row 7 starts at byte 28.
  mem.Write16(tile0 + 28u, 0x2222u);
  mem.Write16(tile0 + 30u, 0x2222u);

  // Exit Forced Blank.
  mem.Write16(0x04000000u, 0x1000u);
  PPU ppu(mem);

  // Render scanline 0. With vflip, this samples source row 7 => green.
  ppu.Update(960);
  ppu.SwapBuffers();
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, TextBg8bpp_TileBytesSelectCorrectBgPaletteIndex) {
  GBAMemory mem;
  mem.Reset();

  // Mode 0, BG0 enable, Forced Blank for setup.
  mem.Write16(0x04000000u, 0x0180u);

  // BG0CNT: 256 colors (bit7), charBase=0, screenBase=0.
  mem.Write16(0x04000008u, (uint16_t)(0u | (0u << 2) | (0u << 8) | (1u << 7)));

  // BG palette: idx1=red, idx2=green.
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x03E0u);

  // Screenblock 0 entry 0 uses tile 1.
  mem.Write16(0x06000000u, 0x0001u);

  // Tile 1 (8bpp) in charBase=0 (0x06000000). Row 0 bytes:
  // pixel0=1, pixel1=2.
  const uint32_t tile1 = 0x06000000u + 1u * 64u;
  TestUtil::WriteVramPackedByteViaHalfword(mem, tile1 + 0u, 1u);
  TestUtil::WriteVramPackedByteViaHalfword(mem, tile1 + 1u, 2u);

  // Exit Forced Blank and render scanline 0.
  mem.Write16(0x04000000u, 0x0100u);
  PPU ppu(mem);
  TestUtil::RenderToScanlineHBlank(ppu, 0);
  EXPECT_EQ(TestUtil::GetPixel(ppu, 0, 0), TestUtil::ARGBFromBGR555(0x001F));
  EXPECT_EQ(TestUtil::GetPixel(ppu, 1, 0), TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, TextBg8bpp_IgnoresTilemapPaletteBankBits) {
  GBAMemory mem;
  mem.Reset();

  // Mode 0, BG0 enable, Forced Blank for setup.
  mem.Write16(0x04000000u, 0x0180u);

  // BG0CNT: 256 colors (bit7), charBase=0, screenBase=0.
  mem.Write16(0x04000008u, (uint16_t)(0u | (0u << 2) | (0u << 8) | (1u << 7)));

  // BG palette: idx1=red, idx17=green.
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000002u + 2u * 16u, 0x03E0u);

  // Tilemap entry: tile 1 with palette bank bits set (bits12-15).
  // In 8bpp mode these bits must be ignored.
  mem.Write16(0x06000000u, (uint16_t)(0x0001u | (1u << 12)));

  // Tile 1 (8bpp) row0 pixel0 = palette index 1.
  const uint32_t tile1 = 0x06000000u + 1u * 64u;
  TestUtil::WriteVramPackedByteViaHalfword(mem, tile1 + 0u, 1u);

  // Exit forced blank.
  mem.Write16(0x04000000u, 0x0100u);
  PPU ppu(mem);
  TestUtil::RenderToScanlineHBlank(ppu, 0);
  EXPECT_EQ(TestUtil::GetPixel(ppu, 0, 0), TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, TextBg8bpp_TilemapHFlip_Bit10FlipsTilePixels) {
  GBAMemory mem;
  mem.Reset();

  // Mode 0, BG0 enable, Forced Blank.
  mem.Write16(0x04000000u, 0x0180u);

  // BG0CNT: 256 colors, charBase=0, screenBase=0.
  mem.Write16(0x04000008u, (uint16_t)(0u | (0u << 2) | (0u << 8) | (1u << 7)));

  // Palette idx1=red, idx2=green.
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x03E0u);

  // Tilemap entry with HFlip (bit10).
  mem.Write16(0x06000000u, (uint16_t)(0x0001u | (1u << 10)));

  // Tile 1 row0: pixel0=idx1, pixel7=idx2.
  const uint32_t tile1 = 0x06000000u + 1u * 64u;
  TestUtil::WriteVramPackedByteViaHalfword(mem, tile1 + 0u, 1u);
  TestUtil::WriteVramPackedByteViaHalfword(mem, tile1 + 7u, 2u);

  // Exit forced blank.
  mem.Write16(0x04000000u, 0x0100u);
  PPU ppu(mem);
  TestUtil::RenderToScanlineHBlank(ppu, 0);

  // With HFlip, screen pixel0 samples source pixel7 => idx2 (green).
  EXPECT_EQ(TestUtil::GetPixel(ppu, 0, 0), TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, TextBg8bpp_TilemapVFlip_Bit11FlipsTileRows) {
  GBAMemory mem;
  mem.Reset();

  // Mode 0, BG0 enable, Forced Blank.
  mem.Write16(0x04000000u, 0x0180u);
  mem.Write16(0x04000008u, (uint16_t)(0u | (0u << 2) | (0u << 8) | (1u << 7)));

  // Palette idx1=red, idx2=green.
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x03E0u);

  // Tilemap entry with VFlip (bit11).
  mem.Write16(0x06000000u, (uint16_t)(0x0001u | (1u << 11)));

  // Tile 1: row0 pixel0=idx1, row7 pixel0=idx2.
  const uint32_t tile1 = 0x06000000u + 1u * 64u;
  TestUtil::WriteVramPackedByteViaHalfword(mem, tile1 + 0u, 1u);
  TestUtil::WriteVramPackedByteViaHalfword(mem, tile1 + 7u * 8u + 0u, 2u);

  mem.Write16(0x04000000u, 0x0100u);
  PPU ppu(mem);
  TestUtil::RenderToScanlineHBlank(ppu, 0);

  // With VFlip, scanline 0 samples source row7 => idx2 (green).
  EXPECT_EQ(TestUtil::GetPixel(ppu, 0, 0), TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, Window_WIN0_Wraparound_LeftGreaterThanRight) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x2000u | 0x0080u));
  mem.Write16(0x04000008u, (uint16_t)(0u | (1u << 2)));
  mem.Write16(0x0400000Au, (uint16_t)(1u | (2u << 2) | (1u << 8)));
  mem.Write16(0x04000040u, (uint16_t)((200u << 8) | 40u));
  mem.Write16(0x04000044u, (uint16_t)((0u << 8) | 160u));
  mem.Write16(0x04000048u, 0x0001u);
  mem.Write16(0x0400004Au, 0x0002u);
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x03E0u);

  // Populate tilemaps for the whole first row so any sample X hits tile 1.
  for (uint32_t tx = 0; tx < 32; ++tx) {
    mem.Write16(0x06000000u + tx * 2u, 0x0001u);           // BG0 screenBase=0
    mem.Write16(0x06000000u + 0x0800u + tx * 2u, 0x0001u); // BG1 screenBase=1
  }

  // Populate tile 1 row 0 fully (8 pixels) for both BGs.
  mem.Write16(0x06004000u + 1u * 32u + 0u, 0x1111u);
  mem.Write16(0x06004000u + 1u * 32u + 2u, 0x1111u);
  mem.Write16(0x06008000u + 1u * 32u + 0u, 0x2222u);
  mem.Write16(0x06008000u + 1u * 32u + 2u, 0x2222u);

  // Exit Forced Blank before rendering.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x2000u));

  ppu.Update(960);
  ppu.SwapBuffers();

  const auto fb = ppu.GetFramebuffer();
  ASSERT_GE(fb.size(), (size_t)PPU::SCREEN_WIDTH);
  // GBATEK: X1>X2 produces a wraparound window (x>=X1 OR x<X2).
  // So WIN0 covers x in [200,240) U [0,40). Outside that range, WINOUT applies.
  EXPECT_EQ(fb[10], TestUtil::ARGBFromBGR555(0x001F));
  EXPECT_EQ(fb[210], TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, Blend_AlphaClamp_Uses5BitChannelClamp) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x0080u));
  mem.Write16(0x04000008u, (uint16_t)(0u | (1u << 2)));
  mem.Write16(0x0400000Au, (uint16_t)(1u | (2u << 2) | (1u << 8)));
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x06000000u + 0u, 0x0001u);
  mem.Write16(0x06000000u + 0x0800u, 0x0001u);
  mem.Write16(0x06004000u + 1u * 32u, 0x1111u);
  mem.Write16(0x06008000u + 1u * 32u, 0x1111u);
  mem.Write16(0x04000050u, (uint16_t)(0x0040u | 0x0001u | 0x0200u));
  mem.Write16(0x04000052u, 0x1010u);

  // Exit Forced Blank before rendering.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u));

  ppu.Update(960);
  ppu.SwapBuffers();

  const auto fb = ppu.GetFramebuffer();
  ASSERT_GE(fb.size(), (size_t)PPU::SCREEN_WIDTH);
  EXPECT_EQ(fb[0], TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, Window_DisablesColorEffectsWhenMasked) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x2000u | 0x0080u));
  mem.Write16(0x04000008u, (uint16_t)(0u | (1u << 2)));
  mem.Write16(0x0400000Au, (uint16_t)(1u | (2u << 2) | (1u << 8)));
  mem.Write16(0x04000040u, (uint16_t)((0u << 8) | 240u));
  mem.Write16(0x04000044u, (uint16_t)((0u << 8) | 160u));
  mem.Write16(0x04000048u, 0x0003u);
  mem.Write16(0x0400004Au, 0x003Fu);
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x7C00u);
  mem.Write16(0x06000000u + 0u, 0x0001u);
  mem.Write16(0x06000000u + 0x0800u, 0x0001u);
  mem.Write16(0x06004000u + 1u * 32u, 0x1111u);
  mem.Write16(0x06008000u + 1u * 32u, 0x2222u);
  mem.Write16(0x04000050u, (uint16_t)(0x0040u | 0x0001u | 0x0200u));
  mem.Write16(0x04000052u, 0x0808u);

  // Exit Forced Blank before rendering.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x2000u));

  ppu.Update(960);
  ppu.SwapBuffers();

  const auto fb = ppu.GetFramebuffer();
  ASSERT_GE(fb.size(), (size_t)PPU::SCREEN_WIDTH);
  EXPECT_EQ(fb[0], TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, WindowPriority_WIN0OverridesWIN1WhenOverlapping) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Mode 0, BG0+BG1 enable, WIN0+WIN1 enable.
  mem.Write16(0x04000000u,
              (uint16_t)(0x0100u | 0x0200u | 0x2000u | 0x4000u | 0x0080u));

  // BG0 priority 0, charBase=1, screenBase=0
  mem.Write16(0x04000008u, (uint16_t)(0u | (1u << 2)));
  // BG1 priority 1, charBase=2, screenBase=1
  mem.Write16(0x0400000Au, (uint16_t)(1u | (2u << 2) | (1u << 8)));

  // Both windows cover the whole screen.
  mem.Write16(0x04000040u, (uint16_t)((0u << 8) | 240u));
  mem.Write16(0x04000044u, (uint16_t)((0u << 8) | 160u));
  mem.Write16(0x04000042u, (uint16_t)((0u << 8) | 240u));
  mem.Write16(0x04000046u, (uint16_t)((0u << 8) | 160u));

  // WININ: WIN0 enables BG0 only, WIN1 enables BG1 only.
  mem.Write16(0x04000048u, (uint16_t)((0x0001u) | (0x0002u << 8)));
  // WINOUT: irrelevant (pixel is inside both windows).
  mem.Write16(0x0400004Au, 0x003Fu);

  // Palettes: BG0 idx1=red, BG1 idx1=green.
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x03E0u);

  // Fill tilemaps row 0 and tile 1 row 0 for both BGs.
  for (uint32_t tx = 0; tx < 32; ++tx) {
    mem.Write16(0x06000000u + tx * 2u, 0x0001u);
    mem.Write16(0x06000000u + 0x0800u + tx * 2u, 0x0001u);
  }
  mem.Write16(0x06004000u + 1u * 32u + 0u, 0x1111u);
  mem.Write16(0x06004000u + 1u * 32u + 2u, 0x1111u);
  mem.Write16(0x06008000u + 1u * 32u + 0u, 0x1111u);
  mem.Write16(0x06008000u + 1u * 32u + 2u, 0x1111u);

  // Exit Forced Blank before rendering.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x2000u | 0x4000u));

  ppu.Update(960);
  ppu.SwapBuffers();

  // WIN0 has higher priority than WIN1, so the pixel must follow WIN0 (BG0).
  const auto fb = ppu.GetFramebuffer();
  ASSERT_GE(fb.size(), (size_t)PPU::SCREEN_WIDTH);
  EXPECT_EQ(fb[10], TestUtil::ARGBFromBGR555(0x001F));
}

TEST(PPUTest, ObjWindow_MasksLayersButDoesNotDrawPixels) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Allow OAM/VRAM/Palette setup.
  mem.Write16(0x04000000u, 0x0080u);

  // Disable all sprites by default (OAM reset-to-zero would otherwise create
  // many active sprites once we write non-zero tile data).
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // Mode 0, BG0+BG1 enable, OBJ enable, OBJWIN enable.
  mem.Write16(0x04000000u,
              (uint16_t)(0x0100u | 0x0200u | 0x1000u | 0x8000u | 0x0080u));

  // BG0 priority 0, charBase=1, screenBase=0
  mem.Write16(0x04000008u, (uint16_t)(0u | (1u << 2)));
  // BG1 priority 1, charBase=2, screenBase=1
  mem.Write16(0x0400000Au, (uint16_t)(1u | (2u << 2) | (1u << 8)));

  // WINOUT: outside windows => BG1 only; inside OBJWIN => BG0 only.
  // Bits: 0..3 BG0..BG3, bit4 OBJ, bit5 effects.
  const uint16_t winoutOutside = 0x0002u; // BG1 only
  const uint16_t winoutObjWin = 0x0001u;  // BG0 only
  mem.Write16(0x0400004Au, (uint16_t)((winoutObjWin << 8) | winoutOutside));

  // Palettes: idx1=red for BG0, idx1=green for BG1.
  mem.Write16(0x05000002u, 0x001Fu);
  mem.Write16(0x05000004u, 0x03E0u);

  // Fill tilemaps row 0 and tile 1 row 0 for BG0/BG1.
  for (uint32_t tx = 0; tx < 32; ++tx) {
    mem.Write16(0x06000000u + tx * 2u, 0x0001u);
    mem.Write16(0x06000000u + 0x0800u + tx * 2u, 0x0001u);
  }
  mem.Write16(0x06004000u + 1u * 32u + 0u, 0x1111u);
  mem.Write16(0x06004000u + 1u * 32u + 2u, 0x1111u);
  // BG1 uses palette index 2 (green).
  mem.Write16(0x06008000u + 1u * 32u + 0u, 0x2222u);
  mem.Write16(0x06008000u + 1u * 32u + 2u, 0x2222u);

  // OBJ window sprite at (0,0), 8x8, 4bpp, tileIndex=0.
  // attr0: y=0, objMode=2 (OBJWIN)
  const uint16_t attr0 = (uint16_t)(0u | (2u << 10));
  const uint16_t attr1 = 0u;
  const uint16_t attr2 = 0u;
  TestUtil::WriteOam16(mem, 0, attr0);
  TestUtil::WriteOam16(mem, 2, attr1);
  TestUtil::WriteOam16(mem, 4, attr2);

  // Make the OBJWIN sprite opaque at x=0..7 for scanline 0 (colorIndex=1).
  // Tile 0 row 0: pixels 0..7 all = 1.
  mem.Write16(0x06010000u + 0u, 0x1111u);
  mem.Write16(0x06010000u + 2u, 0x1111u);

  // Exit Forced Blank before rendering.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x0200u | 0x1000u | 0x8000u));

  ppu.Update(960);
  ppu.SwapBuffers();

  // Inside OBJWIN (x=1) => BG0 red.
  // Outside OBJWIN (x=20) => BG1 green.
  const auto fb = ppu.GetFramebuffer();
  ASSERT_GE(fb.size(), (size_t)PPU::SCREEN_WIDTH);
  EXPECT_EQ(fb[1], TestUtil::ARGBFromBGR555(0x001F));
  EXPECT_EQ(fb[20], TestUtil::ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, SemiTransparentObjBlendingIsGatedByObjWindowEffectsEnableBit) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Allow OAM/VRAM/Palette setup.
  mem.Write16(0x04000000u, 0x0080u);

  // Disable all sprites by default.
  for (uint32_t spr = 0; spr < 128; ++spr) {
    const uint32_t base = spr * 8u;
    TestUtil::WriteOam16(mem, base + 0u, (uint16_t)(1u << 9));
    TestUtil::WriteOam16(mem, base + 2u, 0u);
    TestUtil::WriteOam16(mem, base + 4u, 0u);
  }

  // Mode 0, BG0 enable, OBJ enable, OBJWIN enable.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x1000u | 0x8000u | 0x0080u));

  // BG0 priority 0, charBase=1, screenBase=0
  mem.Write16(0x04000008u, (uint16_t)(0u | (1u << 2)));

  // BG palette idx1 = red.
  mem.Write16(0x05000002u, 0x001Fu);
  // OBJ palette idx1 = blue.
  mem.Write16(0x05000200u + 2u, 0x7C00u);

  // BG0: fill row 0 with tile 1, and tile 1 row 0 = red.
  for (uint32_t tx = 0; tx < 32; ++tx) {
    mem.Write16(0x06000000u + tx * 2u, 0x0001u);
  }
  mem.Write16(0x06004000u + 1u * 32u + 0u, 0x1111u);
  mem.Write16(0x06004000u + 1u * 32u + 2u, 0x1111u);

  // BLDCNT: alpha blend (mode 1), first target = OBJ, second target = BG0.
  mem.Write16(0x04000050u, (uint16_t)(0x0040u | 0x0010u | 0x0100u));
  // EVA=8, EVB=8.
  mem.Write16(0x04000052u, 0x0808u);

  // WINOUT: outside windows => BG0+OBJ enabled, effects DISABLED.
  // OBJWIN region => BG0+OBJ enabled, effects ENABLED.
  const uint16_t outside = (uint16_t)(0x0001u | 0x0010u); // BG0 + OBJ
  const uint16_t objwin =
      (uint16_t)(0x0001u | 0x0010u | 0x0020u); // BG0 + OBJ + FX
  mem.Write16(0x0400004Au, (uint16_t)((objwin << 8) | outside));

  // Sprite 0: semi-transparent OBJ at (0,0), 8x8, 4bpp, tileIndex=0.
  const uint16_t spr0_attr0 = (uint16_t)(0u | (1u << 10)); // objMode=1
  const uint16_t spr0_attr1 = 0u;
  const uint16_t spr0_attr2 = 0u;
  TestUtil::WriteOam16(mem, 0, spr0_attr0);
  TestUtil::WriteOam16(mem, 2, spr0_attr1);
  TestUtil::WriteOam16(mem, 4, spr0_attr2);
  // Tile 0 row 0: pixels 0..7 all = 1 (blue).
  mem.Write16(0x06010000u + 0u, 0x1111u);
  mem.Write16(0x06010000u + 2u, 0x1111u);

  // Sprite 1: OBJ window mask at (0,0), 8x8, 4bpp, tileIndex=1.
  // It covers only the left half (x=0..3) on scanline 0.
  const uint16_t spr1_attr0 = (uint16_t)(0u | (2u << 10)); // objMode=2
  const uint16_t spr1_attr1 = 0u;
  const uint16_t spr1_attr2 = 1u;
  TestUtil::WriteOam16(mem, 8, spr1_attr0);
  TestUtil::WriteOam16(mem, 10, spr1_attr1);
  TestUtil::WriteOam16(mem, 12, spr1_attr2);
  // Tile 1 row 0: pixels [1,1,1,1,0,0,0,0]
  mem.Write16(0x06010000u + 1u * 32u + 0u, 0x1111u);
  mem.Write16(0x06010000u + 1u * 32u + 2u, 0x0000u);

  // Exit Forced Blank before rendering.
  mem.Write16(0x04000000u, (uint16_t)(0x0100u | 0x1000u | 0x8000u));

  ppu.Update(960);
  ppu.SwapBuffers();

  const auto fb = ppu.GetFramebuffer();
  ASSERT_GE(fb.size(), (size_t)PPU::SCREEN_WIDTH);

  // Inside OBJWIN => blending enabled => average(red,blue) = purple.
  EXPECT_EQ(fb[1], TestUtil::ARGBFromBGR555(0x3C0Fu));
  // Outside OBJWIN => effects disabled => no blending; OBJ stays blue.
  EXPECT_EQ(fb[6], TestUtil::ARGBFromBGR555(0x7C00u));
}
