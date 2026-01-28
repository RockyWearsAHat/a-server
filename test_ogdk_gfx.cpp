/**
 * Deep diagnostic for OG-DK rendering
 * Check tile map, palette, and scroll registers
 */
#include "emulator/gba/GBA.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

int main() {
  AIO::Emulator::GBA::GBA gba;

  if (!gba.LoadROM("OG-DK.gba")) {
    std::cerr << "Failed to load ROM\n";
    return 1;
  }

  // Run for a bit to get past boot
  // GBA runs at 16.78MHz, ~280,000 cycles per frame at 60fps
  for (int f = 0; f < 60; f++) { // 60 frames
    for (int c = 0; c < 280000; c++) {
      gba.Step();
    }
  }

  auto &memory = gba.GetMemory();

  std::cout << "=== OG-DK Graphics State Diagnostic ===\n\n";

  // Read display registers
  uint16_t dispcnt = memory.Read16(0x04000000);
  uint16_t dispstat = memory.Read16(0x04000004);
  uint16_t vcount = memory.Read16(0x04000006);

  std::cout << "Display Registers:\n";
  std::cout << "  DISPCNT: 0x" << std::hex << std::setw(4) << std::setfill('0')
            << dispcnt << std::dec << "\n";
  std::cout << "    Mode: " << (dispcnt & 0x7) << "\n";
  std::cout << "    BG0 enable: " << ((dispcnt >> 8) & 1) << "\n";
  std::cout << "    BG1 enable: " << ((dispcnt >> 9) & 1) << "\n";
  std::cout << "    BG2 enable: " << ((dispcnt >> 10) & 1) << "\n";
  std::cout << "    BG3 enable: " << ((dispcnt >> 11) & 1) << "\n";
  std::cout << "    OBJ enable: " << ((dispcnt >> 12) & 1) << "\n";
  std::cout << "  VCOUNT: " << vcount << "\n\n";

  // BG0 control
  uint16_t bg0cnt = memory.Read16(0x04000008);
  std::cout << "BG0CNT: 0x" << std::hex << std::setw(4) << std::setfill('0')
            << bg0cnt << std::dec << "\n";
  int priority = bg0cnt & 0x3;
  int charBase = ((bg0cnt >> 2) & 0x3) * 0x4000;
  int screenBase = ((bg0cnt >> 8) & 0x1F) * 0x800;
  int screenSize = (bg0cnt >> 14) & 0x3;
  bool mosaic = (bg0cnt >> 6) & 1;
  bool colorMode = (bg0cnt >> 7) & 1; // 0 = 16 colors, 1 = 256 colors

  std::cout << "  Priority: " << priority << "\n";
  std::cout << "  Char Base: 0x" << std::hex << charBase << std::dec
            << " (VRAM offset)\n";
  std::cout << "  Screen Base: 0x" << std::hex << screenBase << std::dec
            << " (VRAM offset)\n";
  std::cout << "  Screen Size: " << screenSize << " (";
  switch (screenSize) {
  case 0:
    std::cout << "256x256";
    break;
  case 1:
    std::cout << "512x256";
    break;
  case 2:
    std::cout << "256x512";
    break;
  case 3:
    std::cout << "512x512";
    break;
  }
  std::cout << ")\n";
  std::cout << "  Mosaic: " << mosaic << "\n";
  std::cout << "  Color Mode: "
            << (colorMode ? "256 colors" : "16 colors (4bpp)") << "\n\n";

  // BG0 scroll registers
  uint16_t bg0hofs = memory.Read16(0x04000010);
  uint16_t bg0vofs = memory.Read16(0x04000012);
  std::cout << "BG0 Scroll: HOFS=" << (bg0hofs & 0x1FF)
            << ", VOFS=" << (bg0vofs & 0x1FF) << "\n\n";

  // Dump first few tilemap entries
  std::cout << "First 16 tilemap entries at screen base 0x" << std::hex
            << screenBase << std::dec << ":\n";
  for (int i = 0; i < 16; i++) {
    uint16_t entry = memory.Read16(0x06000000 + screenBase + i * 2);
    int tileNum = entry & 0x3FF;
    int hFlip = (entry >> 10) & 1;
    int vFlip = (entry >> 11) & 1;
    int palette = (entry >> 12) & 0xF;
    std::cout << "  [" << i << "] Tile " << tileNum << " pal " << palette;
    if (hFlip)
      std::cout << " H";
    if (vFlip)
      std::cout << " V";
    std::cout << "\n";
  }

  // Check palette
  std::cout << "\nBG Palette (first 16 colors, palette 0):\n";
  for (int i = 0; i < 16; i++) {
    uint16_t color = memory.Read16(0x05000000 + i * 2);
    int r = (color & 0x1F) * 8;
    int g = ((color >> 5) & 0x1F) * 8;
    int b = ((color >> 10) & 0x1F) * 8;
    std::cout << "  [" << std::setw(2) << i << "] 0x" << std::hex
              << std::setw(4) << color << std::dec << " -> RGB(" << r << ","
              << g << "," << b << ")\n";
  }

  // Check if any DMA is active
  std::cout << "\nDMA Registers:\n";
  for (int dma = 0; dma < 4; dma++) {
    uint32_t sad = memory.Read32(0x040000B0 + dma * 12);
    uint32_t dad = memory.Read32(0x040000B4 + dma * 12);
    uint32_t cnt = memory.Read32(0x040000B8 + dma * 12);
    bool enable = (cnt >> 31) & 1;
    if (enable) {
      std::cout << "  DMA" << dma << " ACTIVE: SAD=0x" << std::hex << sad
                << " DAD=0x" << dad << " CNT=0x" << cnt << std::dec << "\n";
    }
  }

  // Dump a portion of the tile data to see if it's valid
  std::cout << "\nFirst tile (4bpp) at char base:\n";
  for (int row = 0; row < 8; row++) {
    std::cout << "  ";
    uint32_t tileData = memory.Read32(0x06000000 + charBase + row * 4);
    for (int px = 0; px < 8; px++) {
      int palIdx = (tileData >> (px * 4)) & 0xF;
      std::cout << std::hex << palIdx;
    }
    std::cout << "\n";
  }

  std::cout << "\nDone.\n";
  return 0;
}
