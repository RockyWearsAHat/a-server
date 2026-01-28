// Analyze the double-buffer behavior in OG-DK
// The SWI 0x02 calls show alternating r0=0x06003200 and r0=0x06006800
// This is double-buffered rendering - let's see what's actually configured

#include "include/emulator/gba/GBA.h"
#include "include/emulator/gba/GBAMemory.h"
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

using namespace AIO::Emulator::GBA;

// Track BG0CNT changes during execution
struct BG0State {
  uint16_t bg0cnt;
  int charBase;
  int screenBase;
  uint32_t pc;
  int frameCount;
};

std::vector<BG0State> bg0History;
int frameCounter = 0;

int main() {
  GBA gba;
  if (!gba.LoadROM("OG-DK.gba")) {
    printf("Failed to load ROM\n");
    return 1;
  }
  gba.Reset();

  auto &mem = gba.GetMemory();

  uint16_t lastBG0CNT = 0;

  // Run and track BG0CNT changes
  printf("Running 5 frames and tracking BG0CNT changes...\n\n");

  for (int f = 0; f < 5; f++) {
    for (int cycle = 0; cycle < 280896; cycle++) {
      gba.Step();

      uint16_t bg0cnt = mem.Read16(0x04000008);
      if (bg0cnt != lastBG0CNT) {
        BG0State state;
        state.bg0cnt = bg0cnt;
        state.charBase = (bg0cnt >> 2) & 0x3;
        state.screenBase = (bg0cnt >> 8) & 0x1F;
        state.pc = 0; // Can't access PC directly
        state.frameCount = f;
        bg0History.push_back(state);

        printf("Frame %d: BG0CNT changed to 0x%04X\n", f, bg0cnt);
        printf("  CharBase=%d (0x%08X), ScreenBase=%d (0x%08X)\n",
               state.charBase, 0x06000000 + state.charBase * 0x4000,
               state.screenBase, 0x06000000 + state.screenBase * 0x800);

        lastBG0CNT = bg0cnt;
      }
    }
    frameCounter++;
  }

  printf("\n=== BG0CNT Summary ===\n");
  printf("Total changes recorded: %zu\n", bg0History.size());

  // Now let's analyze both screenbases
  printf("\n=== ScreenBase Analysis ===\n");

  // Screenbase 6 = 0x06003000 (but the SWI uses 0x06003200 = screenbase 6 +
  // offset?) Screenbase 13 = 0x06006800

  // Actually, looking at the SWI addresses:
  // 0x06003200 = 0x06000000 + 0x3200 = 0x06000000 + 6*0x800 + 0x200
  // 0x06006800 = 0x06000000 + 0x6800 = 0x06000000 + 13*0x800

  // So screenbase 6 should be at 0x06003000, but SWI uses 0x06003200...
  // That's screenbase 6 + 512 bytes = screenbase 6.4?
  // Actually: 0x3200 / 0x800 = 6.25, not a clean screenbase

  printf("\nSWI destination addresses:\n");
  printf("  0x06003200 = screenBase 6 + 0x200 offset (6.25 blocks)\n");
  printf("  0x06006800 = screenBase 13 exactly\n");

  // Let's check what the actual BG0CNT screenBase values are being used
  printf("\n=== Checking Both Buffer Contents ===\n");

  // Current BG0CNT
  uint16_t bg0cnt = mem.Read16(0x04000008);
  int charBase = (bg0cnt >> 2) & 0x3;
  int screenBase = (bg0cnt >> 8) & 0x1F;

  printf("\nCurrent BG0CNT: 0x%04X\n", bg0cnt);
  printf("  CharBase: %d → 0x%08X\n", charBase, 0x06000000 + charBase * 0x4000);
  printf("  ScreenBase: %d → 0x%08X\n", screenBase,
         0x06000000 + screenBase * 0x800);

  // Check buffer at 0x06003200 (used by game)
  printf("\n=== Buffer at 0x06003200 ===\n");
  uint32_t buf1 = 0x06003200;
  std::map<int, int> tiles1;
  for (int i = 0; i < 1024; i++) { // 32x32 tiles
    uint16_t entry = mem.Read16(buf1 + i * 2);
    tiles1[entry & 0x3FF]++;
  }
  printf("Unique tiles: %zu\n", tiles1.size());
  printf("Most common:\n");
  std::vector<std::pair<int, int>> sorted1(tiles1.begin(), tiles1.end());
  std::sort(sorted1.begin(), sorted1.end(),
            [](auto &a, auto &b) { return a.second > b.second; });
  for (int i = 0; i < 10 && i < sorted1.size(); i++) {
    printf("  Tile %3d: %d times\n", sorted1[i].first, sorted1[i].second);
  }

  // Check buffer at 0x06006800 (screenBase 13)
  printf("\n=== Buffer at 0x06006800 (ScreenBase 13) ===\n");
  uint32_t buf2 = 0x06006800;
  std::map<int, int> tiles2;
  for (int i = 0; i < 1024; i++) { // 32x32 tiles
    uint16_t entry = mem.Read16(buf2 + i * 2);
    tiles2[entry & 0x3FF]++;
  }
  printf("Unique tiles: %zu\n", tiles2.size());
  printf("Most common:\n");
  std::vector<std::pair<int, int>> sorted2(tiles2.begin(), tiles2.end());
  std::sort(sorted2.begin(), sorted2.end(),
            [](auto &a, auto &b) { return a.second > b.second; });
  for (int i = 0; i < 10 && i < sorted2.size(); i++) {
    printf("  Tile %3d: %d times\n", sorted2[i].first, sorted2[i].second);
  }

  // Key question: With CharBase=1 (0x4000), what is the max valid tile?
  // CharBase 1 = 0x06004000
  // If using screenBase 6 (0x3000) = tiles can go up to (0x3000 - 0x4000)/32 =
  // NEGATIVE Wait, that can't be right...

  // Let's recalculate:
  // CharBase 1 = 0x06004000 (tiles start here)
  // ScreenBase 6 = 0x06003000 (BEFORE tiles!)
  // ScreenBase 13 = 0x06006800 (AFTER tiles start!)

  printf("\n=== VRAM Layout Diagram ===\n");
  printf("0x06000000 - 0x06003000: CharBase 0 tiles / empty\n");
  printf("0x06003000 - 0x06003800: ScreenBase 6 tilemap (2KB)\n");
  printf("          0x06003200: Actual tilemap start used by game\n");
  printf("0x06003800 - 0x06004000: More tilemap / padding\n");
  printf("0x06004000 - 0x06006800: CharBase 1 tiles (starts here)\n");
  printf("          0x06004000: Tile 0\n");
  printf("          0x06006800: Tile 320 = overlap with ScreenBase 13!\n");
  printf("0x06006800 - 0x06007000: ScreenBase 13 tilemap (2KB)\n");
  printf("0x06007000+: More tiles or other data\n");

  // The insight: The game uses TWO screenbases
  // When screenBase=6 (at 0x3000), tiles can start at charBase=1 (0x4000)
  // without overlap! When screenBase=13 (at 0x6800), tiles OVERLAP because
  // 0x4000+320*32 = 0x6800

  printf("\n=== KEY INSIGHT ===\n");
  printf("The game double-buffers between:\n");
  printf(
      "  Buffer A: ScreenBase 6 (0x06003000) - NO OVERLAP with CharBase 1\n");
  printf("  Buffer B: ScreenBase 13 (0x06006800) - OVERLAPS with CharBase 1 "
         "tile 320+\n");
  printf("\n");
  printf("When BG0CNT points to screenBase=13 (0x6800),\n");
  printf("tile indices 320+ read from the tilemap itself as tile data!\n");

  // Let's see what screenBase is actually set to most of the time
  printf("\n=== What ScreenBase is BG0 ACTUALLY using? ===\n");

  // Run a bit more and sample BG0CNT
  int sb6_count = 0, sb13_count = 0, other_count = 0;
  for (int f = 0; f < 60; f++) {
    for (int cycle = 0; cycle < 280896; cycle += 1000) {
      gba.Step();
      uint16_t bg0cnt = mem.Read16(0x04000008);
      int sb = (bg0cnt >> 8) & 0x1F;
      if (sb == 6)
        sb6_count++;
      else if (sb == 13)
        sb13_count++;
      else
        other_count++;
    }
  }
  printf("ScreenBase 6: %d samples\n", sb6_count);
  printf("ScreenBase 13: %d samples\n", sb13_count);
  printf("Other: %d samples\n", other_count);

  // Final check: what's the actual BG0CNT screenBase right now?
  bg0cnt = mem.Read16(0x04000008);
  screenBase = (bg0cnt >> 8) & 0x1F;
  printf("\nFinal BG0CNT: 0x%04X, ScreenBase: %d\n", bg0cnt, screenBase);

  return 0;
}
