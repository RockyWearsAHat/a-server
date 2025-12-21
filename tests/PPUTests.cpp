#include <gtest/gtest.h>

#include <cstdlib>

#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/PPU.h"

using namespace AIO::Emulator::GBA;

namespace {
inline uint32_t ARGBFromBGR555(uint16_t bgr555) {
    const uint8_t r = (uint8_t)((bgr555 & 0x1F) << 3);
    const uint8_t g = (uint8_t)(((bgr555 >> 5) & 0x1F) << 3);
    const uint8_t b = (uint8_t)(((bgr555 >> 10) & 0x1F) << 3);
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

inline void WriteOam16(GBAMemory& mem, uint32_t oamOffset, uint16_t value) {
    mem.Write16(0x07000000u + oamOffset, value);
}

inline void WriteVRAM8(GBAMemory& mem, uint32_t vramAddr, uint8_t value) {
    // Use byte writes for 8bpp data; VRAM 8-bit writes to OBJ area are ignored on real HW,
    // but our memory model enforces that too. For tests, use 16-bit writes to populate.
    // We'll pack two bytes per halfword.
    const uint32_t aligned = vramAddr & ~1u;
    const bool high = (vramAddr & 1u) != 0;
    const uint16_t existing = mem.Read16(aligned);
    const uint16_t merged = high ? (uint16_t)((existing & 0x00FFu) | ((uint16_t)value << 8))
                                 : (uint16_t)((existing & 0xFF00u) | (uint16_t)value);
    mem.Write16(aligned, merged);
}
} // namespace

TEST(PPUTest, Obj2DMapping8bppUses64BlockRowStride) {
    GBAMemory mem;
    mem.Reset();

    PPU ppu(mem);

    // DISPCNT: mode 0, OBJ enable, OBJ mapping = 2D (bit6=0)
    mem.Write16(0x04000000u, 0x1000u);

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
    WriteOam16(mem, 0, attr0);
    WriteOam16(mem, 2, attr1);
    WriteOam16(mem, 4, attr2);

    // Populate OBJ VRAM (tileBase = 0x06010000). In 2D mapping, a tile-row step is 64 blocks in 8bpp.
    // Put colorIndex=1 at (0,0) of the top tile.
    const uint32_t tileBase = 0x06010000u;
    WriteVRAM8(mem, tileBase + 0u, 1u);

    // Put colorIndex=2 at (0,0) of the tile at ty=1 (one tile row down in the 2D layout).
    // That's +64 blocks * 32 bytes/block = 2048 bytes.
    WriteVRAM8(mem, tileBase + 2048u, 2u);

    // Render scanline 0 (top half of sprite): should pick colorIndex=1 at x=0.
    ppu.Update(960);
    ppu.SwapBuffers();
    const auto fb0 = ppu.GetFramebuffer();
    ASSERT_GE(fb0.size(), (size_t)PPU::SCREEN_WIDTH);
    EXPECT_EQ(fb0[0], ARGBFromBGR555(0x001F));

    // Advance to scanline 8 and render it (8 lines later).
    // Each scanline is 1232 cycles; we already consumed 960 cycles of line 0.
    ppu.Update(1232 - 960); // finish line 0
    ppu.Update(1232 * 7 + 960); // 7 full lines + hblank of line 8
    ppu.SwapBuffers();
    const auto fb8 = ppu.GetFramebuffer();
    const size_t idx8 = (size_t)8 * (size_t)PPU::SCREEN_WIDTH;
    ASSERT_GT(fb8.size(), idx8);
    EXPECT_EQ(fb8[idx8 + 0], ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, ObjVramUpperWindowMirrorsToObjRegion) {
    GBAMemory mem;
    mem.Reset();

    PPU ppu(mem);

    // DISPCNT: mode 0, OBJ enable, OBJ mapping = 2D (bit6=0)
    mem.Write16(0x04000000u, 0x1000u);

    // OBJ palette: index 1 = visible color
    mem.Write16(0x05000200u + 2u, 0x001Fu);

    // Sprite 0: 64x64, 4bpp, at (0,0), tileIndex=1023.
    // This setup forces the bottom row of tiles (ty=7) to compute tileNum >= 1024,
    // which addresses into 0x06018000+ and must mirror back to 0x06010000+.
    const uint16_t attr0 = 0u;                    // y=0, 4bpp, square
    const uint16_t attr1 = (uint16_t)(3u << 14);  // size=3 => 64x64
    const uint16_t attr2 = 1023u;                 // tileIndex=1023, paletteBank=0, prio=0
    WriteOam16(mem, 0, attr0);
    WriteOam16(mem, 2, attr1);
    WriteOam16(mem, 4, attr2);

    // For scanline 56 (spriteY=56 => ty=7, inTileY=0), the computed tileNum is:
    // tileNum = 1023 + (ty * 32) + tx = 1023 + 224 + 0 = 1247
    // The corresponding byte address is tileBase + tileNum*32.
    // Hardware maps 0x06018000-0x0601FFFF to 0x06010000-0x06017FFF, i.e. tileNum-1024.
    const uint32_t tileBase = 0x06010000u;
    const uint32_t mirroredTileNum = 1247u - 1024u; // 223
    const uint32_t mirroredAddr = tileBase + mirroredTileNum * 32u;

    // Set pixel (0,0) in that tile to colorIndex 1 (low nibble).
    // Use 16-bit write to avoid any bus quirks in the test harness helper.
    mem.Write16(mirroredAddr, 0x0001u);

    // Advance to scanline 56 and render it (hit HBlank start at cycle 960 for that line).
    ppu.Update(1232 * 56 + 960);
    ppu.SwapBuffers();

    const auto fb = ppu.GetFramebuffer();
    const size_t idx = (size_t)56 * (size_t)PPU::SCREEN_WIDTH;
    ASSERT_GT(fb.size(), idx);
    EXPECT_EQ(fb[idx + 0], ARGBFromBGR555(0x001F));
}

TEST(PPUTest, UnalignedIoWrite16AlignsToEvenAddress) {
    GBAMemory mem;
    mem.Reset();

    // WIN0H is at 0x04000040 (2 bytes). Some titles end up issuing unaligned halfword
    // stores into IO space; on hardware these behave as aligned halfword accesses.
    mem.Write16(0x04000041u, 0xFFFEu);

    // Expect the value to land in the aligned WIN0H register.
    EXPECT_EQ(mem.Read16(0x04000040u), 0xFFFEu);
}

TEST(PPUTest, UnalignedVramWritesAlignWhenFixEnabled) {
    if (std::getenv("AIO_FIX_VIDMEM_ALIGNED_WRITES") == nullptr) {
        GTEST_SKIP();
    }

    GBAMemory mem;
    mem.Reset();

    // Unaligned halfword store should be forced to an even address.
    mem.Write16(0x06000001u, 0xBBAAu);
    EXPECT_EQ(mem.Read8(0x06000000u), 0xAAu);
    EXPECT_EQ(mem.Read8(0x06000001u), 0xBBu);

    mem.Reset();

    // Unaligned word store should be forced to a word-aligned address.
    mem.Write32(0x06000002u, 0xDDCCBBAAu);
    EXPECT_EQ(mem.Read8(0x06000000u), 0xAAu);
    EXPECT_EQ(mem.Read8(0x06000001u), 0xBBu);
    EXPECT_EQ(mem.Read8(0x06000002u), 0xCCu);
    EXPECT_EQ(mem.Read8(0x06000003u), 0xDDu);
}

TEST(PPUTest, TextBgScreenSize1SelectsSecondHorizontalScreenBlock) {
    GBAMemory mem;
    mem.Reset();

    PPU ppu(mem);

    // DISPCNT: mode 0, BG0 enable
    mem.Write16(0x04000000u, 0x0100u);

    // BG0CNT: priority 0, charBase=0, screenBase=0, 4bpp, screenSize=1 (512x256 => 64x32)
    mem.Write16(0x04000008u, 0x4000u);

    // Scroll X by 256 pixels so x=0 reads tx=32 => screen block X=1
    mem.Write16(0x04000010u, 256u);
    mem.Write16(0x04000012u, 0u);

    // BG palette: index 1 = red-ish
    mem.Write16(0x05000002u, 0x001Fu);

    // Tilemap: second horizontal screen block starts at +2048 bytes.
    // Put tileIndex=1 at (0,0).
    mem.Write16(0x06000000u + 2048u, 0x0001u);

    // Tile graphics for tileIndex=1: set pixel (0,0) to colorIndex 1 (low nibble).
    mem.Write16(0x06000000u + 1u * 32u, 0x0001u);

    // Render scanline 0 (hit HBlank start at cycle 960 for that line).
    ppu.Update(960);
    ppu.SwapBuffers();

    const auto fb = ppu.GetFramebuffer();
    ASSERT_GE(fb.size(), (size_t)PPU::SCREEN_WIDTH);
    EXPECT_EQ(fb[0], ARGBFromBGR555(0x001F));
}

TEST(PPUTest, TextBgScreenSize2SelectsSecondVerticalScreenBlock) {
    GBAMemory mem;
    mem.Reset();

    PPU ppu(mem);

    // DISPCNT: mode 0, BG0 enable
    mem.Write16(0x04000000u, 0x0100u);

    // BG0CNT: priority 0, charBase=0, screenBase=0, 4bpp, screenSize=2 (256x512 => 32x64)
    mem.Write16(0x04000008u, 0x8000u);

    // Scroll Y by 256 pixels so scanline 0 reads ty=32 => screen block Y=1
    mem.Write16(0x04000010u, 0u);
    mem.Write16(0x04000012u, 256u);

    // BG palette: index 1 = green-ish
    mem.Write16(0x05000002u, 0x03E0u);

    // Tilemap: second vertical screen block starts at +2048 bytes.
    // Put tileIndex=1 at (0,0).
    mem.Write16(0x06000000u + 2048u, 0x0001u);

    // Tile graphics for tileIndex=1: set pixel (0,0) to colorIndex 1.
    mem.Write16(0x06000000u + 1u * 32u, 0x0001u);

    // Render scanline 0.
    ppu.Update(960);
    ppu.SwapBuffers();

    const auto fb = ppu.GetFramebuffer();
    ASSERT_GE(fb.size(), (size_t)PPU::SCREEN_WIDTH);
    EXPECT_EQ(fb[0], ARGBFromBGR555(0x03E0));
}

TEST(PPUTest, TextBgMosaicRepeatsPixelsHorizontally) {
    GBAMemory mem;
    mem.Reset();

    PPU ppu(mem);

    // DISPCNT: mode 0, BG0 enable
    mem.Write16(0x04000000u, 0x0100u);

    // BG0CNT: mosaic enable (bit6), charBase=0, screenBase=0, 4bpp, screenSize=0
    mem.Write16(0x04000008u, 0x0040u);

    // MOSAIC: BG mosaic H=2 (nibble=1), V=1
    mem.Write16(0x0400004Cu, 0x0001u);

    // BG palette: index 1 = red, index 2 = green
    mem.Write16(0x05000002u, 0x001Fu);
    mem.Write16(0x05000004u, 0x03E0u);

    // Tilemap entry (0,0): tileIndex=1
    mem.Write16(0x06000000u, 0x0001u);

    // Tile 1 graphics, row 0: pixels 0..3 = [1,2,1,2]
    // Two bytes, each encodes 2 pixels (low nibble first).
    mem.Write16(0x06000000u + 1u * 32u, 0x2121u);

    // Render scanline 0
    ppu.Update(960);
    ppu.SwapBuffers();

    const auto fb = ppu.GetFramebuffer();
    ASSERT_GE(fb.size(), (size_t)PPU::SCREEN_WIDTH);

    // With mosaic H=2, x=1 should repeat the x=0 pixel.
    EXPECT_EQ(fb[0], ARGBFromBGR555(0x001F));
    EXPECT_EQ(fb[1], ARGBFromBGR555(0x001F));
}

TEST(PPUTest, ObjMosaicRepeatsPixelsHorizontally) {
    GBAMemory mem;
    mem.Reset();

    PPU ppu(mem);

    // DISPCNT: mode 0, OBJ enable
    mem.Write16(0x04000000u, 0x1000u);

    // MOSAIC: OBJ mosaic H=2 (bits 8-11 nibble=1), V=1
    mem.Write16(0x0400004Cu, 0x0100u);

    // OBJ palette: index 1 = red, index 2 = green
    mem.Write16(0x05000200u + 2u, 0x001Fu);
    mem.Write16(0x05000200u + 4u, 0x03E0u);

    // Sprite 0: 8x8, 4bpp, mosaic enabled, at (0,0)
    const uint16_t attr0 = (uint16_t)(0u | (1u << 12));
    const uint16_t attr1 = 0u;
    const uint16_t attr2 = 0u;
    WriteOam16(mem, 0, attr0);
    WriteOam16(mem, 2, attr1);
    WriteOam16(mem, 4, attr2);

    // OBJ tile 0, row 0: pixels [1,2,...]
    // First byte 0x21 => pixel0=1 (low nibble), pixel1=2 (high nibble)
    mem.Write16(0x06010000u + 0u, 0x0021u);

    // Render scanline 0
    ppu.Update(960);
    ppu.SwapBuffers();

    const auto fb = ppu.GetFramebuffer();
    ASSERT_GE(fb.size(), (size_t)PPU::SCREEN_WIDTH);

    // With mosaic H=2, x=1 should repeat x=0.
    EXPECT_EQ(fb[0], ARGBFromBGR555(0x001F));
    EXPECT_EQ(fb[1], ARGBFromBGR555(0x001F));
}
