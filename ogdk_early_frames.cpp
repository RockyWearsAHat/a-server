// Capture frames 1-30 to see when garbled screen appears
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

  // Save every frame from 1-35
  for (int frame = 1; frame <= 35; frame++) {
    uint64_t targetCycles = frame * CYCLES_PER_FRAME;
    uint64_t totalCycles = 0;

    // Reset and run to this frame
    gba.LoadROM("OG-DK.gba"); // This resets
    while (totalCycles < targetCycles) {
      totalCycles += gba.Step();
    }

    std::string filename = "ogdk_early_f" + std::to_string(frame) + ".ppm";
    savePPM(filename, gba.GetPPU());

    auto &mem = gba.GetMemory();
    std::cout << "Frame " << frame << " PC=0x" << std::hex << gba.GetPC()
              << " DISPCNT=0x" << mem.Read16(0x04000000) << " BG0CNT=0x"
              << mem.Read16(0x04000008) << std::dec << std::endl;
  }

  return 0;
}
