// Check specific tiles referenced in tilemap
#include "emulator/gba/GBA.h"
#include "emulator/gba/PPU.h"
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>

int main() {
  AIO::Emulator::GBA::GBA gba;
  gba.LoadROM("OG-DK.gba");

  const uint64_t CYCLES_PER_FRAME = 280896;
  uint64_t totalCycles = 0;

  // Run 30 frames
  while (totalCycles < 30 * CYCLES_PER_FRAME) {
    totalCycles += gba.Step();
  }

  auto &mem = gba.GetMemory();

  // Tiles mentioned in tilemap: 247, 251, 248, 87, etc.
  int tiles[] = {0, 1, 247, 248, 251, 87, 510, 436, 32, 14};

  // CharBase is 0x06004000, 4bpp = 32 bytes per tile
  for (int i = 0; i < 10; i++) {
    int tile = tiles[i];
    uint32_t addr = 0x06004000 + tile * 32;

    std::cout << "=== Tile " << std::dec << tile << " at 0x" << std::hex << addr
              << " ===" << std::endl;

    // Print as 8 rows of 8 pixels
    for (int row = 0; row < 8; row++) {
      uint32_t rowData = mem.Read32(addr + row * 4);
      std::cout << "  ";
      for (int col = 0; col < 8; col++) {
        int pixel = (rowData >> (col * 4)) & 0xF;
        char c = (pixel == 0) ? '.' : ('0' + pixel);
        if (pixel > 9)
          c = 'A' + pixel - 10;
        std::cout << c;
      }
      std::cout << std::endl;
    }
  }

  // Check what's at high tile addresses
  std::cout << "\n=== Checking memory at tile 247 region ===" << std::endl;
  uint32_t addr247 = 0x06004000 + 247 * 32;
  std::cout << "Tile 247 at: 0x" << std::hex << addr247 << std::endl;

  // Check if charBase wraps or is different
  std::cout << "\nBG0CNT: 0x" << std::hex << mem.Read16(0x04000008)
            << std::endl;
  int charBase = (mem.Read16(0x04000008) >> 2) & 0x3;
  std::cout << "CharBase block: " << charBase << " -> 0x" << std::hex
            << (0x06000000 + charBase * 0x4000) << std::endl;

  return 0;
}
