#include "emulator/gba/GBA.h"
#include <iomanip>
#include <iostream>

int main() {
  AIO::Emulator::GBA::GBA gba;
  if (!gba.LoadROM("OG-DK.gba"))
    return 1;

  const uint64_t CYCLES_PER_FRAME = 280896;
  for (int f = 0; f < 2400; f++) {
    uint64_t target = (f + 1) * CYCLES_PER_FRAME;
    uint64_t current = 0;
    while (current < target) {
      current += gba.Step();
    }
  }

  auto &memory = gba.GetMemory();
  const uint8_t *vram = memory.GetVRAMData();

  std::cout << "=== Scanning for valid tilemap patterns ===" << std::endl;

  // For a blank NES title screen area, we'd expect tile 0 entries
  // Let's scan VRAM for regions that have reasonable tilemap entries
  for (uint32_t screenBase = 0; screenBase <= 31; screenBase++) {
    uint32_t mapOffset = screenBase * 2048;
    int zeroTileCount = 0;
    int lowTileCount = 0; // tiles < 256

    // Check first 64 entries (2 rows of 32)
    for (int i = 0; i < 64; i++) {
      uint16_t entry =
          vram[mapOffset + i * 2] | (vram[mapOffset + i * 2 + 1] << 8);
      int tileIndex = entry & 0x3FF;
      if (tileIndex == 0)
        zeroTileCount++;
      if (tileIndex < 256)
        lowTileCount++;
    }

    std::cout << "screenBase=" << std::dec << screenBase << " (0x" << std::hex
              << (0x06000000 + mapOffset) << ")"
              << " zeroTiles=" << std::dec << zeroTileCount
              << " lowTiles=" << lowTileCount << " entry0=0x" << std::hex
              << (vram[mapOffset] | (vram[mapOffset + 1] << 8)) << std::endl;
  }

  // Now let's look at the actual BG0CNT to understand the intended config
  uint16_t bg0cnt = memory.Read16(0x04000008);
  int charBase = (bg0cnt >> 2) & 3;
  int screenBase = (bg0cnt >> 8) & 0x1F;
  int screenSize = (bg0cnt >> 14) & 3;

  std::cout << "\n=== Actual BG0CNT Configuration ===" << std::endl;
  std::cout << "BG0CNT = 0x" << std::hex << bg0cnt << std::endl;
  std::cout << "charBase = " << std::dec << charBase << " (0x" << std::hex
            << (charBase * 0x4000) << ")" << std::endl;
  std::cout << "screenBase = " << std::dec << screenBase << " (0x" << std::hex
            << (screenBase * 0x800) << ")" << std::endl;
  std::cout << "screenSize = " << std::dec << screenSize << std::endl;

  return 0;
}
