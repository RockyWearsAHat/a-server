// Generate PPM after longer run and check framebuffer stability
#include <emulator/gba/GBA.h>
#include <fstream>
#include <iostream>

using namespace AIO::Emulator::GBA;

int main() {
  GBA gba;
  if (!gba.LoadROM("OG-DK.gba")) {
    std::cerr << "Failed to load ROM" << std::endl;
    return 1;
  }

  auto &ppu = gba.GetPPU();

  // Run for 200 frames
  for (int f = 0; f < 200; f++) {
    for (int i = 0; i < 280896; i++) {
      gba.Step();
    }
  }

  // Get framebuffer and write
  auto &fb = ppu.GetFramebuffer();

  std::ofstream ppm("ogdk_200frames.ppm");
  ppm << "P3\n240 160\n255\n";
  for (int y = 0; y < 160; y++) {
    for (int x = 0; x < 240; x++) {
      uint32_t pixel = fb[y * 240 + x];
      int r = (pixel >> 16) & 0xFF;
      int g = (pixel >> 8) & 0xFF;
      int b = pixel & 0xFF;
      ppm << r << " " << g << " " << b << " ";
    }
    ppm << "\n";
  }
  ppm.close();

  // Check if it's mostly black or has content
  int nonBlack = 0;
  for (auto p : fb) {
    if ((p & 0xFFFFFF) != 0)
      nonBlack++;
  }

  std::cout << "Frame 200: " << nonBlack << " non-black pixels" << std::endl;

  // Run 100 more frames
  for (int f = 0; f < 100; f++) {
    for (int i = 0; i < 280896; i++) {
      gba.Step();
    }
  }

  auto &fb2 = ppu.GetFramebuffer();
  nonBlack = 0;
  for (auto p : fb2) {
    if ((p & 0xFFFFFF) != 0)
      nonBlack++;
  }
  std::cout << "Frame 300: " << nonBlack << " non-black pixels" << std::endl;

  std::ofstream ppm2("ogdk_300frames.ppm");
  ppm2 << "P3\n240 160\n255\n";
  for (int y = 0; y < 160; y++) {
    for (int x = 0; x < 240; x++) {
      uint32_t pixel = fb2[y * 240 + x];
      int r = (pixel >> 16) & 0xFF;
      int g = (pixel >> 8) & 0xFF;
      int b = pixel & 0xFF;
      ppm2 << r << " " << g << " " << b << " ";
    }
    ppm2 << "\n";
  }
  ppm2.close();

  std::cout << "Wrote ogdk_200frames.ppm and ogdk_300frames.ppm" << std::endl;

  return 0;
}
