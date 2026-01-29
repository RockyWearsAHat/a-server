// Analyze actual tile data at charBase
#include <emulator/gba/GBA.h>
#include <emulator/gba/GBAMemory.h>
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

  // Run for 100 frames
  for (int f = 0; f < 100; f++) {
    for (int i = 0; i < 280896; i++) {
      gba.Step();
    }
  }

  uint32_t charBase = 0x06004000;

  std::cout << "Analyzing first few tiles at charBase 0x06004000:\n"
            << std::endl;

  // Show first 8 tiles (32 bytes each in 4bpp)
  for (int tile = 0; tile < 8; tile++) {
    std::cout << "Tile " << tile << " (at 0x" << std::hex
              << (charBase + tile * 32) << "):" << std::endl;

    // Each tile is 8x8 pixels, 4bpp = 32 bytes
    for (int row = 0; row < 8; row++) {
      std::cout << "  Row " << row << ": ";
      // Each row is 4 bytes (8 pixels * 4 bits / 8)
      for (int col = 0; col < 4; col++) {
        uint8_t byte = mem.Read8(charBase + tile * 32 + row * 4 + col);
        // Extract two pixels from byte
        int lowPixel = byte & 0x0F;
        int highPixel = (byte >> 4) & 0x0F;
        std::cout << std::hex << lowPixel << highPixel << " ";
      }
      std::cout << std::endl;
    }
    std::cout << std::endl;
  }

  // Also check the screen map at 0x06006800
  std::cout << "\n=== Screen map at 0x06006800 (first 64 entries) ===\n";
  for (int i = 0; i < 64; i++) {
    uint16_t entry = mem.Read16(0x06006800 + i * 2);
    int tileIndex = entry & 0x3FF;
    int hFlip = (entry >> 10) & 1;
    int vFlip = (entry >> 11) & 1;
    int palette = (entry >> 12) & 0xF;

    if (i % 8 == 0)
      std::cout << std::endl << std::dec << "  Row " << (i / 8) << ": ";
    std::cout << std::hex << std::setfill('0');
    std::cout << std::setw(3) << tileIndex;
    if (hFlip)
      std::cout << "h";
    if (vFlip)
      std::cout << "v";
    std::cout << "p" << palette << " ";
  }
  std::cout << std::dec << std::endl;

  return 0;
}
