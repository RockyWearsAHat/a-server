// Check tile 247 content and find blank tiles
#include "emulator/gba/GBA.h"
#include <cstdint>
#include <iomanip>
#include <iostream>

int main() {
  AIO::Emulator::GBA::GBA gba;
  if (!gba.LoadROM("OG-DK.gba"))
    return 1;

  const uint64_t CYCLES_PER_FRAME = 280896;
  for (int f = 0; f < 120; f++) {
    uint64_t target = (f + 1) * CYCLES_PER_FRAME;
    uint64_t current = 0;
    while (current < target) {
      current += gba.Step();
    }
  }

  auto &memory = gba.GetMemory();
  const uint8_t *vram = memory.GetVRAMData();

  // charBase=1 means tiles at offset 0x4000
  uint32_t charOffset = 0x4000;

  std::cout << "=== Finding blank tiles at charBase=1 ===" << std::endl;

  int blankTiles[512];
  int numBlank = 0;

  for (int tile = 0; tile < 512; tile++) {
    uint32_t tileOffset = charOffset + tile * 32;
    bool isBlank = true;
    for (int b = 0; b < 32; b++) {
      if (vram[tileOffset + b] != 0) {
        isBlank = false;
        break;
      }
    }
    if (isBlank) {
      blankTiles[numBlank++] = tile;
    }
  }

  std::cout << "Found " << numBlank << " blank tiles: ";
  for (int i = 0; i < numBlank && i < 20; i++) {
    std::cout << blankTiles[i] << " ";
  }
  if (numBlank > 20)
    std::cout << "...";
  std::cout << std::endl;

  // Check specific tiles from tilemap
  std::cout << "\n=== Checking specific tiles from tilemap ===" << std::endl;
  int checkTiles[] = {0,  247, 248, 251, 510, 436, 32,
                      14, 65,  216, 87,  16,  24,  104};
  for (int tile : checkTiles) {
    uint32_t tileOffset = charOffset + tile * 32;
    bool isBlank = true;
    int nonZeroBytes = 0;
    for (int b = 0; b < 32; b++) {
      if (vram[tileOffset + b] != 0) {
        isBlank = false;
        nonZeroBytes++;
      }
    }
    std::cout << "Tile " << tile << " at 0x" << std::hex
              << (0x06000000 + tileOffset) << ": "
              << (isBlank ? "BLANK" : "HAS DATA") << " (nonzero=" << std::dec
              << nonZeroBytes << " bytes)" << std::endl;

    if (!isBlank && tile <= 251) {
      std::cout << "  First 8 bytes: ";
      for (int b = 0; b < 8; b++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << (int)vram[tileOffset + b] << " ";
      }
      std::cout << std::endl;
    }
  }

  // What does the top-left should look like?
  // Entry 0 in tilemap is 0x80f7 -> tile 247, palBank 8
  // For the screen to be black, tile 247 should be all zeros (blank)

  std::cout << "\n=== Tile 247 full dump ===" << std::endl;
  uint32_t tile247 = charOffset + 247 * 32;
  for (int row = 0; row < 8; row++) {
    std::cout << "Row " << row << ": ";
    for (int col = 0; col < 4; col++) {
      std::cout << std::hex << std::setw(2) << std::setfill('0')
                << (int)vram[tile247 + row * 4 + col] << " ";
    }
    std::cout << " -> pixels: ";
    for (int px = 0; px < 8; px++) {
      uint8_t b = vram[tile247 + row * 4 + px / 2];
      int pix = (px & 1) ? ((b >> 4) & 0xF) : (b & 0xF);
      std::cout << std::hex << pix;
    }
    std::cout << std::endl;
  }

  return 0;
}
