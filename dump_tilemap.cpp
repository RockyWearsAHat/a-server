// Properly dump tilemap with correct parsing
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

  // Run for about 100 frames
  for (int f = 0; f < 100; f++) {
    for (int i = 0; i < 280896; i++) {
      gba.Step();
    }
  }

  std::cout << std::hex << std::setfill('0');

  uint16_t dispcnt = mem.Read16(0x04000000);
  uint16_t bg0cnt = mem.Read16(0x04000008);

  std::cout << "=== Graphics State ===" << std::endl;
  std::cout << "DISPCNT = 0x" << std::setw(4) << dispcnt << std::endl;
  std::cout << "BG0CNT  = 0x" << std::setw(4) << bg0cnt << std::endl;

  uint32_t charBase = 0x06000000 + (((bg0cnt >> 2) & 3) * 0x4000);
  uint32_t screenBase = 0x06000000 + (((bg0cnt >> 8) & 0x1F) * 0x800);

  std::cout << "Char base:   0x" << std::setw(8) << charBase << std::endl;
  std::cout << "Screen base: 0x" << std::setw(8) << screenBase << std::endl;

  // Dump raw tilemap entries
  std::cout << "\n=== Raw Tilemap Entries (first 8x8 tiles) ===" << std::endl;
  for (int row = 0; row < 8; row++) {
    std::cout << "Row " << std::dec << row << ": ";
    for (int col = 0; col < 8; col++) {
      uint32_t addr = screenBase + (row * 32 + col) * 2;
      uint16_t entry = mem.Read16(addr);
      std::cout << std::hex << std::setw(4) << entry << " ";
    }
    std::cout << std::endl;
  }

  // Parse tilemap entries properly
  std::cout << "\n=== Parsed Tilemap (first 8x8 tiles) ===" << std::endl;
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      uint32_t addr = screenBase + (row * 32 + col) * 2;
      uint16_t entry = mem.Read16(addr);
      uint16_t tileNum = entry & 0x3FF;
      bool hFlip = (entry >> 10) & 1;
      bool vFlip = (entry >> 11) & 1;
      uint8_t palette = (entry >> 12) & 0xF;

      std::cout << "  [" << row << "," << col << "] entry=0x" << std::setw(4)
                << entry << " tile=" << std::dec << std::setw(3) << tileNum
                << " pal=" << (int)palette << " h=" << hFlip << " v=" << vFlip
                << std::endl;
    }
  }

  // Dump palette 0 in detail
  std::cout << "\n=== Palette 0 Colors ===" << std::endl;
  for (int c = 0; c < 16; c++) {
    uint16_t color = mem.Read16(0x05000000 + c * 2);
    int r = (color & 0x1F) << 3;
    int g = ((color >> 5) & 0x1F) << 3;
    int b = ((color >> 10) & 0x1F) << 3;
    std::cout << "  [" << std::dec << c << "] = 0x" << std::hex << std::setw(4)
              << color << " RGB(" << std::dec << r << "," << g << "," << b
              << ")" << std::endl;
  }

  // Check if tiles are 4bpp or 8bpp
  bool is8bpp = (bg0cnt >> 7) & 1;
  std::cout << "\n=== Tile Format ===" << std::endl;
  std::cout << "8bpp mode: " << (is8bpp ? "yes" : "no (4bpp)") << std::endl;

  // Dump first tile data
  std::cout << "\n=== Tile 0 Data ===" << std::endl;
  for (int i = 0; i < 32; i++) {
    if (i % 8 == 0)
      std::cout << "  ";
    std::cout << std::hex << std::setw(2) << (int)mem.Read8(charBase + i)
              << " ";
    if (i % 8 == 7)
      std::cout << std::endl;
  }

  return 0;
}
