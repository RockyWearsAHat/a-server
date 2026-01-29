// Test harness to analyze OG-DK overlap tiles
// Runs headless and checks for overlap tile rendering

#include "include/emulator/gba/GBA.h"
#include "include/emulator/gba/GBAMemory.h"
#include "include/emulator/gba/PPU.h"
#include <cstdio>
#include <fstream>

using namespace AIO::Emulator::GBA;

int main() {
  GBA gba;
  if (!gba.LoadROM("OG-DK.gba")) {
    printf("Failed to load ROM\n");
    return 1;
  }
  gba.Reset();

  auto &mem = gba.GetMemory();

  printf("Running 120 frames to reach stable state...\n");
  for (int f = 0; f < 120; f++) {
    for (int i = 0; i < 280896; i++) {
      gba.Step();
    }
  }

  printf("\n=== Analyzing tilemap at screenBase 13 (0x06006800) ===\n");

  // Read BG0CNT
  uint16_t bg0cnt = mem.Read16(0x04000008);
  int charBase = (bg0cnt >> 2) & 0x3;
  int screenBase = (bg0cnt >> 8) & 0x1F;

  printf("BG0CNT: 0x%04X\n", bg0cnt);
  printf("  CharBase: %d → 0x%08X\n", charBase, 0x06000000 + charBase * 0x4000);
  printf("  ScreenBase: %d → 0x%08X\n", screenBase,
         0x06000000 + screenBase * 0x800);

  // Calculate overlap boundary
  uint32_t charAddr = 0x06000000 + charBase * 0x4000;
  uint32_t screenAddr = 0x06000000 + screenBase * 0x800;
  int overlapTileStart = (screenAddr - charAddr) / 32; // 4bpp

  printf("\nWith 4bpp tiles (32 bytes each):\n");
  printf("  Tile %d starts at screenAddr 0x%08X\n", overlapTileStart,
         screenAddr);

  // Scan tilemap for overlap tiles
  printf("\n=== Scanning tilemap for tiles >= %d ===\n", overlapTileStart);

  int overlapCount = 0;
  std::ofstream trace("ogdk_overlap_detail.txt");

  for (int y = 0; y < 64; y++) { // Size 2 = 32x64 tiles
    for (int x = 0; x < 32; x++) {
      uint32_t mapAddr = screenAddr + (y * 32 + x) * 2;
      uint16_t entry = mem.Read16(mapAddr);
      int tile = entry & 0x3FF;
      int palBank = (entry >> 12) & 0xF;

      if (tile >= overlapTileStart) {
        overlapCount++;
        uint32_t tileAddr = charAddr + tile * 32;

        // This tile's "data" is actually tilemap data!
        trace << "Tilemap[" << y << "," << x << "]: tile=" << tile
              << " palBank=" << palBank << " tileAddr=0x" << std::hex
              << tileAddr << std::dec;

        // Show what this "tile data" actually contains
        // It's reading from the tilemap itself!
        int tilemapOffset = (tileAddr - screenAddr) / 2;
        if (tileAddr >= screenAddr && tileAddr < screenAddr + 0x1000) {
          trace << " → READS TILEMAP ENTRY " << tilemapOffset;
        }
        trace << std::endl;

        if (overlapCount <= 10) {
          printf("  [%d,%d] tile=%d palBank=%d → tileAddr=0x%08X", y, x, tile,
                 palBank, tileAddr);
          if (tileAddr >= screenAddr && tileAddr < screenAddr + 0x1000) {
            printf(" (READS TILEMAP!)");
          }
          printf("\n");
        }
      }
    }
  }

  printf("\nTotal overlap tiles: %d out of 2048 entries\n", overlapCount);
  trace.close();
  printf("Full details written to ogdk_overlap_detail.txt\n");

  // KEY QUESTION: What screenBase is ACTUALLY being used?
  printf("\n=== Checking both screenbases ===\n");

  // The SWI calls alternate between 0x3200 and 0x6800
  // 0x3200 / 0x800 = 6.25 → screenBase 6 + 0x200 offset
  // 0x6800 / 0x800 = 13 → screenBase 13

  // ScreenBase 6 = 0x06003000
  uint32_t screen6 = 0x06003000;
  printf("\nScreenBase 6 (0x06003000) first 10 entries:\n");
  for (int i = 0; i < 10; i++) {
    uint16_t entry = mem.Read16(screen6 + i * 2);
    printf("  [%d] 0x%04X tile=%d pal=%d\n", i, entry, entry & 0x3FF,
           (entry >> 12) & 0xF);
  }

  // ScreenBase 6 + 0x200 = 0x06003200 (what SWI uses)
  uint32_t screen6_offset = 0x06003200;
  printf("\nScreenBase 6+0x200 (0x06003200) first 10 entries:\n");
  for (int i = 0; i < 10; i++) {
    uint16_t entry = mem.Read16(screen6_offset + i * 2);
    printf("  [%d] 0x%04X tile=%d pal=%d\n", i, entry, entry & 0x3FF,
           (entry >> 12) & 0xF);
  }

  // ScreenBase 13 = 0x06006800
  uint32_t screen13 = 0x06006800;
  printf("\nScreenBase 13 (0x06006800) first 10 entries:\n");
  for (int i = 0; i < 10; i++) {
    uint16_t entry = mem.Read16(screen13 + i * 2);
    printf("  [%d] 0x%04X tile=%d pal=%d\n", i, entry, entry & 0x3FF,
           (entry >> 12) & 0xF);
  }

  printf("\n=== The Fix ===\n");
  printf("When BG0CNT.screenBase=13, tiles 320+ overlap with the tilemap!\n");
  printf("Solution: Either:\n");
  printf("  1. Mask tile indices to max valid (319) for this "
         "charBase/screenBase combo\n");
  printf("  2. Skip pixels where tile address overlaps with tilemap\n");
  printf("  3. The game should NOT be using screenBase 13 for rendering!\n");
  printf("     Check if BG0CNT is being set correctly during VBlank swap\n");

  return 0;
}
