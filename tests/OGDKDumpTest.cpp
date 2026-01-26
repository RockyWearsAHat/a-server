#include "../include/emulator/gba/GBA.h"
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <vector>

using namespace AIO::Emulator::GBA;

int main() {
  GBA gba;
  if (!gba.LoadROM("OG-DK.gba")) {
    std::cerr << "Failed to load ROM" << std::endl;
    return 1;
  }
  gba.Reset();

  const int TARGET_FRAMES = 300; // Try more frames
  const int CYCLES_PER_FRAME = 280896;

  for (int frame = 0; frame < TARGET_FRAMES; frame++) {
    int totalCycles = 0;
    while (totalCycles < CYCLES_PER_FRAME && !gba.IsCPUHalted()) {
      int cycles = gba.Step(); // This internally calls memory->AdvanceCycles()
                               // which updates PPU
      totalCycles += cycles;
    }
    // If halted, advance remaining cycles
    if (gba.IsCPUHalted()) {
      int remaining = CYCLES_PER_FRAME - totalCycles;
      if (remaining > 0) {
        gba.GetMemory().AdvanceCycles(remaining);
      }
    }
  }

  // Ensure we get a complete frame rendered
  gba.GetPPU().SwapBuffers();

  std::cout << "=== After 200 frames ===" << std::endl;

  uint16_t dispcnt = gba.GetMemory().Read16(0x04000000);
  std::cout << "DISPCNT: 0x" << std::hex << dispcnt << std::dec << std::endl;
  std::cout << "  Mode: " << (dispcnt & 0x7) << std::endl;
  std::cout << "  BG0 enable: " << ((dispcnt >> 8) & 1) << std::endl;
  std::cout << "  BG1 enable: " << ((dispcnt >> 9) & 1) << std::endl;
  std::cout << "  BG2 enable: " << ((dispcnt >> 10) & 1) << std::endl;
  std::cout << "  BG3 enable: " << ((dispcnt >> 11) & 1) << std::endl;
  std::cout << "  OBJ enable: " << ((dispcnt >> 12) & 1) << std::endl;

  uint16_t bg0cnt = gba.GetMemory().Read16(0x04000008);
  std::cout << "BG0CNT: 0x" << std::hex << bg0cnt << std::dec << std::endl;

  uint16_t bg1cnt = gba.GetMemory().Read16(0x0400000A);
  std::cout << "BG1CNT: 0x" << std::hex << bg1cnt << std::dec << std::endl;
  int bg1charBase = ((bg1cnt >> 2) & 0x3) * 0x4000;
  int bg1screenBase = ((bg1cnt >> 8) & 0x1F) * 0x800;
  bool bg1is8bpp = (bg1cnt >> 7) & 1;
  std::cout << "  BG1 charBase: 0x" << std::hex << bg1charBase << std::dec
            << std::endl;
  std::cout << "  BG1 screenBase: 0x" << std::hex << bg1screenBase << std::dec
            << std::endl;
  std::cout << "  BG1 is8bpp: " << bg1is8bpp << std::endl;

  int charBase = ((bg0cnt >> 2) & 0x3) * 0x4000;
  int screenBase = ((bg0cnt >> 8) & 0x1F) * 0x800;
  bool is8bpp = (bg0cnt >> 7) & 1;

  std::cout << "charBase: 0x" << std::hex << charBase << std::dec << std::endl;
  std::cout << "screenBase: 0x" << std::hex << screenBase << std::dec
            << std::endl;
  std::cout << "is8bpp: " << is8bpp << std::endl;

  // Dump first few tiles from VRAM charBase to see actual tile data
  std::cout << "\n=== VRAM tile data at charBase 0x" << std::hex
            << (0x06000000 + charBase) << " ===" << std::dec << std::endl;
  for (int tileNum = 0; tileNum < 5; tileNum++) {
    std::cout << "Tile " << tileNum << " raw bytes: ";
    for (int b = 0; b < 32; b++) {
      uint8_t byte =
          gba.GetMemory().Read8(0x06000000 + charBase + tileNum * 32 + b);
      std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)byte
                << " ";
    }
    std::cout << std::dec << std::endl;
  }

  // Dump tile 247 (0xf7) which is used by screen map entry 0
  std::cout << "\nTile 247 (0xf7) raw bytes: ";
  for (int b = 0; b < 32; b++) {
    uint8_t byte = gba.GetMemory().Read8(0x06000000 + charBase + 247 * 32 + b);
    std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)byte
              << " ";
  }
  std::cout << std::dec << std::endl;

  std::cout << "Tile 247 decoded (nibbles):" << std::endl;
  for (int row = 0; row < 8; row++) {
    std::cout << "  Row " << row << ": ";
    for (int col = 0; col < 4; col++) {
      uint8_t byte = gba.GetMemory().Read8(0x06000000 + charBase + 247 * 32 +
                                           row * 4 + col);
      int lo = byte & 0xF;
      int hi = (byte >> 4) & 0xF;
      std::cout << lo << " " << hi << " ";
    }
    std::cout << std::endl;
  }

  std::cout << "\n=== Screen Map (first 10 tiles) ===" << std::endl;
  for (int i = 0; i < 10; i++) {
    uint32_t addr = 0x06000000 + screenBase + i * 2;
    uint16_t entry = gba.GetMemory().Read16(addr);
    int tileIndex = entry & 0x3FF;
    int palBank = (entry >> 12) & 0xF;
    std::cout << "Tile " << i << ": entry=0x" << std::hex << entry
              << " idx=" << std::dec << tileIndex << " palBank=" << palBank
              << std::endl;
  }

  std::cout << "\n=== Tile 0 data (32 bytes, 4bpp) ===" << std::endl;
  for (int row = 0; row < 8; row++) {
    uint32_t addr = 0x06000000 + charBase + row * 4;
    std::cout << "Row " << row << ": ";
    for (int col = 0; col < 4; col++) {
      uint8_t b = gba.GetMemory().Read8(addr + col);
      std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b
                << " ";
    }
    std::cout << std::endl;
  }

  std::cout << "\n=== Looking for non-zero tiles ===" << std::endl;
  for (int i = 0; i < 100; i++) {
    uint32_t addr = 0x06000000 + screenBase + i * 2;
    uint16_t entry = gba.GetMemory().Read16(addr);
    int tileIndex = entry & 0x3FF;
    if (tileIndex != 0) {
      std::cout << "First non-zero at pos " << i << ": tileIndex=" << tileIndex
                << std::endl;
      std::cout << "Tile " << tileIndex
                << " data (nibbles = color indices):" << std::endl;
      for (int row = 0; row < 8; row++) {
        uint32_t tileAddr = 0x06000000 + charBase + tileIndex * 32 + row * 4;
        std::cout << "  Row " << row << ": ";
        for (int col = 0; col < 4; col++) {
          uint8_t b = gba.GetMemory().Read8(tileAddr + col);
          std::cout << std::hex << (b & 0xF) << " " << ((b >> 4) & 0xF) << " ";
        }
        std::cout << std::endl;
      }
      break;
    }
  }

  // Also dump framebuffer to PPM for visual inspection
  std::cout << "\n=== Writing frame to /tmp/ogdk_test.ppm ===" << std::endl;
  std::ofstream ppm("/tmp/ogdk_test.ppm");
  ppm << "P3\n240 160\n255\n";
  auto &ppu = gba.GetPPU();
  const auto &fb = ppu.GetFramebuffer();
  std::set<uint32_t> uniqueColors;
  for (int y = 0; y < 160; y++) {
    for (int x = 0; x < 240; x++) {
      uint32_t c = fb[y * 240 + x];
      uint8_t r = (c >> 16) & 0xFF;
      uint8_t g = (c >> 8) & 0xFF;
      uint8_t b = c & 0xFF;
      ppm << (int)r << " " << (int)g << " " << (int)b << " ";
      uniqueColors.insert(c & 0x00FFFFFF);
    }
    ppm << "\n";
  }
  ppm.close();
  std::cout << "Unique colors in frame: " << uniqueColors.size() << std::endl;
  for (auto c : uniqueColors) {
    std::cout << "  RGB(" << ((c >> 16) & 0xFF) << "," << ((c >> 8) & 0xFF)
              << "," << (c & 0xFF) << ")" << std::endl;
  }

  // Also dump the DMA source buffer
  std::cout << "\n=== IWRAM DMA source buffer 0x0300750c (first 64 bytes) ==="
            << std::endl;
  for (int i = 0; i < 64; i += 4) {
    uint32_t addr = 0x0300750c + i;
    uint32_t val = gba.GetMemory().Read32(addr);
    std::cout << "0x" << std::hex << addr << ": 0x" << std::setw(8)
              << std::setfill('0') << val << std::endl;
  }

  // Dump VRAM palette source at 0x0600095c
  std::cout << "\n=== VRAM palette source 0x0600095c (first 64 bytes) ==="
            << std::endl;
  for (int i = 0; i < 64; i += 4) {
    uint32_t addr = 0x0600095c + i;
    uint32_t val = gba.GetMemory().Read32(addr);
    std::cout << "0x" << std::hex << addr << ": 0x" << std::setw(8)
              << std::setfill('0') << val << std::dec << std::endl;
  }

  // Dump palette bank 0 (offset 0-31) and bank 8 (offset 256-287)
  std::cout << "\n=== Palette bank 0 vs bank 8 ===" << std::endl;
  std::cout << "Bank 0 (offset 0-31):" << std::endl;
  for (int i = 0; i < 16; i++) {
    uint16_t c = gba.GetMemory().Read16(0x05000000 + i * 2);
    std::cout << "  [" << i << "] = 0x" << std::hex << c << std::dec
              << std::endl;
  }
  std::cout << "Bank 8 (offset 256-287):" << std::endl;
  for (int i = 0; i < 16; i++) {
    uint16_t c = gba.GetMemory().Read16(0x05000100 + i * 2);
    std::cout << "  [" << (128 + i) << "] = 0x" << std::hex << c << std::dec
              << std::endl;
  }

  std::cout << "\n=== Raw screen map at screenBase (hex dump) ===" << std::endl;
  for (int i = 0; i < 20; i++) {
    uint32_t addr = 0x06000000 + screenBase + i * 2;
    uint8_t lo = gba.GetMemory().Read8(addr);
    uint8_t hi = gba.GetMemory().Read8(addr + 1);
    uint16_t entry = lo | (hi << 8);
    int tileIndex = entry & 0x3FF;
    int palBank = (entry >> 12) & 0xF;
    std::cout << "MapEntry[" << i << "] @0x" << std::hex << addr
              << ": bytes=" << std::setw(2) << std::setfill('0') << (int)lo
              << " " << std::setw(2) << (int)hi << " -> entry=0x"
              << std::setw(4) << entry << " tileIdx=" << std::dec << tileIndex
              << " palBank=" << palBank << std::endl;
  }

  std::cout << "\n=== Palette RAM (entries 128-160, bank 8) ===" << std::endl;
  for (int i = 128; i < 160; i++) {
    uint16_t color = gba.GetMemory().Read16(0x05000000 + i * 2);
    int r = color & 0x1F;
    int g = (color >> 5) & 0x1F;
    int b = (color >> 10) & 0x1F;
    std::cout << "Pal[" << std::dec << std::setw(3) << i << "]: 0x" << std::hex
              << std::setw(4) << std::setfill('0') << color << " RGB("
              << std::dec << r << "," << g << "," << b << ")" << std::endl;
  }

  return 0;
}
