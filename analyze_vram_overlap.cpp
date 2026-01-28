// Deep dive into the VRAM layout issue
// Understanding why tile indices 320+ overlap with tilemap

#include "include/emulator/gba/GBA.h"
#include "include/emulator/gba/GBAMemory.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
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

  // Run 120 frames for stable display
  printf("Running 120 frames...\n");
  for (int f = 0; f < 120; f++) {
    for (int i = 0; i < 280896; i++) {
      gba.Step();
    }
  }

  auto &mem = gba.GetMemory();

  printf("\n=== VRAM Layout Analysis for OG-DK ===\n\n");

  // Read registers
  uint16_t dispcnt = mem.Read16(0x04000000);
  uint16_t bg0cnt = mem.Read16(0x04000008);

  printf("DISPCNT = 0x%04X\n", dispcnt);
  printf("  Mode: %d\n", dispcnt & 7);
  printf("  BG0 enabled: %s\n", (dispcnt & 0x100) ? "yes" : "no");

  printf("\nBG0CNT = 0x%04X\n", bg0cnt);
  int charBase = (bg0cnt >> 2) & 0x3;
  int screenBase = (bg0cnt >> 8) & 0x1F;
  int colorMode = (bg0cnt >> 7) & 0x1;
  int screenSize = (bg0cnt >> 14) & 0x3;

  uint32_t charAddr = 0x06000000 + charBase * 0x4000;
  uint32_t screenAddr = 0x06000000 + screenBase * 0x800;
  int bytesPerTile = colorMode ? 64 : 32; // 8bpp=64, 4bpp=32

  printf("  CharBase: %d → tiles at 0x%08X\n", charBase, charAddr);
  printf("  ScreenBase: %d → tilemap at 0x%08X\n", screenBase, screenAddr);
  printf("  ColorMode: %s (%d bytes/tile)\n", colorMode ? "8bpp" : "4bpp",
         bytesPerTile);
  printf("  ScreenSize: %d\n", screenSize);

  // Calculate overlap
  int tileAtScreenBase = (screenAddr - charAddr) / bytesPerTile;
  printf("\n=== OVERLAP ANALYSIS ===\n");
  printf("Tile index 0 is at: 0x%08X\n", charAddr);
  printf("Tilemap starts at: 0x%08X\n", screenAddr);
  printf("Tile index %d starts at tilemap address!\n", tileAtScreenBase);
  printf("  Formula: (0x%08X - 0x%08X) / %d = %d\n", screenAddr, charAddr,
         bytesPerTile, tileAtScreenBase);

  // Scan the tilemap and categorize tiles
  printf("\n=== TILEMAP SCAN ===\n");

  std::map<int, int> tileCount;
  std::map<int, int> palCount;
  int validTiles = 0;
  int overlapTiles = 0;

  // Size 2 = 32x64 tiles, but we might have two screenbases
  int mapWidth = 32;
  int mapHeight = (screenSize >= 2) ? 64 : 32;
  int totalEntries = mapWidth * mapHeight;

  for (int i = 0; i < totalEntries; i++) {
    uint16_t entry = mem.Read16(screenAddr + i * 2);
    int tile = entry & 0x3FF;
    int pal = (entry >> 12) & 0xF;

    tileCount[tile]++;
    palCount[pal]++;

    if (tile < tileAtScreenBase) {
      validTiles++;
    } else {
      overlapTiles++;
    }
  }

  printf("Total tilemap entries: %d\n", totalEntries);
  printf("Valid tiles (0-%d): %d (%.1f%%)\n", tileAtScreenBase - 1, validTiles,
         validTiles * 100.0 / totalEntries);
  printf("Overlap tiles (%d+): %d (%.1f%%)\n", tileAtScreenBase, overlapTiles,
         overlapTiles * 100.0 / totalEntries);

  // Show the most common overlap tiles
  printf("\nMost common tiles in overlap region:\n");
  std::vector<std::pair<int, int>> overlapList;
  for (auto &p : tileCount) {
    if (p.first >= tileAtScreenBase) {
      overlapList.push_back(p);
    }
  }
  std::sort(overlapList.begin(), overlapList.end(),
            [](auto &a, auto &b) { return a.second > b.second; });

  for (int i = 0; i < std::min(10, (int)overlapList.size()); i++) {
    int tile = overlapList[i].first;
    int count = overlapList[i].second;
    uint32_t addr = charAddr + tile * bytesPerTile;

    // Calculate what this "tile data" actually is (tilemap entries)
    int tilemapOffset = (addr - screenAddr);

    printf("  Tile %d (used %d times): address 0x%08X\n", tile, count, addr);
    printf("    This is tilemap offset %d (entries %d-%d)\n", tilemapOffset,
           tilemapOffset / 2, tilemapOffset / 2 + 15);

    // Show what the "tile data" looks like
    printf("    Data: ");
    for (int b = 0; b < 16; b++) {
      printf("%02X ", mem.Read8(addr + b));
    }
    printf("...\n");

    // Interpret as tilemap entries
    printf("    As tilemap: ");
    for (int e = 0; e < 4; e++) {
      uint16_t tmEntry = mem.Read16(addr + e * 2);
      printf("[tile=%d,pal=%d] ", tmEntry & 0x3FF, (tmEntry >> 12) & 0xF);
    }
    printf("\n");
  }

  // What's the solution?
  printf("\n=== POTENTIAL SOLUTIONS ===\n");

  // Option 1: Check if there's a different interpretation
  printf("\n1. GBA VRAM Layout per GBATEK:\n");
  printf("   Mode 0-2: 0x06000000-0x0600FFFF = 64KB BG Map + Tiles\n");
  printf("   CharBase blocks: 0,1,2,3 = 0x0000, 0x4000, 0x8000, 0xC000\n");
  printf("   ScreenBase blocks: 0-31, each 2KB\n");
  printf("   With CharBase=1 (0x4000), max tiles = (0x10000-0x4000)/32 = 1536 "
         "tiles\n");
  printf("   But screenBase=13 (0x6800) uses only 2-4KB\n");

  // Option 2: Check mGBA behavior
  printf("\n2. Possible interpretations:\n");
  printf(
      "   a) Tile indices should wrap at 512 (bits 0-8 only): tile %d → %d\n",
      440, 440 & 0x1FF);
  printf("   b) CharBase should have extra bits: charBase with bit 2 = block "
         "5?\n");
  printf("   c) Game bug that works due to specific hardware timing\n");

  // Option 3: Check if this is a known Classic NES issue
  printf("\n3. Classic NES Series specifics:\n");
  printf("   These games run an NES emulator on GBA\n");
  printf("   NES has 2x8KB pattern tables (512 tiles total, 0-511)\n");
  printf("   GBA tile index should be 0-511 for NES compat!\n");

  // Test the wrapping theory
  printf("\n=== Testing Tile Index Masking ===\n");
  printf("If we mask tile indices to 9 bits (0-511):\n");

  std::map<int, int> maskedTileCount;
  int fixedCount = 0;
  for (auto &p : tileCount) {
    int origTile = p.first;
    int maskedTile = origTile & 0x1FF; // 9 bits = 0-511
    maskedTileCount[maskedTile] += p.second;
    if (origTile != maskedTile) {
      fixedCount += p.second;
    }
  }

  printf("Entries that would change: %d (%.1f%%)\n", fixedCount,
         fixedCount * 100.0 / totalEntries);

  // Verify the fix would work
  printf("\nMost common tiles after masking:\n");
  std::vector<std::pair<int, int>> maskedList(maskedTileCount.begin(),
                                              maskedTileCount.end());
  std::sort(maskedList.begin(), maskedList.end(),
            [](auto &a, auto &b) { return a.second > b.second; });

  for (int i = 0; i < std::min(15, (int)maskedList.size()); i++) {
    int tile = maskedList[i].first;
    int count = maskedList[i].second;
    uint32_t addr = charAddr + tile * bytesPerTile;
    printf("  Tile %d: %d uses at 0x%08X", tile, count, addr);
    if (addr >= screenAddr) {
      printf(" [STILL IN OVERLAP!]");
    }
    printf("\n");
  }

  // Final check - what tiles are actually valid?
  int maxValidTile = (screenAddr - charAddr) / bytesPerTile - 1;
  printf("\n=== SUMMARY ===\n");
  printf("Max valid tile index for this setup: %d\n", maxValidTile);
  printf("Any tile >= %d reads from tilemap region!\n", maxValidTile + 1);
  printf("\nWith 9-bit masking (NES compat):\n");
  int wouldBeValid = 0;
  for (auto &p : tileCount) {
    int maskedTile = p.first & 0x1FF;
    if (maskedTile <= maxValidTile) {
      wouldBeValid += p.second;
    }
  }
  printf("  Valid entries: %d (%.1f%%)\n", wouldBeValid,
         wouldBeValid * 100.0 / totalEntries);

  return 0;
}
