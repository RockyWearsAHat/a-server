// Quick test to trace OG-DK early execution and see tilemap state
#include "emulator/gba/GBA.h"
#include <iomanip>
#include <iostream>
#include <set>

using namespace AIO::Emulator::GBA;

int main() {
  GBA gba;
  if (!gba.LoadROM("/Users/alexwaldmann/Desktop/AIO Server/OG-DK.gba")) {
    std::cerr << "Failed to load ROM" << std::endl;
    return 1;
  }

  auto &mem = gba.GetMemory();

  std::cout << "=== OG-DK Early Execution Trace ===" << std::endl;

  // Check IWRAM at 0x0300750C BEFORE running
  std::cout << "\n=== IWRAM at 0x0300750C BEFORE execution ===" << std::endl;
  for (int i = 0; i < 32; i++) {
    if (i % 8 == 0)
      std::cout << std::hex << "0x" << (0x0300750C + i) << ": ";
    std::cout << std::setw(2) << std::setfill('0')
              << (int)mem.Read8(0x0300750C + i) << " ";
    if ((i + 1) % 8 == 0)
      std::cout << std::endl;
  }
  std::cout << std::dec << std::setfill(' ');

  // Run 10 frames worth of steps
  constexpr int CYCLES_PER_FRAME = 280896;
  for (int frame = 0; frame < 10; frame++) {
    int cycles = 0;
    while (cycles < CYCLES_PER_FRAME) {
      cycles += gba.Step();
    }
  }

  // Check IWRAM at 0x0300750C AFTER running
  std::cout << "\n=== IWRAM at 0x0300750C AFTER execution ===" << std::endl;
  for (int i = 0; i < 32; i++) {
    if (i % 8 == 0)
      std::cout << std::hex << "0x" << (0x0300750C + i) << ": ";
    std::cout << std::setw(2) << std::setfill('0')
              << (int)mem.Read8(0x0300750C + i) << " ";
    if ((i + 1) % 8 == 0)
      std::cout << std::endl;
  }
  std::cout << std::dec << std::setfill(' ');

  // Dump tilemap state after 10 frames
  std::cout << "\n=== Tilemap at 0x06006800 (first 32 entries) ==="
            << std::endl;
  for (int i = 0; i < 32; i++) {
    uint16_t entry = mem.Read16(0x06006800 + i * 2);
    uint16_t tileIndex = entry & 0x3FF;
    uint16_t palette = (entry >> 12) & 0xF;
    bool hflip = entry & 0x400;
    bool vflip = entry & 0x800;

    std::cout << "[" << std::setw(2) << i << "] Tile " << std::setw(3)
              << tileIndex << " pal " << palette;
    if (hflip)
      std::cout << " H";
    if (vflip)
      std::cout << " V";
    std::cout << " (raw=0x" << std::hex << entry << std::dec << ")"
              << std::endl;
  }

  // Count unique tiles used
  std::set<uint16_t> uniqueTiles;
  for (int i = 0; i < 1024; i++) {
    uint16_t entry = mem.Read16(0x06006800 + i * 2);
    uniqueTiles.insert(entry & 0x3FF);
  }
  std::cout << "\nUnique tiles in tilemap: " << uniqueTiles.size() << std::endl;

  // Check if tilemap looks initialized or random
  int zeroTiles = 0;
  for (int i = 0; i < 1024; i++) {
    uint16_t entry = mem.Read16(0x06006800 + i * 2);
    if ((entry & 0x3FF) == 0)
      zeroTiles++;
  }
  std::cout << "Zero tiles: " << zeroTiles << " / 1024" << std::endl;

  return 0;
}
