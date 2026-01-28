// Check what's actually being rendered to screen buffer
#include "emulator/gba/GBA.h"
#include "emulator/gba/PPU.h"
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>

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

  // Run 30 frames
  while (totalCycles < 30 * CYCLES_PER_FRAME) {
    totalCycles += gba.Step();
  }

  auto &mem = gba.GetMemory();

  std::cout << "=== Screen Buffer Analysis ===" << std::endl;
  std::cout << std::hex;

  // Check both screen buffers
  std::cout << "\n=== Screen Base 0x06003200 (first 64 entries) ==="
            << std::endl;
  for (int i = 0; i < 64; i++) {
    uint16_t entry = mem.Read16(0x06003200 + i * 2);
    std::cout << std::setw(4) << std::setfill('0') << entry << " ";
    if ((i + 1) % 16 == 0)
      std::cout << std::endl;
  }

  std::cout << "\n=== Screen Base 0x06006800 (first 64 entries) ==="
            << std::endl;
  for (int i = 0; i < 64; i++) {
    uint16_t entry = mem.Read16(0x06006800 + i * 2);
    std::cout << std::setw(4) << std::setfill('0') << entry << " ";
    if ((i + 1) % 16 == 0)
      std::cout << std::endl;
  }

  // Check tile data at charBase 0x06004000
  std::cout << "\n=== Tile 0 at Char Base 0x06004000 (first 32 bytes) ==="
            << std::endl;
  for (int i = 0; i < 32; i++) {
    uint8_t byte = mem.Read8(0x06004000 + i);
    std::cout << std::setw(2) << std::setfill('0') << (int)byte << " ";
    if ((i + 1) % 16 == 0)
      std::cout << std::endl;
  }

  // Save frame at 30
  savePPM("ogdk_frame30.ppm", gba.GetPPU());

  // Now run 5 more frames
  for (int f = 0; f < 5; f++) {
    while (totalCycles < (31 + f) * CYCLES_PER_FRAME) {
      totalCycles += gba.Step();
    }

    std::string filename = "ogdk_frame" + std::to_string(31 + f) + ".ppm";
    savePPM(filename, gba.GetPPU());
  }

  std::cout << "\nSaved frames 30-35" << std::endl;

  return 0;
}
