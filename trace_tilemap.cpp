// Track VRAM writes during early boot to understand NES emulator
#include "emulator/gba/GBA.h"
#include "emulator/gba/PPU.h"
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>

void savePPM(const std::string &filename, const AIO::Emulator::GBA::PPU &ppu) {
  const auto &fb = ppu.GetFramebuffer();
  std::ofstream out(filename, std::ios::binary);
  out << "P6\n240 160\n255\n";
  for (int i = 0; i < 240 * 160; ++i) {
    uint32_t pixel = fb[i];
    uint8_t r = (pixel >> 16) & 0xFF;
    uint8_t g = (pixel >> 8) & 0xFF;
    uint8_t b = pixel & 0xFF;
    out.put(r).put(g).put(b);
  }
  out.close();
}

int main() {
  AIO::Emulator::GBA::GBA gba;
  gba.LoadROM("OG-DK.gba");

  const uint64_t CYCLES_PER_FRAME = 280896;
  uint64_t totalCycles = 0;

  auto &mem = gba.GetMemory();

  // Run to frame 5 and check tilemap at that point
  while (totalCycles < 5 * CYCLES_PER_FRAME) {
    totalCycles += gba.Step();
  }

  std::cout << "=== Frame 5 Tilemap Analysis ===" << std::endl;

  // Check unique tile indices used in tilemap
  std::set<int> uniqueTiles;
  std::map<int, int> tileCounts;

  // 256x512 = 32x64 entries = 2048 entries
  for (int i = 0; i < 2048; i++) {
    uint16_t entry = mem.Read16(0x06006800 + i * 2);
    int tileIndex = entry & 0x3FF;
    uniqueTiles.insert(tileIndex);
    tileCounts[tileIndex]++;
  }

  std::cout << "Unique tiles in tilemap: " << uniqueTiles.size() << std::endl;
  std::cout << "Tile index range: " << *uniqueTiles.begin() << " to "
            << *uniqueTiles.rbegin() << std::endl;

  std::cout << "\nMost common tiles:" << std::endl;
  // Sort by count
  std::vector<std::pair<int, int>> sorted(tileCounts.begin(), tileCounts.end());
  std::sort(sorted.begin(), sorted.end(),
            [](auto &a, auto &b) { return a.second > b.second; });
  for (int i = 0; i < 20 && i < sorted.size(); i++) {
    std::cout << "  Tile " << std::setw(4) << sorted[i].first << ": "
              << sorted[i].second << " times" << std::endl;
  }

  savePPM("ogdk_frame5.ppm", gba.GetPPU());
  std::cout << "\nSaved ogdk_frame5.ppm" << std::endl;

  // Now run to frame 15 (when garbled screen appears)
  while (totalCycles < 15 * CYCLES_PER_FRAME) {
    totalCycles += gba.Step();
  }

  std::cout << "\n=== Frame 15 Tilemap Analysis ===" << std::endl;
  uniqueTiles.clear();
  tileCounts.clear();

  for (int i = 0; i < 2048; i++) {
    uint16_t entry = mem.Read16(0x06006800 + i * 2);
    int tileIndex = entry & 0x3FF;
    uniqueTiles.insert(tileIndex);
    tileCounts[tileIndex]++;
  }

  std::cout << "Unique tiles in tilemap: " << uniqueTiles.size() << std::endl;
  std::cout << "Tile index range: " << *uniqueTiles.begin() << " to "
            << *uniqueTiles.rbegin() << std::endl;

  std::cout << "\nMost common tiles:" << std::endl;
  sorted.assign(tileCounts.begin(), tileCounts.end());
  std::sort(sorted.begin(), sorted.end(),
            [](auto &a, auto &b) { return a.second > b.second; });
  for (int i = 0; i < 20 && i < sorted.size(); i++) {
    std::cout << "  Tile " << std::setw(4) << sorted[i].first << ": "
              << sorted[i].second << " times" << std::endl;
  }

  // Check for tiles > 319 (those that would overlap with tilemap)
  int overlappingCount = 0;
  for (auto &t : uniqueTiles) {
    if (t >= 320)
      overlappingCount++;
  }
  std::cout << "\nTiles >= 320 (overlap with tilemap): " << overlappingCount
            << std::endl;

  savePPM("ogdk_frame15.ppm", gba.GetPPU());
  std::cout << "Saved ogdk_frame15.ppm" << std::endl;

  return 0;
}
