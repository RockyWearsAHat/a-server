// Analyze what the tilemap data looks like when interpreted as tiles
// This helps understand the "garbage" pattern

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "include/emulator/gba/GBA.h"
#include "include/emulator/gba/GBAMemory.h"

using namespace AIO::Emulator::GBA;

int main() {
  printf("=== Tilemap Data Analysis ===\n\n");

  // Create GBA instance
  GBA gba;

  // Load ROM
  if (!gba.LoadROM("OG-DK.gba")) {
    fprintf(stderr, "Failed to load ROM\n");
    return 1;
  }

  // Run 10 frames (280896 cycles per frame)
  for (int f = 0; f < 10; f++) {
    for (int i = 0; i < 280896; i++) {
      gba.Step();
    }
  }

  auto &mem = gba.GetMemory();

  // Configuration
  uint32_t charBase = 0x06004000;   // CharBase 1
  uint32_t screenBase = 0x06006800; // ScreenBase 13
  uint32_t tilemapSize = 0x1000; // 256x512 = 32x64 entries, 2 bytes each = 4KB

  printf("CharBase: 0x%08X\n", charBase);
  printf("ScreenBase: 0x%08X\n", screenBase);
  printf("Tilemap ends at: 0x%08X\n", screenBase + tilemapSize);
  printf("\n");

  // Calculate which tiles overlap
  printf("=== Tile Overlap Analysis ===\n");
  int firstOverlap = (screenBase - charBase) / 32;
  int lastOverlap = (screenBase + tilemapSize - charBase) / 32 - 1;
  printf("Tiles %d to %d overlap with tilemap\n", firstOverlap, lastOverlap);
  printf("Tile %d starts at 0x%08X (tilemap start)\n", firstOverlap,
         charBase + firstOverlap * 32);
  printf("Tile %d ends at 0x%08X (tilemap end)\n", lastOverlap,
         charBase + (lastOverlap + 1) * 32);
  printf("\n");

  // Sample a few overlap tiles and show what they contain
  printf("=== Sample Overlap Tile Data ===\n");
  int sampleTiles[] = {320, 384, 400, 436, 440};

  for (int tile : sampleTiles) {
    uint32_t tileAddr = charBase + tile * 32;
    printf("Tile %d (addr 0x%08X):\n", tile, tileAddr);

    // Read 32 bytes of tile data (8 rows of 4bpp = 4 bytes per row)
    printf("  Raw bytes: ");
    for (int i = 0; i < 32; i++) {
      uint8_t b = gba.ReadMem(tileAddr + i) & 0xFF;
      printf("%02X ", b);
      if (i == 15)
        printf("\n             ");
    }
    printf("\n");

    // This data is actually tilemap entries!
    // Each tilemap entry is 2 bytes: bits 0-9 = tile#, 10 = hflip, 11 = vflip,
    // 12-15 = palette
    printf("  As tilemap entries: ");
    for (int i = 0; i < 32; i += 2) {
      uint16_t entry = gba.ReadMem16(tileAddr + i);
      int entryTile = entry & 0x3FF;
      int pal = (entry >> 12) & 0xF;
      printf("[t%d,p%d] ", entryTile, pal);
    }
    printf("\n\n");
  }

  // Now look at what tile 440 actually contains
  // Tile 440 is the "fill" tile for rows 24-63
  printf("=== Tile 440 Detail (the 'fill' tile) ===\n");
  uint32_t tile440Addr = charBase + 440 * 32;
  printf("Address: 0x%08X\n", tile440Addr);

  // Relative to screenBase
  uint32_t offsetInTilemap = tile440Addr - screenBase;
  printf("Offset from tilemap start: 0x%X (%d bytes)\n", offsetInTilemap,
         offsetInTilemap);
  printf("This is tilemap entry %d (row %d, col %d)\n", offsetInTilemap / 2,
         offsetInTilemap / 64, (offsetInTilemap / 2) % 32);

  // Show the 8 rows of pixel data (as 4bpp)
  printf("\nAs 4bpp pixel data (what PPU would render):\n");
  for (int row = 0; row < 8; row++) {
    uint32_t rowData;
    // Read 4 bytes for this row
    rowData = gba.ReadMem32(tile440Addr + row * 4);
    printf("  Row %d: ", row);
    for (int px = 0; px < 8; px++) {
      int pixel = (rowData >> (px * 4)) & 0xF;
      printf("%X", pixel);
    }
    printf(" (raw: %08X)\n", rowData);
  }

  // Show what palette bank 0 colors are
  printf("\n=== Palette Bank 0 ===\n");
  for (int i = 0; i < 16; i++) {
    uint16_t color = gba.ReadMem16(0x05000000 + i * 2);
    int r = (color & 0x1F) * 255 / 31;
    int g = ((color >> 5) & 0x1F) * 255 / 31;
    int b = ((color >> 10) & 0x1F) * 255 / 31;
    printf("  Color %d: RGB(%3d, %3d, %3d) = 0x%04X\n", i, r, g, b, color);
  }

  return 0;
}
