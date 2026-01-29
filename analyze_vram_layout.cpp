// Analyze VRAM layout for OG-DK to understand tilemap/tile overlap
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "emulator/gba/GBA.h"

int main() {
  AIO::Emulator::GBA::GBA gba;
  gba.LoadROM("OG-DK.gba");

  const uint64_t CYCLES_PER_FRAME = 280896;

  // Run for 120 frames
  for (int f = 0; f < 120; f++) {
    uint64_t target = (f + 1) * CYCLES_PER_FRAME;
    uint64_t current = 0;
    while (current < target) {
      current += gba.Step();
    }
  }

  auto &mem = gba.GetMemory();

  std::cout << "=== OG-DK VRAM Layout Analysis ===" << std::endl;

  uint16_t bg0cnt = mem.Read16(0x04000008);
  int charBase = (bg0cnt >> 2) & 0x3;
  int screenBase = (bg0cnt >> 8) & 0x1F;

  uint32_t charAddr = 0x06000000 + charBase * 0x4000;
  uint32_t screenAddr = 0x06000000 + screenBase * 0x800;

  std::cout << "BG0CNT = 0x" << std::hex << bg0cnt << std::dec << std::endl;
  std::cout << "charBase = " << charBase << " (0x" << std::hex << charAddr
            << ")" << std::endl;
  std::cout << "screenBase = " << std::dec << screenBase << " (0x" << std::hex
            << screenAddr << ")" << std::endl;

  // Check if tilemap overlaps with tile data
  // charBase 1 = tiles from 0x06004000 to 0x06007FFF (16KB of tiles = 512 tiles
  // at 32 bytes each) screenBase 13 = tilemap at 0x06006800

  std::cout << "\n=== VRAM Regions ===" << std::endl;
  std::cout << "Tile data: 0x" << std::hex << charAddr << " - 0x"
            << (charAddr + 0x4000 - 1) << std::endl;
  std::cout << "Tilemap:   0x" << screenAddr << " - 0x"
            << (screenAddr + 0x800 - 1) << std::endl;

  // Check for overlap
  if (screenAddr >= charAddr && screenAddr < charAddr + 0x4000) {
    uint32_t overlap = screenAddr - charAddr;
    int firstOverlappingTile = overlap / 32;
    std::cout << "\n*** OVERLAP DETECTED! ***" << std::endl;
    std::cout << "Tilemap starts at tile offset " << std::dec
              << firstOverlappingTile << std::endl;
    std::cout << "Tiles 0-" << (firstOverlappingTile - 1) << " are safe"
              << std::endl;
    std::cout << "Tiles " << firstOverlappingTile << "+ overlap with tilemap!"
              << std::endl;
  }

  // Now let's see what the tilemap SHOULD look like
  // Read the first row - each entry is 16 bits:
  // bits 0-9: tile index
  // bit 10: hflip
  // bit 11: vflip
  // bits 12-15: palette bank
  std::cout << "\n=== Tilemap Analysis (first 32 entries = row 0) ==="
            << std::endl;
  for (int i = 0; i < 32; i++) {
    uint16_t entry = mem.Read16(screenAddr + i * 2);
    int tileIndex = entry & 0x3FF;
    int palBank = (entry >> 12) & 0xF;
    bool hflip = (entry >> 10) & 1;
    bool vflip = (entry >> 11) & 1;

    // Check if this tile index would overlap with tilemap
    bool overlaps = (charAddr + tileIndex * 32) >= screenAddr;

    std::cout << "[" << std::dec << std::setw(2) << i << "] 0x" << std::hex
              << std::setw(4) << std::setfill('0') << entry
              << " tile=" << std::dec << std::setw(3) << std::setfill(' ')
              << tileIndex << " pal=" << palBank << (hflip ? " H" : "  ")
              << (vflip ? "V" : " ") << (overlaps ? " *OVERLAP*" : "")
              << std::endl;
  }

  // What does a "good" tilemap look like? All zeros for black?
  std::cout << "\n=== Expected for blank title screen ===" << std::endl;
  std::cout << "Top-left should be tile 0 with palette bank 0 (black)"
            << std::endl;
  std::cout << "Entry 0 is: 0x" << std::hex << mem.Read16(screenAddr)
            << std::endl;

  // Check tile 0 content (should be all zeros for blank)
  std::cout << "\n=== Tile 0 at 0x" << std::hex << charAddr
            << " ===" << std::endl;
  bool allZero = true;
  for (int i = 0; i < 32; i++) {
    uint8_t b = mem.Read8(charAddr + i);
    if (b != 0)
      allZero = false;
    std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
    if ((i + 1) % 16 == 0)
      std::cout << std::endl;
  }
  std::cout << "Tile 0 is " << (allZero ? "BLANK (all zeros)" : "NOT BLANK")
            << std::endl;

  return 0;
}
