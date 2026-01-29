// Analyze what the NES emulator is storing in the "back buffer" at 0x06003200
// This should be the NES nametable converted to GBA format

#include "include/emulator/gba/GBA.h"
#include "include/emulator/gba/GBAMemory.h"
#include <cstdint>
#include <cstdio>
#include <map>

using namespace AIO::Emulator::GBA;

int main() {
  GBA gba;
  if (!gba.LoadROM("OG-DK.gba")) {
    printf("Failed to load ROM\n");
    return 1;
  }
  gba.Reset();

  // Run to frame 15
  for (int i = 0; i < 15; i++) {
    for (int j = 0; j < 280896; j++) {
      gba.Step();
    }
  }

  auto &mem = gba.GetMemory();

  printf("=== Analyzing back buffer at 0x06003200 ===\n\n");

  // The NES screen is 32x30 tiles = 960 entries
  // GBA tilemap at 256x256 is 32x32 = 1024 entries
  // Let's see what's in the back buffer

  printf("First 8 rows of back buffer (NES nametable equivalent):\n");
  for (int row = 0; row < 8; row++) {
    printf("Row %2d: ", row);
    for (int col = 0; col < 16; col++) {
      uint16_t entry = mem.Read16(0x06003200 + (row * 32 + col) * 2);
      int tile = entry & 0x3FF;
      printf("%3d ", tile);
    }
    printf("...\n");
  }

  printf("\n=== Tile index distribution in back buffer ===\n");
  std::map<int, int> ranges;
  for (int i = 0; i < 1024; i++) {
    uint16_t entry = mem.Read16(0x06003200 + i * 2);
    int tile = entry & 0x3FF;
    int range = tile / 64; // Group by 64s
    ranges[range]++;
  }

  printf("Tile index ranges:\n");
  for (auto &[range, cnt] : ranges) {
    printf("  %3d-%3d: %3d tiles", range * 64, range * 64 + 63, cnt);
    if (range * 64 >= 320)
      printf(" [OVERLAP REGION]");
    printf("\n");
  }

  // Now let's look at what SHOULD be happening
  printf("\n=== Expected NES emulator VRAM layout ===\n");
  printf("NES has 2KB of VRAM for nametables (2 screens of 32x30)\n");
  printf(
      "NES has 8KB of CHR ROM/RAM for tiles (2 pattern tables of 256 tiles)\n");
  printf("NES tiles are 8x8 2bpp = 16 bytes\n");
  printf("GBA tiles are 8x8 4bpp = 32 bytes\n\n");

  printf("The Classic NES emulator should:\n");
  printf("1. Convert NES 2bpp tiles to GBA 4bpp (doubling the size)\n");
  printf("2. Store converted tiles at CharBase (0x06004000)\n");
  printf("3. Convert NES nametable to GBA tilemap\n");
  printf("4. NES tile indices 0-255 should map to GBA tile indices\n");
  printf("   (possibly with an offset for the character base block)\n\n");

  // Check if there's a pattern - maybe the high bits are attribute data?
  printf("=== Checking if high bits have meaning ===\n");
  int pal8count = 0, pal0count = 0, otherPal = 0;
  for (int i = 0; i < 1024; i++) {
    uint16_t entry = mem.Read16(0x06003200 + i * 2);
    int pal = (entry >> 12) & 0xF;
    if (pal == 8)
      pal8count++;
    else if (pal == 0)
      pal0count++;
    else
      otherPal++;
  }
  printf("Palette usage: pal8=%d, pal0=%d, other=%d\n", pal8count, pal0count,
         otherPal);

  // Let's look at specific high tile entries
  printf("\n=== Examining high tile indices (>= 320) ===\n");
  printf("These overlap with the tilemap itself and cause garbage\n\n");

  for (int i = 0; i < 64; i++) {
    uint16_t entry = mem.Read16(0x06003200 + i * 2);
    int tile = entry & 0x3FF;
    if (tile >= 320) {
      int pal = (entry >> 12) & 0xF;
      int flip = (entry >> 10) & 0x3;
      printf("  [%3d] entry=0x%04x tile=%d pal=%d flip=%d", i, entry, tile, pal,
             flip);

      // What does this tile index ACTUALLY point to?
      uint32_t tileAddr = 0x06004000 + tile * 32;
      printf(" -> addr=0x%08x", tileAddr);
      if (tileAddr >= 0x06006800) {
        // This is reading from tilemap as tile data!
        int tilemapOffset = (tileAddr - 0x06006800) / 2;
        printf(" (tilemap[%d])", tilemapOffset);
      }
      printf("\n");
    }
  }

  // Let's check if there's something wrong with how the tiles are being indexed
  printf("\n=== Hypothesis: Tile index calculation error ===\n");
  printf("If NES tile index is being added to some offset incorrectly,\n");
  printf("or if the nametable itself is being treated as tile indices...\n\n");

  // Check: are the high tile values actually nametable ADDRESSES being used as
  // indices? NES nametable starts at 0x2000 in NES VRAM If someone accidentally
  // used nametable addresses / 16 as tile indices... 0x2000 / 16 = 512, 0x2400
  // / 16 = 576, etc.

  // Or maybe the tilemap is being read as tile data?
  printf("Checking if tilemap data is being misinterpreted as tile indices:\n");
  uint16_t firstFewEntries[8];
  for (int i = 0; i < 8; i++) {
    firstFewEntries[i] = mem.Read16(0x06003200 + i * 2);
    printf("  Buffer[%d] = 0x%04x (as tile: %d)\n", i, firstFewEntries[i],
           firstFewEntries[i] & 0x3FF);
  }

  return 0;
}
