// Analyze the screen map structure to understand tile layout
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

#include "emulator/gba/GBA.h"
#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/PPU.h"

int main() {
  AIO::Emulator::GBA::GBA gba;

  std::string romPath = "OG-DK.gba";
  gba.LoadROM(romPath);

  // Run for 200 frames worth of cycles (~200 * 280896 cycles)
  for (uint64_t i = 0; i < 200 * 280896; ++i) {
    gba.Step();
  }

  auto &mem = gba.GetMemory();

  // BG0CNT = 0x8D04
  // Screen base = block 13 = 0x06006800
  // Char base = block 1 = 0x06004000
  // Size = 2 (256x512)
  // 4bpp mode

  uint32_t screenBase = 0x06006800;
  uint32_t charBase = 0x06004000;

  std::cout << "=== Screen Map Analysis ===" << std::endl;
  std::cout << "Screen base: 0x" << std::hex << screenBase << std::endl;
  std::cout << "Char base: 0x" << charBase << std::endl;

  // For BG size 2 (256x512), we have 32x64 tiles = 2048 entries
  // But visible screen is 30x20 tiles (240x160 pixels)

  std::cout << "\n=== Visible Screen Map (30x20 tiles, 240x160 pixels) ==="
            << std::endl;

  // Analyze the tile index pattern
  std::map<uint16_t, int> tileUsage;

  for (int tileY = 0; tileY < 20; ++tileY) {
    std::cout << "Row " << std::dec << tileY << " (Y=" << tileY * 8 << "-"
              << tileY * 8 + 7 << "): ";

    for (int tileX = 0; tileX < 30; ++tileX) {
      // In 256x512 mode, it's 32 tiles wide
      uint32_t mapOffset = (tileY * 32 + tileX) * 2;
      uint16_t mapEntry = mem.Read16(screenBase + mapOffset);

      uint16_t tileIndex = mapEntry & 0x3FF;
      uint8_t hFlip = (mapEntry >> 10) & 1;
      uint8_t vFlip = (mapEntry >> 11) & 1;
      uint8_t paletteBank = (mapEntry >> 12) & 0xF;

      tileUsage[tileIndex]++;

      // Print abbreviated: tile index in hex
      if (tileX < 15) { // First half of visible row
        printf("%03X ", tileIndex);
      }
    }
    std::cout << "..." << std::endl;
  }

  std::cout << "\n=== Tile Index Distribution ===" << std::endl;
  std::cout << "Unique tiles used: " << tileUsage.size() << std::endl;

  // Find min/max indices
  uint16_t minIdx = 0xFFFF, maxIdx = 0;
  for (auto &[idx, count] : tileUsage) {
    if (idx < minIdx)
      minIdx = idx;
    if (idx > maxIdx)
      maxIdx = idx;
  }
  std::cout << "Tile index range: " << std::hex << minIdx << " - " << maxIdx
            << std::dec << std::endl;

  // Show most used tiles
  std::cout << "\nMost used tile indices:" << std::endl;
  std::vector<std::pair<uint16_t, int>> sorted(tileUsage.begin(),
                                               tileUsage.end());
  std::sort(sorted.begin(), sorted.end(),
            [](auto &a, auto &b) { return a.second > b.second; });

  for (int i = 0; i < std::min(10, (int)sorted.size()); ++i) {
    std::cout << "  Tile 0x" << std::hex << sorted[i].first << ": " << std::dec
              << sorted[i].second << " times" << std::endl;
  }

  // Now let's look at the actual tile data to see if it makes sense
  std::cout << "\n=== Tile Data Analysis ===" << std::endl;

  // For 4bpp, each tile is 32 bytes (8x8 pixels, 4 bits each)
  // Check tile 0 (should be blank?)
  std::cout << "Tile 0 data (first 8 bytes): ";
  for (int i = 0; i < 8; ++i) {
    printf("%02X ", mem.Read8(charBase + i));
  }
  std::cout << std::endl;

  // Check tile 1
  std::cout << "Tile 1 data (first 8 bytes): ";
  for (int i = 0; i < 8; ++i) {
    printf("%02X ", mem.Read8(charBase + 32 + i));
  }
  std::cout << std::endl;

  // Check if there's NES-style data structure
  // NES uses 2 bitplanes per 8x8 tile (16 bytes total)
  // GBA 4bpp uses 32 bytes per tile

  std::cout << "\n=== Checking for NES tile structure ===" << std::endl;

  // In NES:
  // - First 8 bytes = low bit plane (bit 0 of each pixel)
  // - Next 8 bytes = high bit plane (bit 1 of each pixel)
  // - Each byte is one row of 8 pixels

  // In GBA 4bpp:
  // - Each byte contains 2 pixels (low nibble = left pixel, high nibble = right
  // pixel)
  // - 4 bytes per row, 8 rows per tile = 32 bytes total

  // Let's see if the game is writing NES-format data to GBA VRAM
  std::cout << "First tile's raw data (32 bytes for 4bpp):" << std::endl;
  for (int row = 0; row < 8; ++row) {
    std::cout << "  Row " << row << ": ";
    for (int col = 0; col < 4; ++col) {
      printf("%02X ", mem.Read8(charBase + row * 4 + col));
    }
    std::cout << std::endl;
  }

  // Check a few more tiles
  std::cout << "\nTile 0x100 data (first 8 bytes): ";
  for (int i = 0; i < 8; ++i) {
    printf("%02X ", mem.Read8(charBase + 0x100 * 32 + i));
  }
  std::cout << std::endl;

  std::cout << "\nTile 0x1FF data (first 8 bytes): ";
  for (int i = 0; i < 8; ++i) {
    printf("%02X ", mem.Read8(charBase + 0x1FF * 32 + i));
  }
  std::cout << std::endl;

  return 0;
}
