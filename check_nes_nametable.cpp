// Check if VRAM contains NES-style nametable (8-bit per tile) instead of GBA
// tilemap (16-bit)
#include "emulator/gba/GBA.h"
#include <cstdint>
#include <iomanip>
#include <iostream>

int main() {
  AIO::Emulator::GBA::GBA gba;
  if (!gba.LoadROM("OG-DK.gba"))
    return 1;

  const uint64_t CYCLES_PER_FRAME = 280896;
  for (int f = 0; f < 120; f++) { // 120 frames = 2 seconds
    uint64_t target = (f + 1) * CYCLES_PER_FRAME;
    uint64_t current = 0;
    while (current < target) {
      current += gba.Step();
    }
  }

  auto &memory = gba.GetMemory();
  const uint8_t *vram = memory.GetVRAMData();

  // screenBase 13 = 0x6800 offset
  uint32_t mapOffset = 13 * 0x800;

  std::cout << "=== Analyzing tilemap at screenBase 13 (0x" << std::hex
            << (0x06000000 + mapOffset) << ") ===" << std::endl;

  // Print first 64 bytes (raw)
  std::cout << "\nRaw bytes (first 64):" << std::endl;
  for (int i = 0; i < 64; i++) {
    if (i % 16 == 0)
      std::cout << std::hex << std::setw(4) << std::setfill('0') << i << ": ";
    std::cout << std::setw(2) << std::setfill('0') << (int)vram[mapOffset + i]
              << " ";
    if (i % 16 == 15)
      std::cout << std::endl;
  }

  // Interpret as 8-bit NES nametable entries
  std::cout << "\nAs NES nametable (8-bit tiles):" << std::endl;
  for (int row = 0; row < 4; row++) {
    std::cout << "Row " << row << ": ";
    for (int col = 0; col < 32; col++) {
      uint8_t tile = vram[mapOffset + row * 32 + col];
      std::cout << std::setw(2) << std::setfill('0') << (int)tile << " ";
    }
    std::cout << std::endl;
  }

  // Interpret as GBA tilemap (16-bit entries)
  std::cout << "\nAs GBA tilemap (16-bit entries):" << std::endl;
  for (int row = 0; row < 4; row++) {
    std::cout << "Row " << row << ": ";
    for (int col = 0; col < 16;
         col++) { // Only 16 entries per row since each is 2 bytes
      int idx = row * 64 + col * 2;
      uint16_t entry = vram[mapOffset + idx] | (vram[mapOffset + idx + 1] << 8);
      std::cout << std::setw(4) << std::setfill('0') << entry << " ";
    }
    std::cout << std::endl;
  }

  // Check for NES pattern: Donkey Kong title screen should have repeated tile
  // patterns The NES screen is 32x30 tiles, and the border area would be filled
  // with a specific tile
  std::cout << "\n=== Statistical Analysis ===" << std::endl;
  int count[256] = {0};
  for (int i = 0; i < 32 * 30; i++) { // NES screen size
    count[vram[mapOffset + i]]++;
  }
  std::cout << "Most common bytes (if NES format):" << std::endl;
  for (int iter = 0; iter < 5; iter++) {
    int maxIdx = 0;
    for (int i = 0; i < 256; i++) {
      if (count[i] > count[maxIdx])
        maxIdx = i;
    }
    std::cout << "  Byte 0x" << std::hex << maxIdx << " appears " << std::dec
              << count[maxIdx] << " times" << std::endl;
    count[maxIdx] = 0;
  }

  return 0;
}
