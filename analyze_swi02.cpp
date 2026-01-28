// Trace what CpuFastSet (SWI 0x02) is doing to VRAM
// R0 = src, R1 = len_mode, R2 = dst
// len_mode: bits 0-20 = count (in words), bit 24 = fill mode

#include "include/emulator/gba/ARM7TDMI.h"
#include "include/emulator/gba/GBA.h"
#include "include/emulator/gba/GBAMemory.h"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <vector>

using namespace AIO::Emulator::GBA;

int main() {
  GBA gba;
  if (!gba.LoadROM("OG-DK.gba")) {
    printf("Failed to load ROM\n");
    return 1;
  }
  gba.Reset();

  printf("=== Analyzing SWI 0x02 (CpuFastSet) calls ===\n\n");
  printf("The game calls SWI 0x02 with:\n");
  printf("  R0 = 0x06003200 or 0x06006800 (dst)\n");
  printf("  R1 = 0x0000099F (count/mode)\n");
  printf("  R2 = 0x08002739 (src when not fill mode)\n\n");

  uint32_t r1 = 0x099F;
  uint32_t count = r1 & 0x1FFFFF; // bits 0-20
  bool fillMode = (r1 >> 24) & 1; // bit 24

  printf("Decoding R1=0x%08x:\n", r1);
  printf("  Count = %d words = %d bytes\n", count, count * 4);
  printf("  Fill mode = %d\n", fillMode);

  // 0x099F = 2463 words = 9852 bytes
  // The tilemap is 4KB = 1024 words
  // So this is filling ~2.4x the tilemap size

  printf("\nThe game is doing CpuFastSet fill of %d bytes\n", count * 4);
  printf("This fills from 0x06003200 to 0x%08x (or 0x06006800 to 0x%08x)\n",
         0x06003200 + count * 4, 0x06006800 + count * 4);

  // Run to frame 5 and check the tilemaps
  printf("\n=== Running to frame 5 ===\n");
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 280896; j++) {
      gba.Step();
    }
  }

  auto &mem = gba.GetMemory();

  // Dump part of tilemap at 0x06006800
  printf("\n=== Tilemap at 0x06006800 (ScreenBase 13) ===\n");
  printf("Format: each entry is 16-bit: pppp vhtt tttt tttt\n");
  printf("        t=tile index (10 bits), h=hflip, v=vflip, p=palette\n\n");

  // Show first few rows of tilemap (32 tiles per row for 256-pixel width)
  for (int row = 0; row < 4; row++) {
    printf("Row %d: ", row);
    for (int col = 0; col < 8; col++) {
      uint16_t entry = mem.Read16(0x06006800 + (row * 32 + col) * 2);
      int tile = entry & 0x3FF;
      int pal = (entry >> 12) & 0xF;
      printf("%3d/p%d ", tile, pal);
    }
    printf("...\n");
  }

  // Check if there's a pattern in the high tile indices
  printf("\n=== Looking for pattern in tile indices ===\n");

  std::map<int, int> tileUsage;
  for (int i = 0; i < 1024; i++) { // 32x32 tilemap
    uint16_t entry = mem.Read16(0x06006800 + i * 2);
    int tile = entry & 0x3FF;
    tileUsage[tile]++;
  }

  // Most common tiles
  printf("Most used tiles:\n");
  std::vector<std::pair<int, int>> sorted;
  for (auto &[tile, cnt] : tileUsage) {
    sorted.push_back({cnt, tile});
  }
  std::sort(sorted.rbegin(), sorted.rend());

  for (int i = 0; i < 10 && i < sorted.size(); i++) {
    int tile = sorted[i].second;
    int cnt = sorted[i].first;
    uint32_t addr = 0x06004000 + tile * 32;
    printf("  Tile %3d: %3d uses, addr=0x%08x", tile, cnt, addr);
    if (addr >= 0x06006800) {
      printf(" [OVERLAP WITH TILEMAP!]");
    }
    printf("\n");
  }

  // Now let's look at what values are being USED as tile data in the overlap
  // region
  printf("\n=== Values at tile 320 (= tilemap entry 0) ===\n");
  // Tile 320 starts at 0x06006800 which is tilemap[0]
  // So the "tile data" for tile 320 IS the tilemap entries themselves!
  printf("Address 0x06006800 (tilemap start / tile 320 data):\n");
  for (int i = 0; i < 8; i++) {
    printf("  [0x%02x]: 0x%04x", i * 2, mem.Read16(0x06006800 + i * 2));
    // This is both a tilemap entry AND 2 bytes of "tile 320" data
    uint16_t entry = mem.Read16(0x06006800 + i * 2);
    int tile = entry & 0x3FF;
    printf(" (as tilemap: tile %d, pal %d)", tile, (entry >> 12) & 0xF);
    printf("\n");
  }

  printf("\n=== Key insight ===\n");
  printf("The NES emulator stores its nametable (tilemap) at 0x06003200.\n");
  printf("The GBA displays from ScreenBase 13 = 0x06006800.\n");
  printf("But wait... let's check if they copy 0x06003200 to 0x06006800...\n");

  // Compare the two buffers
  printf("\n=== Comparing 0x06003200 and 0x06006800 ===\n");
  int matches = 0;
  int diffs = 0;
  for (int i = 0; i < 1024; i++) {
    uint16_t a = mem.Read16(0x06003200 + i * 2);
    uint16_t b = mem.Read16(0x06006800 + i * 2);
    if (a == b)
      matches++;
    else
      diffs++;
  }
  printf("Matching entries: %d, Different: %d\n", matches, diffs);

  if (matches > 900) {
    printf(
        "\nThe buffers are nearly identical - they ARE the double buffers!\n");
    printf(
        "The NES emulator renders to 0x06003200, then copies to 0x06006800.\n");
  }

  return 0;
}
