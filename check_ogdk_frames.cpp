// Generate PPM at different frame counts to see if the game is progressing
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

#include "emulator/gba/GBA.h"
#include "emulator/gba/PPU.h"

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

  // Save frames at different points
  std::vector<int> framePoints = {60, 120, 180, 240, 300, 600, 1200};

  uint64_t targetCycles = 0;
  uint64_t totalCycles = 0;
  int framePointIndex = 0;

  std::cout << "Generating frames at different points..." << std::endl;

  while (framePointIndex < framePoints.size()) {
    targetCycles = framePoints[framePointIndex] * CYCLES_PER_FRAME;

    while (totalCycles < targetCycles) {
      totalCycles += gba.Step();
    }

    std::string filename =
        "ogdk_frame_" + std::to_string(framePoints[framePointIndex]) + ".ppm";
    savePPM(filename, gba.GetPPU());
    std::cout << "Saved " << filename << std::endl;

    framePointIndex++;
  }

  // Also save current state info
  auto &mem = gba.GetMemory();
  std::cout << "\n=== State at frame 1200 ===" << std::endl;
  std::cout << "PC: 0x" << std::hex << gba.GetPC() << std::endl;
  std::cout << "BG0CNT: 0x" << mem.Read16(0x04000008) << std::endl;
  std::cout << "DISPCNT: 0x" << mem.Read16(0x04000000) << std::dec << std::endl;

  return 0;
}
