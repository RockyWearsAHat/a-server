// Trace VRAM writes to see what the NES emulator is writing
// This will help us understand why tile indices are so high (320+)

#include "include/emulator/gba/ARM7TDMI.h"
#include "include/emulator/gba/GBA.h"
#include "include/emulator/gba/GBAMemory.h"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <vector>

using namespace AIO::Emulator::GBA;

std::vector<uint8_t> readRom(const char *path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
}

// Structure to track VRAM writes
struct VRAMWrite {
  uint32_t pc;
  uint32_t address;
  uint32_t value;
  int size; // 8, 16, or 32
};

int main() {
  GBA gba;
  if (!gba.LoadROM("OG-DK.gba")) {
    printf("Failed to load ROM\n");
    return 1;
  }
  gba.Reset();

  // Run for some frames first
  const int targetFrame = 15;
  printf("Running to frame %d to let game initialize...\n", targetFrame);

  for (int i = 0; i < targetFrame; i++) {
    for (int j = 0; j < 280896; j++) {
      gba.Step();
    }
  }

  // Now we're at frame 15 - track VRAM writes during the next frame
  printf("\n=== Tracking VRAM writes during frame %d ===\n", targetFrame);

  // Take snapshot of tilemap before frame
  auto &mem = gba.GetMemory();
  std::vector<uint16_t> tilemapBefore(2048); // 4KB = 2048 halfwords
  for (int i = 0; i < 2048; i++) {
    tilemapBefore[i] = mem.Read16(0x06006800 + i * 2);
  }

  // Track tilemap changes
  std::map<uint32_t, std::pair<uint16_t, uint16_t>>
      tilemapChanges; // addr -> (before, after)

  // Run one more frame
  for (int j = 0; j < 280896; j++) {
    gba.Step();
  }

  // Check what changed in tilemap
  printf("\n=== Tilemap changes at 0x06006800 ===\n");
  int changeCount = 0;
  for (int i = 0; i < 2048; i++) {
    uint16_t after = mem.Read16(0x06006800 + i * 2);
    if (after != tilemapBefore[i]) {
      changeCount++;
      if (changeCount <= 30) {
        uint16_t tile = after & 0x3FF;
        uint16_t flip = (after >> 10) & 0x3;
        uint16_t pal = (after >> 12) & 0xF;
        printf(
            "  [0x%08x] offset %d: 0x%04x -> 0x%04x (tile=%d pal=%d flip=%d)\n",
            0x06006800 + i * 2, i, tilemapBefore[i], after, tile, pal, flip);
      }
    }
  }
  printf("Total tilemap changes: %d\n", changeCount);

  // Now let's look at the CharBase area (tiles) - 0x06004000
  printf("\n=== Character data (tiles) at CharBase 0x06004000 ===\n");
  printf("First few tiles (32 bytes each in 4bpp):\n");

  for (int tileIdx = 0; tileIdx < 5; tileIdx++) {
    uint32_t tileAddr = 0x06004000 + tileIdx * 32;
    printf("  Tile %d at 0x%08x: ", tileIdx, tileAddr);
    for (int b = 0; b < 8; b++) {
      printf("%02x ", mem.Read8(tileAddr + b));
    }
    printf("...\n");
  }

  // Check tiles around 320 boundary (where overlap starts)
  printf("\nTiles near overlap boundary (tile 320 = 0x06006800 = tilemap!):\n");
  for (int tileIdx = 318; tileIdx <= 322; tileIdx++) {
    uint32_t tileAddr = 0x06004000 + tileIdx * 32;
    printf("  Tile %d at 0x%08x: ", tileIdx, tileAddr);
    for (int b = 0; b < 8; b++) {
      printf("%02x ", mem.Read8(tileAddr + b));
    }
    printf("...\n");
  }

  // Let's look at what the NES emulator stored at specific high tile indices
  printf("\n=== High tile indices used in tilemap ===\n");
  std::map<int, int> highTileCounts;
  for (int i = 0; i < 2048; i++) {
    uint16_t entry = mem.Read16(0x06006800 + i * 2);
    int tile = entry & 0x3FF;
    if (tile >= 256) { // NES only has 256 tiles per pattern table
      highTileCounts[tile]++;
    }
  }

  printf("Tiles >= 256 (NES should only have 0-255):\n");
  int count = 0;
  for (auto &[tile, cnt] : highTileCounts) {
    if (count++ < 20) {
      uint32_t addr = 0x06004000 + tile * 32;
      printf("  Tile %d (%d uses) at addr 0x%08x", tile, cnt, addr);
      if (addr >= 0x06006800) {
        printf(" [IN TILEMAP!]");
      }
      printf("\n");
    }
  }
  printf("Total unique tiles >= 256: %d\n", (int)highTileCounts.size());

  // Let's trace what PC writes to the tilemap region
  printf("\n=== Analysis: What should the NES emulator do? ===\n");
  printf("NES has 2 pattern tables of 256 tiles each (512 total).\n");
  printf("Each tile is 8x8 pixels, 2bpp = 16 bytes per tile.\n");
  printf("GBA uses 4bpp = 32 bytes per tile.\n");
  printf(
      "If NES tiles are converted to GBA, they should fit in tiles 0-511.\n");
  printf("But we're seeing tiles 320+ which overlap with tilemap at "
         "0x06006800!\n");
  printf("\n");
  printf("VRAM layout collision:\n");
  printf("  CharBase=0x06004000 (tile data starts here)\n");
  printf("  Tile 320 = 0x06004000 + 320*32 = 0x06006800 = ScreenBase!\n");
  printf("  ScreenBase=0x06006800 (tilemap starts here)\n");
  printf("\n");
  printf("The NES emulator needs to use tile indices 0-319 OR use a\n");
  printf("different ScreenBase to avoid overlap!\n");

  return 0;
}
