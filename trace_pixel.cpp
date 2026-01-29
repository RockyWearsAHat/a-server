// Generate PPM and trace specific pixel rendering
#include <emulator/gba/GBA.h>
#include <emulator/gba/GBAMemory.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>

using namespace AIO::Emulator::GBA;

int main() {
  GBA gba;
  if (!gba.LoadROM("OG-DK.gba")) {
    std::cerr << "Failed to load ROM" << std::endl;
    return 1;
  }

  auto &mem = gba.GetMemory();
  auto &ppu = gba.GetPPU();

  // Run for 100 frames
  for (int f = 0; f < 100; f++) {
    for (int i = 0; i < 280896; i++) {
      gba.Step();
    }
  }

  // Get framebuffer
  auto &fb = ppu.GetFramebuffer();

  // Write PPM
  std::ofstream ppm("ogdk_fresh.ppm");
  ppm << "P3\n240 160\n255\n";

  for (int y = 0; y < 160; y++) {
    for (int x = 0; x < 240; x++) {
      uint32_t pixel = fb[y * 240 + x];
      int r = (pixel >> 16) & 0xFF;
      int g = (pixel >> 8) & 0xFF;
      int b = pixel & 0xFF;
      ppm << r << " " << g << " " << b << " ";
    }
    ppm << "\n";
  }
  ppm.close();
  std::cout << "Wrote ogdk_fresh.ppm" << std::endl;

  // Analyze color distribution
  std::map<uint32_t, int> colorCount;
  for (auto p : fb) {
    colorCount[p & 0xFFFFFF]++;
  }

  std::cout << "\nColor distribution (" << colorCount.size()
            << " unique colors):" << std::endl;
  for (auto &[color, count] : colorCount) {
    int r = (color >> 16) & 0xFF;
    int g = (color >> 8) & 0xFF;
    int b = color & 0xFF;
    std::cout << "  RGB(" << std::dec << std::setw(3) << r << ","
              << std::setw(3) << g << "," << std::setw(3) << b
              << ") count=" << count << std::endl;
  }

  // Trace a specific pixel manually to verify logic
  int testX = 16; // Second tile column (first visible after any margin)
  int testY = 8;  // Second row of tiles

  std::cout << "\n=== Manual trace for pixel (" << testX << "," << testY
            << ") ===" << std::endl;

  // Read BG0 settings
  uint16_t bg0cnt = mem.Read16(0x04000008);
  int charBaseBlock = (bg0cnt >> 2) & 3;
  int screenBaseBlock = (bg0cnt >> 8) & 0x1F;
  uint32_t charBase = 0x06000000 + (charBaseBlock * 0x4000);
  uint32_t screenBase = 0x06000000 + (screenBaseBlock * 0x800);

  // Read scroll
  uint16_t hofs = mem.Read16(0x04000010);
  uint16_t vofs = mem.Read16(0x04000012);

  int scrolledX = (testX + hofs) % 256;
  int scrolledY = (testY + vofs) % 256;

  int mapX = scrolledX / 8;
  int mapY = scrolledY / 8;

  uint32_t mapAddr = screenBase + (mapY * 32 + mapX) * 2;
  uint16_t tileEntry = mem.Read16(mapAddr);

  int tileIndex = tileEntry & 0x3FF;
  int hFlip = (tileEntry >> 10) & 1;
  int vFlip = (tileEntry >> 11) & 1;
  int paletteBank = (tileEntry >> 12) & 0xF;

  std::cout << "BG0CNT=0x" << std::hex << bg0cnt << std::endl;
  std::cout << "charBase=0x" << charBase << " screenBase=0x" << screenBase
            << std::endl;
  std::cout << "HOFS=" << std::dec << hofs << " VOFS=" << vofs << std::endl;
  std::cout << "Scrolled position: (" << scrolledX << "," << scrolledY << ")"
            << std::endl;
  std::cout << "Map position: (" << mapX << "," << mapY << ")" << std::endl;
  std::cout << "Map address: 0x" << std::hex << mapAddr << std::endl;
  std::cout << "Tile entry: 0x" << tileEntry << std::endl;
  std::cout << "  Tile index: " << std::dec << tileIndex << std::endl;
  std::cout << "  H flip: " << hFlip << std::endl;
  std::cout << "  V flip: " << vFlip << std::endl;
  std::cout << "  Palette bank: " << paletteBank << " (masked to "
            << (paletteBank & 7) << ")" << std::endl;

  // Read tile data
  int inTileX = scrolledX % 8;
  int inTileY = scrolledY % 8;
  uint32_t tileAddr =
      charBase + (tileIndex * 32) + (inTileY * 4) + (inTileX / 2);
  uint8_t tileByte = mem.Read8(tileAddr);
  int colorIndex = (inTileX & 1) ? ((tileByte >> 4) & 0xF) : (tileByte & 0xF);

  std::cout << "In-tile pos: (" << inTileX << "," << inTileY << ")"
            << std::endl;
  std::cout << "Tile address: 0x" << std::hex << tileAddr << std::endl;
  std::cout << "Tile byte: 0x" << (int)tileByte << std::endl;
  std::cout << "Color index: " << std::dec << colorIndex << std::endl;

  // Apply Classic NES offset
  int effectiveIndex = (colorIndex != 0) ? colorIndex + 8 : 0;
  std::cout << "Effective index (with +8): " << effectiveIndex << std::endl;

  // Read palette color
  uint32_t palAddr =
      0x05000000 + ((paletteBank & 7) * 32) + (effectiveIndex * 2);
  uint16_t color = mem.Read16(palAddr);
  std::cout << "Palette address: 0x" << std::hex << palAddr << std::endl;
  std::cout << "Color: 0x" << color << std::endl;

  // Actual framebuffer pixel
  uint32_t fbPixel = fb[testY * 240 + testX];
  std::cout << "\nActual framebuffer pixel: 0x" << std::hex << fbPixel
            << std::endl;

  return 0;
}
