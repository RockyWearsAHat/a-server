// Dump VRAM tiles and compare structure
#include <emulator/gba/GBA.h>
#include <emulator/gba/GBAMemory.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <vector>

using namespace AIO::Emulator::GBA;

void dumpTileAsText(const uint8_t *data, int offset, std::ostream &out) {
  // 4bpp tile: 32 bytes = 8 rows x 4 bytes
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 4; col++) {
      uint8_t byte = data[offset + row * 4 + col];
      uint8_t lo = byte & 0xF;
      uint8_t hi = (byte >> 4) & 0xF;
      out << std::hex << (int)lo << (int)hi;
    }
    out << "\n";
  }
}

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

  uint16_t bg0cnt = mem.Read16(0x04000008);
  uint32_t charBase = 0x06000000 + (((bg0cnt >> 2) & 3) * 0x4000);
  uint32_t screenBase = 0x06000000 + (((bg0cnt >> 8) & 0x1F) * 0x800);

  std::cout << "BG0CNT = 0x" << std::hex << bg0cnt << std::endl;
  std::cout << "Char base = 0x" << charBase << std::endl;
  std::cout << "Screen base = 0x" << screenBase << std::endl;

  // Read VRAM into buffer
  std::vector<uint8_t> vram(0x18000);
  for (int i = 0; i < 0x18000; i++) {
    vram[i] = mem.Read8(0x06000000 + i);
  }

  // Dump first 8 tiles used in the tilemap
  std::cout << "\n=== First 8 Tilemap Entries and Their Tiles ===" << std::endl;
  for (int i = 0; i < 8; i++) {
    uint16_t entry = mem.Read16(screenBase + i * 2);
    uint16_t tileIndex = entry & 0x3FF;
    uint8_t palette = (entry >> 12) & 0xF;

    std::cout << "\n[" << std::dec << i << "] Entry 0x" << std::hex
              << std::setw(4) << entry << " -> Tile " << std::dec << tileIndex
              << " (pal " << (int)palette << ")" << std::endl;

    // Calculate tile address
    uint32_t tileOffset = (charBase - 0x06000000) + (tileIndex * 32);
    if (tileOffset + 32 <= vram.size()) {
      dumpTileAsText(vram.data(), tileOffset, std::cout);
    } else {
      std::cout << "(tile out of bounds)" << std::endl;
    }
  }

  // Count unique tile indices used
  std::set<uint16_t> usedTiles;
  for (int i = 0; i < 32 * 32; i++) {
    uint16_t entry = mem.Read16(screenBase + i * 2);
    uint16_t tileIndex = entry & 0x3FF;
    usedTiles.insert(tileIndex);
  }
  std::cout << "\n=== Statistics ===" << std::endl;
  std::cout << "Unique tiles used: " << usedTiles.size() << std::endl;

  // Show range of tiles used
  if (!usedTiles.empty()) {
    std::cout << "Tile index range: " << *usedTiles.begin() << " - "
              << *usedTiles.rbegin() << std::endl;
  }

  // Dump tilemap as a 2D array showing tile indices
  std::cout << "\n=== Tilemap Grid (tile indices) ===" << std::endl;
  for (int row = 0; row < 20; row++) {
    for (int col = 0; col < 30; col++) {
      uint16_t entry = mem.Read16(screenBase + (row * 32 + col) * 2);
      uint16_t tileIndex = entry & 0x3FF;
      std::cout << std::hex << std::setw(3) << tileIndex << " ";
    }
    std::cout << std::endl;
  }

  return 0;
}
