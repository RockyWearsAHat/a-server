// Try pressing START button to see if game responds
#include "emulator/gba/GBA.h"
#include "emulator/gba/PPU.h"
#include <cstdint>
#include <fstream>
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

  // Run 30 frames to get to stable state
  while (totalCycles < 30 * CYCLES_PER_FRAME) {
    totalCycles += gba.Step();
  }

  savePPM("ogdk_before_input.ppm", gba.GetPPU());
  std::cout << "Before input - PC: 0x" << std::hex << gba.GetPC() << std::dec
            << std::endl;

  // Press START button using UpdateInput
  // KEYINPUT: bits are active LOW (0 = pressed, 1 = not pressed)
  // Bit 3 = Start button
  gba.UpdateInput(~(1 << 3)); // Press START (all bits high except START)

  // Run 60 more frames
  uint64_t inputCycles = 0;
  while (inputCycles < 60 * CYCLES_PER_FRAME) {
    inputCycles += gba.Step();
  }

  savePPM("ogdk_after_start.ppm", gba.GetPPU());
  std::cout << "After START - PC: 0x" << std::hex << gba.GetPC() << std::dec
            << std::endl;

  // Release START, press A
  gba.UpdateInput(~(1 << 0)); // Press A (bit 0)

  // Run 60 more frames
  inputCycles = 0;
  while (inputCycles < 60 * CYCLES_PER_FRAME) {
    inputCycles += gba.Step();
  }

  savePPM("ogdk_after_a.ppm", gba.GetPPU());
  std::cout << "After A - PC: 0x" << std::hex << gba.GetPC() << std::dec
            << std::endl;

  // Compare hashes
  auto &mem = gba.GetMemory();
  std::cout << "\nKEYINPUT register: 0x" << std::hex << mem.Read16(0x04000130)
            << std::dec << std::endl;

  return 0;
}
