// Check DISPCNT and BG control registers in detail
#include "emulator/gba/GBA.h"
#include "emulator/gba/PPU.h"
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

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

  std::cout << "=== Display Control Registers ===" << std::endl;
  std::cout << std::hex << std::setfill('0');

  uint16_t DISPCNT = mem.Read16(0x04000000);
  uint16_t DISPSTAT = mem.Read16(0x04000004);
  uint16_t VCOUNT = mem.Read16(0x04000006);
  uint16_t BG0CNT = mem.Read16(0x04000008);
  uint16_t BG1CNT = mem.Read16(0x0400000A);
  uint16_t BG2CNT = mem.Read16(0x0400000C);
  uint16_t BG3CNT = mem.Read16(0x0400000E);

  std::cout << "DISPCNT:  0x" << std::setw(4) << DISPCNT << std::endl;
  int mode = DISPCNT & 0x7;
  std::cout << "  Mode: " << mode << std::endl;
  std::cout << "  BG0 enabled: " << ((DISPCNT >> 8) & 1) << std::endl;
  std::cout << "  BG1 enabled: " << ((DISPCNT >> 9) & 1) << std::endl;
  std::cout << "  BG2 enabled: " << ((DISPCNT >> 10) & 1) << std::endl;
  std::cout << "  BG3 enabled: " << ((DISPCNT >> 11) & 1) << std::endl;
  std::cout << "  OBJ enabled: " << ((DISPCNT >> 12) & 1) << std::endl;
  std::cout << "  Frame select: " << ((DISPCNT >> 4) & 1) << std::endl;

  std::cout << "\nDISPSTAT: 0x" << std::setw(4) << DISPSTAT << std::endl;
  std::cout << "VCOUNT:   " << std::dec << VCOUNT << std::endl;

  auto decodeBGCNT = [](uint16_t cnt, int bgNum) {
    std::cout << "\nBG" << bgNum << "CNT:   0x" << std::hex << std::setw(4)
              << std::setfill('0') << cnt << std::endl;
    int priority = cnt & 0x3;
    int charBase = (cnt >> 2) & 0x3; // 16KB blocks
    int mosaic = (cnt >> 6) & 0x1;
    int colorMode = (cnt >> 7) & 0x1;   // 0=4bpp, 1=8bpp
    int screenBase = (cnt >> 8) & 0x1F; // 2KB blocks
    int overflow = (cnt >> 13) & 0x1;   // Only for BG2/3 affine
    int screenSize = (cnt >> 14) & 0x3;

    std::cout << "  Priority: " << std::dec << priority << std::endl;
    std::cout << "  CharBase: block " << charBase << " (0x0600" << std::hex
              << std::setw(4) << (charBase * 0x4000) << ")" << std::endl;
    std::cout << "  ScreenBase: block " << std::dec << screenBase << " (0x0600"
              << std::hex << std::setw(4) << (screenBase * 0x800) << ")"
              << std::endl;
    std::cout << "  Color mode: "
              << (colorMode ? "8bpp (256 colors)" : "4bpp (16 colors)")
              << std::endl;
    std::cout << "  Mosaic: " << mosaic << std::endl;
    std::cout << "  Screen size: " << std::dec << screenSize << " (";
    const char *sizes[] = {"256x256", "512x256", "256x512", "512x512"};
    std::cout << sizes[screenSize] << ")" << std::endl;
  };

  decodeBGCNT(BG0CNT, 0);
  decodeBGCNT(BG1CNT, 1);
  decodeBGCNT(BG2CNT, 2);
  decodeBGCNT(BG3CNT, 3);

  // Check scroll registers
  std::cout << "\n=== Scroll Registers ===" << std::endl;
  uint16_t BG0HOFS = mem.Read16(0x04000010);
  uint16_t BG0VOFS = mem.Read16(0x04000012);
  std::cout << "BG0 scroll: H=" << std::dec << (int16_t)(BG0HOFS & 0x1FF)
            << ", V=" << (int16_t)(BG0VOFS & 0x1FF) << std::endl;

  // Save the frame
  savePPM("ogdk_registers.ppm", gba.GetPPU());
  std::cout << "\nSaved ogdk_registers.ppm" << std::endl;

  return 0;
}
