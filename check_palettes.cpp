// Check all palette banks
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

  // Run for about 100 frames
  for (int f = 0; f < 100; f++) {
    for (int i = 0; i < 280896; i++) {
      gba.Step();
    }
  }

  std::cout << std::hex << std::setfill('0');

  // Dump ALL palette RAM (512 bytes)
  std::cout << "=== Full Palette RAM Dump ===" << std::endl;
  std::cout << "BG Palettes (0x05000000 - 0x050001FF):" << std::endl;
  for (int pal = 0; pal < 16; pal++) {
    uint32_t baseAddr = 0x05000000 + pal * 32;
    std::cout << "  Palette " << std::dec << std::setw(2) << pal << ": ";
    bool hasData = false;
    for (int c = 0; c < 16; c++) {
      uint16_t color = mem.Read16(baseAddr + c * 2);
      if (color != 0)
        hasData = true;
    }
    if (hasData) {
      std::cout << std::hex;
      for (int c = 0; c < 16; c++) {
        uint16_t color = mem.Read16(baseAddr + c * 2);
        std::cout << std::setw(4) << color << " ";
      }
    } else {
      std::cout << "(all zeros)";
    }
    std::cout << std::endl;
  }

  std::cout << "\nOBJ Palettes (0x05000200 - 0x050003FF):" << std::endl;
  for (int pal = 0; pal < 16; pal++) {
    uint32_t baseAddr = 0x05000200 + pal * 32;
    std::cout << "  Palette " << std::dec << std::setw(2) << pal << ": ";
    bool hasData = false;
    for (int c = 0; c < 16; c++) {
      uint16_t color = mem.Read16(baseAddr + c * 2);
      if (color != 0)
        hasData = true;
    }
    if (hasData) {
      std::cout << std::hex;
      for (int c = 0; c < 16; c++) {
        uint16_t color = mem.Read16(baseAddr + c * 2);
        std::cout << std::setw(4) << color << " ";
      }
    } else {
      std::cout << "(all zeros)";
    }
    std::cout << std::endl;
  }

  // Check what the PPU's Classic NES mode does
  std::cout << "\n=== Tilemap Entry Analysis ===" << std::endl;
  uint16_t bg0cnt = mem.Read16(0x04000008);
  uint32_t screenBase = 0x06000000 + (((bg0cnt >> 8) & 0x1F) * 0x800);

  std::cout << "Unique palette indices used in tilemap:" << std::endl;
  uint16_t palettes_used = 0;
  for (int i = 0; i < 32 * 32; i++) {
    uint16_t entry = mem.Read16(screenBase + i * 2);
    uint8_t pal = (entry >> 12) & 0xF;
    palettes_used |= (1 << pal);
  }
  for (int p = 0; p < 16; p++) {
    if (palettes_used & (1 << p)) {
      std::cout << "  Palette " << p << " is used" << std::endl;
    }
  }

  return 0;
}
