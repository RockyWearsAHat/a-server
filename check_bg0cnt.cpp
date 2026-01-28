// Check BG0CNT and both screen buffer contents
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <vector>

#include "emulator/gba/GBA.h"
#include "emulator/gba/GBAMemory.h"

int main() {
  AIO::Emulator::GBA::GBA gba;

  std::string romPath = "OG-DK.gba";
  gba.LoadROM(romPath);

  // Run for 200 frames
  for (uint64_t i = 0; i < 200 * 280896; ++i) {
    gba.Step();
  }

  auto &mem = gba.GetMemory();

  // Read BG0CNT
  uint16_t bg0cnt = mem.Read16(0x04000008);
  printf("BG0CNT = 0x%04X\n", bg0cnt);
  printf("  Priority: %d\n", bg0cnt & 3);
  printf("  CharBase Block: %d (0x%08X)\n", (bg0cnt >> 2) & 3,
         0x06000000 + ((bg0cnt >> 2) & 3) * 0x4000);
  printf("  ScreenBase Block: %d (0x%08X)\n", (bg0cnt >> 8) & 0x1F,
         0x06000000 + ((bg0cnt >> 8) & 0x1F) * 0x800);
  printf("  Colors: %s\n", (bg0cnt >> 7) & 1 ? "256 (8bpp)" : "16/16 (4bpp)");
  printf("  Size: %d\n", (bg0cnt >> 14) & 3);

  uint32_t screenBase = 0x06000000 + ((bg0cnt >> 8) & 0x1F) * 0x800;
  uint32_t charBase = 0x06000000 + ((bg0cnt >> 2) & 3) * 0x4000;

  printf("\n=== Screen buffer at 0x06006800 (block 13) ===\n");
  printf("First 16 entries:\n");
  for (int i = 0; i < 16; ++i) {
    uint16_t entry = mem.Read16(0x06006800 + i * 2);
    uint16_t tileIdx = entry & 0x3FF;
    uint8_t hFlip = (entry >> 10) & 1;
    uint8_t vFlip = (entry >> 11) & 1;
    uint8_t pal = (entry >> 12) & 0xF;
    printf("  [%2d] 0x%04X: tile=%03X h=%d v=%d pal=%d\n", i, entry, tileIdx,
           hFlip, vFlip, pal);
  }

  printf("\n=== Screen buffer at 0x06003200 (block 6.4) ===\n");
  printf("First 16 entries:\n");
  for (int i = 0; i < 16; ++i) {
    uint16_t entry = mem.Read16(0x06003200 + i * 2);
    uint16_t tileIdx = entry & 0x3FF;
    uint8_t hFlip = (entry >> 10) & 1;
    uint8_t vFlip = (entry >> 11) & 1;
    uint8_t pal = (entry >> 12) & 0xF;
    printf("  [%2d] 0x%04X: tile=%03X h=%d v=%d pal=%d\n", i, entry, tileIdx,
           hFlip, vFlip, pal);
  }

  // Check what the ACTIVE screen base has
  printf("\n=== ACTIVE screen buffer at 0x%08X ===\n", screenBase);
  printf("First 16 entries:\n");
  for (int i = 0; i < 16; ++i) {
    uint16_t entry = mem.Read16(screenBase + i * 2);
    uint16_t tileIdx = entry & 0x3FF;
    uint8_t hFlip = (entry >> 10) & 1;
    uint8_t vFlip = (entry >> 11) & 1;
    uint8_t pal = (entry >> 12) & 0xF;
    printf("  [%2d] 0x%04X: tile=%03X h=%d v=%d pal=%d\n", i, entry, tileIdx,
           hFlip, vFlip, pal);
  }

  // Check DISPCNT
  uint16_t dispcnt = mem.Read16(0x04000000);
  printf("\nDISPCNT = 0x%04X\n", dispcnt);
  printf("  Mode: %d\n", dispcnt & 7);
  printf("  BG0 enabled: %s\n", (dispcnt >> 8) & 1 ? "YES" : "NO");
  printf("  BG1 enabled: %s\n", (dispcnt >> 9) & 1 ? "YES" : "NO");
  printf("  BG2 enabled: %s\n", (dispcnt >> 10) & 1 ? "YES" : "NO");
  printf("  BG3 enabled: %s\n", (dispcnt >> 11) & 1 ? "YES" : "NO");

  return 0;
}
