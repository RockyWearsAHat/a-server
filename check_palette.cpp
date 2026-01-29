// Check palette colors for Classic NES
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
  const uint8_t *palData = memory.GetPaletteData();

  std::cout << "=== BG Palette Bank 0 (first 16 colors) ===" << std::endl;
  for (int i = 0; i < 16; i++) {
    uint16_t color = palData[i * 2] | (palData[i * 2 + 1] << 8);
    int r = (color & 0x1F) * 8;
    int g = ((color >> 5) & 0x1F) * 8;
    int b = ((color >> 10) & 0x1F) * 8;
    std::cout << "  Index " << std::dec << i << ": 0x" << std::hex
              << std::setw(4) << std::setfill('0') << color << " RGB("
              << std::dec << r << "," << g << "," << b << ")" << std::endl;
  }

  std::cout << "\n=== What tile 247 renders as ===" << std::endl;
  std::cout << "Tile 247 has pixels with index 0x3 (nibbles in 0x33)"
            << std::endl;
  std::cout << "With Classic NES +8 offset: index 3 -> index 11" << std::endl;
  uint16_t color11 = palData[11 * 2] | (palData[11 * 2 + 1] << 8);
  int r = (color11 & 0x1F) * 8;
  int g = ((color11 >> 5) & 0x1F) * 8;
  int b = ((color11 >> 10) & 0x1F) * 8;
  std::cout << "Palette index 11 = 0x" << std::hex << color11 << " RGB("
            << std::dec << r << "," << g << "," << b << ")" << std::endl;

  // But the tilemap has palBank=8, so it uses palette bank 8
  std::cout << "\n=== BG Palette Bank 8 (indices 128-143) ===" << std::endl;
  for (int i = 0; i < 16; i++) {
    int idx = 128 + i;
    uint16_t color = palData[idx * 2] | (palData[idx * 2 + 1] << 8);
    int r = (color & 0x1F) * 8;
    int g = ((color >> 5) & 0x1F) * 8;
    int b = ((color >> 10) & 0x1F) * 8;
    std::cout << "  Index " << std::dec << idx << ": 0x" << std::hex
              << std::setw(4) << std::setfill('0') << color << " RGB("
              << std::dec << r << "," << g << "," << b << ")" << std::endl;
  }

  std::cout << "\n=== Actual rendering ===" << std::endl;
  std::cout << "Tilemap entry 0x80f7: tile=247, palBank=8 (bit 15 set)"
            << std::endl;
  std::cout
      << "With Classic NES mode: palBank is masked/overridden to use bank 0"
      << std::endl;
  std::cout << "Pixel index in tile: 3 (from 0x33 bytes)" << std::endl;
  std::cout << "Classic NES offset: +8 -> effective index 11" << std::endl;
  std::cout << "Final color: palette[11] = CYAN" << std::endl;

  return 0;
}
