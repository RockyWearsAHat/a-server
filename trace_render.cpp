// Trace actual pixel rendering for OG-DK
#include <emulator/gba/GBA.h>
#include <emulator/gba/GBAMemory.h>
#include <iomanip>
#include <iostream>

using namespace AIO::Emulator::GBA;

int main() {
  GBA gba;
  if (!gba.LoadROM("OG-DK.gba")) {
    std::cerr << "Failed to load ROM" << std::endl;
    return 1;
  }

  auto &mem = gba.GetMemory();

  // Run for 60 frames to reach stable state
  for (int f = 0; f < 60; f++) {
    for (int i = 0; i < 280896; i++) {
      gba.Step();
    }
  }

  std::cout << std::hex << std::setfill('0');

  // Now manually trace what the PPU SHOULD see for the first few tiles
  uint16_t bg0cnt = mem.Read16(0x04000008);
  uint32_t charBase = 0x06000000 + (((bg0cnt >> 2) & 3) * 0x4000);
  uint32_t screenBase = 0x06000000 + (((bg0cnt >> 8) & 0x1F) * 0x800);
  bool is8bpp = (bg0cnt >> 7) & 1;

  std::cout << "=== Manual Pixel Trace ===" << std::endl;
  std::cout << "BG0CNT = 0x" << std::setw(4) << bg0cnt << std::endl;
  std::cout << "Char base = 0x" << std::setw(8) << charBase << std::endl;
  std::cout << "Screen base = 0x" << std::setw(8) << screenBase << std::endl;
  std::cout << "8bpp = " << (is8bpp ? "yes" : "no") << std::endl;

  // Trace first tile (0,0)
  uint16_t entry = mem.Read16(screenBase);
  uint16_t tileIndex = entry & 0x3FF;
  uint8_t paletteBank = (entry >> 12) & 0xF;
  bool hFlip = (entry >> 10) & 1;
  bool vFlip = (entry >> 11) & 1;

  std::cout << "\n=== Tile at (0,0) ===" << std::endl;
  std::cout << "Raw entry = 0x" << std::setw(4) << entry << std::endl;
  std::cout << "Tile index = " << std::dec << tileIndex << std::endl;
  std::cout << "Palette bank = " << (int)paletteBank << std::endl;
  std::cout << "H-flip = " << hFlip << ", V-flip = " << vFlip << std::endl;

  // Classic NES fix: mask palette bank
  uint8_t maskedPaletteBank = paletteBank & 0x7;
  std::cout << "Masked palette bank (& 0x7) = " << (int)maskedPaletteBank
            << std::endl;

  // Read tile data
  uint32_t tileAddr = charBase + (tileIndex * 32);
  std::cout << "\nTile data at 0x" << std::hex << tileAddr << ":" << std::endl;
  for (int row = 0; row < 8; row++) {
    std::cout << "  Row " << std::dec << row << ": ";
    for (int col = 0; col < 4; col++) {
      uint8_t b = mem.Read8(tileAddr + row * 4 + col);
      uint8_t lo = b & 0xF;
      uint8_t hi = (b >> 4) & 0xF;
      std::cout << std::hex << (int)lo << (int)hi << " ";
    }
    std::cout << std::endl;
  }

  // For first row, first pixel
  uint8_t firstByte = mem.Read8(tileAddr);
  uint8_t colorIndex = firstByte & 0xF; // Low nibble for pixel 0
  std::cout << "\nFirst pixel color index (raw) = " << std::dec
            << (int)colorIndex << std::endl;

  // Apply offset for Classic NES
  uint8_t effectiveColorIndex = (colorIndex != 0) ? colorIndex + 8 : 0;
  std::cout << "Effective color index (+8) = " << (int)effectiveColorIndex
            << std::endl;

  // Calculate palette address
  uint32_t paletteAddr =
      0x05000000 + (maskedPaletteBank * 32) + (effectiveColorIndex * 2);
  std::cout << "Palette address = 0x" << std::hex << paletteAddr << std::endl;

  // Read the color
  uint16_t color = mem.Read16(paletteAddr);
  std::cout << "Color value = 0x" << std::setw(4) << color << std::endl;

  int r = (color & 0x1F) << 3;
  int g = ((color >> 5) & 0x1F) << 3;
  int b = ((color >> 10) & 0x1F) << 3;
  std::cout << "RGB = (" << std::dec << r << ", " << g << ", " << b << ")"
            << std::endl;

  // Now check what colors SHOULD be visible
  std::cout << "\n=== Expected Colors ===" << std::endl;
  std::cout << "Palette 0 (masked from palette 8):" << std::endl;
  for (int i = 0; i < 16; i++) {
    uint16_t c = mem.Read16(0x05000000 + i * 2);
    if (c != 0) {
      int rr = (c & 0x1F) << 3;
      int gg = ((c >> 5) & 0x1F) << 3;
      int bb = ((c >> 10) & 0x1F) << 3;
      std::cout << "  [" << std::dec << i << "] = 0x" << std::hex
                << std::setw(4) << c << " RGB(" << std::dec << rr << "," << gg
                << "," << bb << ")" << std::endl;
    }
  }

  return 0;
}
