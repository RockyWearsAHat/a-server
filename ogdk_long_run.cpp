// Generate PPMs at more frames to see if game shows any progress
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

  // Save frames at longer intervals
  std::vector<int> framePoints = {10, 30, 60, 120, 300, 600, 1800, 3600};

  uint64_t totalCycles = 0;
  int framePointIndex = 0;

  std::cout << "Generating frames..." << std::endl;

  while (framePointIndex < framePoints.size()) {
    uint64_t targetCycles = framePoints[framePointIndex] * CYCLES_PER_FRAME;

    while (totalCycles < targetCycles) {
      totalCycles += gba.Step();
    }

    std::string filename =
        "ogdk_f" + std::to_string(framePoints[framePointIndex]) + ".ppm";
    savePPM(filename, gba.GetPPU());
    std::cout << "Frame " << framePoints[framePointIndex] << " - PC: 0x"
              << std::hex << gba.GetPC() << std::dec << std::endl;

    framePointIndex++;
  }

  return 0;
}
