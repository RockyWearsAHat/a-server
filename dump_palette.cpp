// Dump full palette 0 to verify Classic NES color layout
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

  // Run for 10 frames
  for (int f = 0; f < 10; f++) {
    for (int i = 0; i < 280896; i++) {
      gba.Step();
    }
  }

  std::cout << std::hex << std::setfill('0');

  std::cout << "=== Palette 0 (BG palette) at 0x05000000 ===" << std::endl;
  for (int i = 0; i < 16; i++) {
    uint16_t color = mem.Read16(0x05000000 + i * 2);
    int r = (color & 0x1F);
    int g = ((color >> 5) & 0x1F);
    int b = ((color >> 10) & 0x1F);
    std::cout << "  Index " << std::dec << std::setw(2) << i << ": 0x"
              << std::hex << std::setw(4) << color;
    if (color == 0) {
      std::cout << " (black/transparent)";
    } else {
      std::cout << " RGB(" << std::dec << (r * 8) << "," << (g * 8) << ","
                << (b * 8) << ")";
    }
    std::cout << std::endl;
  }

  std::cout << "\n=== For Classic NES workaround: ===" << std::endl;
  std::cout << "Tile color index 1 + 8 offset = palette index 9" << std::endl;
  uint16_t c9 = mem.Read16(0x05000000 + 9 * 2);
  std::cout << "Palette index 9 = 0x" << std::hex << c9 << std::endl;

  // Also dump palette 8 to confirm it's zeros
  std::cout << "\n=== Palette 8 (what tiles reference before mask) ==="
            << std::endl;
  for (int i = 0; i < 16; i++) {
    uint16_t color = mem.Read16(0x05000100 + i * 2); // palette 8 = offset 0x100
    std::cout << "  Index " << std::dec << std::setw(2) << i << ": 0x"
              << std::hex << std::setw(4) << color << std::endl;
  }

  return 0;
}
