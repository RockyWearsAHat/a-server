// Generate a PPM frame and analyze what we're rendering
#include <emulator/gba/GBA.h>
#include <emulator/gba/GBAMemory.h>
#include <fstream>
#include <iomanip>
#include <iostream>

using namespace AIO::Emulator::GBA;

int main() {
  GBA gba;
  if (!gba.LoadROM("OG-DK.gba")) {
    std::cerr << "Failed to load ROM" << std::endl;
    return 1;
  }

  auto &mem = gba.GetMemory();

  // Run for about 100 frames
  std::cout << "Running emulator..." << std::endl;
  for (int f = 0; f < 100; f++) {
    for (int i = 0; i < 280896; i++) {
      gba.Step();
    }
  }

  // Get framebuffer from PPU
  const std::vector<uint32_t> &fbVec = gba.GetPPU().GetFramebuffer();
  if (fbVec.empty()) {
    std::cerr << "No framebuffer!" << std::endl;
    return 1;
  }
  const uint32_t *fb = fbVec.data();

  // Write PPM file
  std::ofstream ppm("ogdk_debug.ppm", std::ios::binary);
  ppm << "P6\n240 160\n255\n";
  for (int y = 0; y < 160; y++) {
    for (int x = 0; x < 240; x++) {
      uint32_t pixel = fb[y * 240 + x];
      uint8_t r = (pixel >> 16) & 0xFF;
      uint8_t g = (pixel >> 8) & 0xFF;
      uint8_t b = pixel & 0xFF;
      ppm.put(r);
      ppm.put(g);
      ppm.put(b);
    }
  }
  ppm.close();
  std::cout << "Wrote ogdk_debug.ppm" << std::endl;

  // Analyze color distribution
  std::cout << "\n=== Color Analysis ===" << std::endl;
  int colorCounts[65536] = {0};
  for (int i = 0; i < 240 * 160; i++) {
    uint32_t pixel = fb[i];
    // Convert to 15-bit
    uint8_t r = ((pixel >> 16) & 0xFF) >> 3;
    uint8_t g = ((pixel >> 8) & 0xFF) >> 3;
    uint8_t b = (pixel & 0xFF) >> 3;
    uint16_t c15 = r | (g << 5) | (b << 10);
    colorCounts[c15]++;
  }

  std::cout << "Unique colors used:" << std::endl;
  for (int i = 0; i < 65536; i++) {
    if (colorCounts[i] > 0) {
      int r = (i & 0x1F) << 3;
      int g = ((i >> 5) & 0x1F) << 3;
      int b = ((i >> 10) & 0x1F) << 3;
      std::cout << "  0x" << std::hex << std::setw(4) << std::setfill('0') << i
                << " RGB(" << std::dec << r << "," << g << "," << b << ")"
                << " count=" << colorCounts[i] << std::endl;
    }
  }

  // Check if any pixels are non-black
  int nonBlack = 0;
  for (int i = 0; i < 240 * 160; i++) {
    if (fb[i] != 0 && fb[i] != 0xFF000000)
      nonBlack++;
  }
  std::cout << "\nNon-black pixels: " << nonBlack << " / " << (240 * 160)
            << std::endl;

  // Sample some pixel values in a grid
  std::cout << "\n=== Pixel Sample (8x8 grid) ===" << std::endl;
  for (int y = 0; y < 160; y += 20) {
    for (int x = 0; x < 240; x += 30) {
      uint32_t pixel = fb[y * 240 + x];
      uint8_t r = (pixel >> 16) & 0xFF;
      uint8_t g = (pixel >> 8) & 0xFF;
      uint8_t b = pixel & 0xFF;
      if (r == 0 && g == 0 && b == 0) {
        std::cout << "--- ";
      } else {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)r
                  << std::setw(2) << (int)g << std::setw(2) << (int)b << " ";
      }
    }
    std::cout << std::endl;
  }

  return 0;
}
