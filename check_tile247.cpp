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
  for (int f = 0; f < 120; f++) { // 120 frames
    uint64_t target = (f + 1) * CYCLES_PER_FRAME;
    uint64_t current = 0;
    while (current < target) {
      current += gba.Step();
    }
  }

  auto &memory = gba.GetMemory();
  const uint8_t *vram = memory.GetVRAMData();

  uint32_t charOffset = 0x4000;

  std::cout << "=== Finding blank tiles at charBase=1 ===" << std::endl;

  int blankCount = 0;
  std::cout << "Blank tiles: ";
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
      if (blankCount < 20)
        std::cout << tile << " ";
      blankCount++;
    }
  }
  std::cout << "... total " << blankCount << std::endl;

  std::cout << "\n=== Tile 247 dump ===" << std::endl;
  uint32_t tile247 = charOffset + 247 * 32;
  for (int row = 0; row < 8; row++) {
    std::cout << "Row " << row << ": ";
    for (int col = 0; col < 4; col++) {
      std::cout << std::hex << std::setw(2) << std::setfill('0')
                << (int)vram[tile247 + row * 4 + col] << " ";
    }
    std::cout << std::endl;
  }

  std::cout << "\n=== Checking if tile 247 contains tilemap data ==="
            << std::endl;
  // Tile 247 is at offset 0x4000 + 247*32 = 0x5EE0
  // Tilemap at screenBase 13 = 0x6800
  // So tile 247 data is NOT in tilemap region - it's separate
  std::cout << "Tile 247 offset: 0x" << std::hex << (charOffset + 247 * 32)
            << std::endl;
  std::cout << "Tilemap start: 0x6800" << std::endl;
  std::cout << "Tile 247 is " << ((tile247 < 0x6800) ? "BEFORE" : "INSIDE")
            << " tilemap region" << std::endl;

  // Also check tilemap at screenBase 13
  uint32_t mapOffset = 13 * 0x800; // screenBase 13
  std::cout << "\n=== Tilemap at screenBase 13 (first 8 entries) ==="
            << std::endl;
  for (int i = 0; i < 8; i++) {
    uint16_t entry =
        vram[mapOffset + i * 2] | (vram[mapOffset + i * 2 + 1] << 8);
    int tile = entry & 0x3FF;
    int pal = (entry >> 12) & 0xF;
    std::cout << "[" << i << "] 0x" << std::hex << entry << " tile=" << std::dec
              << tile << " pal=" << pal << std::endl;
  }

  return 0;
}
