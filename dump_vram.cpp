// Look for the source of corruption - trace what writes these high tile indices
// Let's dump raw memory and look for patterns

#include "include/emulator/gba/GBA.h"
#include "include/emulator/gba/GBAMemory.h"
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace AIO::Emulator::GBA;

int main() {
  GBA gba;
  if (!gba.LoadROM("OG-DK.gba")) {
    printf("Failed to load ROM\n");
    return 1;
  }
  gba.Reset();

  // Run a few frames
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 280896; j++) {
      gba.Step();
    }
  }

  auto &mem = gba.GetMemory();

  printf("=== Raw dump of key VRAM regions ===\n\n");

  // Dump CharBase area - where tiles should be
  printf("CharBase (tile data) at 0x06004000:\n");
  printf("  First 64 bytes (tiles 0-1 partial):\n  ");
  for (int i = 0; i < 64; i++) {
    printf("%02x ", mem.Read8(0x06004000 + i));
    if ((i + 1) % 16 == 0)
      printf("\n  ");
  }

  // Dump area just before tilemap overlap point
  printf("\n  Bytes at 0x060067E0 (tile 319, just before overlap):\n  ");
  for (int i = 0; i < 32; i++) {
    printf("%02x ", mem.Read8(0x060067E0 + i));
    if ((i + 1) % 16 == 0)
      printf("\n  ");
  }

  // Dump the actual tilemap
  printf("\nScreenBase (tilemap) at 0x06006800:\n");
  printf("  First 64 bytes (32 tilemap entries):\n  ");
  for (int i = 0; i < 64; i++) {
    printf("%02x ", mem.Read8(0x06006800 + i));
    if ((i + 1) % 16 == 0)
      printf("\n  ");
  }

  // What's in the back buffer?
  printf("\nBack buffer at 0x06003200:\n");
  printf("  First 64 bytes:\n  ");
  for (int i = 0; i < 64; i++) {
    printf("%02x ", mem.Read8(0x06003200 + i));
    if ((i + 1) % 16 == 0)
      printf("\n  ");
  }

  // Check: Are the back buffer and front buffer using the same data
  // differently?
  printf("\n=== Comparing first 32 entries of both buffers ===\n");
  printf("Back (0x06003200) vs Front (0x06006800):\n");
  for (int i = 0; i < 32; i++) {
    uint16_t back = mem.Read16(0x06003200 + i * 2);
    uint16_t front = mem.Read16(0x06006800 + i * 2);
    if (back != front) {
      printf("  [%2d] back=0x%04x (tile %3d) front=0x%04x (tile %3d) DIFF\n", i,
             back, back & 0x3FF, front, front & 0x3FF);
    }
  }

  // Let's look at what the NES CHR data looks like
  // It should be at some known location converted to GBA format
  printf("\n=== Looking for valid tile data patterns ===\n");

  // A valid 4bpp tile has pixel values 0-15
  // In packed format, each byte has two 4-bit values
  // We'd expect to see values like 0x00, 0x11, 0x22, 0x12, etc.

  // Check tile 0
  printf("Tile 0 at 0x06004000 (32 bytes):\n  ");
  bool tile0Valid = true;
  for (int i = 0; i < 32; i++) {
    uint8_t b = mem.Read8(0x06004000 + i);
    printf("%02x ", b);
    if ((i + 1) % 16 == 0)
      printf("\n  ");
  }

  // Check tile 247 (one of the commonly used ones)
  printf("\nTile 247 at 0x06005EE0 (32 bytes):\n  ");
  for (int i = 0; i < 32; i++) {
    uint8_t b = mem.Read8(0x06005EE0 + i);
    printf("%02x ", b);
    if ((i + 1) % 16 == 0)
      printf("\n  ");
  }

  // Check tile 440 (most used, but in overlap zone)
  printf("\nTile 440 at 0x06007700 (32 bytes) - IN TILEMAP OVERLAP:\n  ");
  for (int i = 0; i < 32; i++) {
    uint8_t b = mem.Read8(0x06007700 + i);
    printf("%02x ", b);
    if ((i + 1) % 16 == 0)
      printf("\n  ");
  }

  printf("\n=== Interpretation ===\n");
  printf("Tile 440 data is at 0x06007700 which is INSIDE the tilemap.\n");
  printf("Tilemap offset: 0x06007700 - 0x06006800 = 0x%x = %d bytes = %d "
         "entries\n",
         0x06007700 - 0x06006800, 0x06007700 - 0x06006800,
         (0x06007700 - 0x06006800) / 2);
  printf("So 'tile 440' data is actually tilemap entries 1152-1167!\n");
  printf("This creates recursive garbage - tilemap references itself.\n");

  return 0;
}
