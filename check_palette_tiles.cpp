// Check palette RAM and tile data
#include "emulator/gba/GBA.h"
#include "emulator/gba/PPU.h"
#include <cstdint>
#include <fstream>
#include <iomanip>
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

// Decode GBA 15-bit color to RGB
void printColor(uint16_t color) {
  int r = (color & 0x1F) << 3;
  int g = ((color >> 5) & 0x1F) << 3;
  int b = ((color >> 10) & 0x1F) << 3;
  std::cout << "0x" << std::hex << std::setw(4) << std::setfill('0') << color
            << " (R" << std::dec << std::setw(3) << r << " G" << std::setw(3)
            << g << " B" << std::setw(3) << b << ")";
}

int main() {
  AIO::Emulator::GBA::GBA gba;
  gba.LoadROM("OG-DK.gba");

  const uint64_t CYCLES_PER_FRAME = 280896;
  uint64_t totalCycles = 0;

  // Run 200 frames (title screen with DONKEY KONG logo)
  while (totalCycles < 200 * CYCLES_PER_FRAME) {
    totalCycles += gba.Step();
  }

  auto &mem = gba.GetMemory();

  // Check BG palette (first 256 colors, 16 palettes of 16 colors each)
  std::cout << "=== BG Palettes (Palette RAM 0x05000000) ===" << std::endl;
  for (int pal = 0; pal < 16; pal++) {
    std::cout << "\nPalette " << std::dec << pal << ":" << std::endl;
    for (int c = 0; c < 16; c++) {
      uint16_t color = mem.Read16(0x05000000 + (pal * 32) + c * 2);
      std::cout << "  [" << std::setw(2) << c << "] ";
      printColor(color);
      std::cout << std::endl;
    }
  }

  // Check what tiles look like
  std::cout << "\n=== Tiles at CharBase 0x06004000 ===" << std::endl;

  // Dump specific tiles referenced in tilemap: 247, 510, 436, 32, 14
  int tilesToDump[] = {0, 1, 14, 32, 247, 436, 510};

  // In 4bpp mode, each tile is 32 bytes (8x8 pixels, 4 bits per pixel)
  for (int tile : tilesToDump) {
    uint32_t addr = 0x06004000 + tile * 32;
    std::cout << "\nTile " << std::dec << tile << " at 0x" << std::hex << addr
              << ":" << std::endl;

    // Print as 8 rows of 8 pixels
    for (int row = 0; row < 8; row++) {
      uint32_t rowData = mem.Read32(addr + row * 4);
      std::cout << "  ";
      for (int col = 0; col < 8; col++) {
        int pixel = (rowData >> (col * 4)) & 0xF;
        char c = (pixel == 0) ? '.' : ('0' + pixel);
        if (pixel > 9)
          c = 'A' + pixel - 10;
        std::cout << c;
      }
      std::cout << "  (0x" << std::hex << std::setw(8) << std::setfill('0')
                << rowData << ")" << std::endl;
    }
  }

  // Also dump colorIndex histogram for all tiles in tilemap
  std::cout << "\n=== ColorIndex histogram from tilemap tiles ===" << std::endl;
  int ciHist[16] = {0};
  int totalPix = 0;
  for (int row = 0; row < 20; row++) {
    for (int col = 0; col < 30; col++) {
      uint16_t entry = mem.Read16(0x06006800 + (row * 32 + col) * 2);
      int tileIdx = entry & 0x3FF;
      uint32_t tileAddr = 0x06004000 + tileIdx * 32;
      for (int py = 0; py < 8; py++) {
        uint32_t rowData = mem.Read32(tileAddr + py * 4);
        for (int px = 0; px < 8; px++) {
          int ci = (rowData >> (px * 4)) & 0xF;
          ciHist[ci]++;
          totalPix++;
        }
      }
    }
  }
  std::cout << "Total pixels: " << std::dec << totalPix << std::endl;
  for (int i = 0; i < 16; i++) {
    double pct = 100.0 * ciHist[i] / totalPix;
    std::cout << "  ci=" << std::setw(2) << i << ": " << std::setw(6)
              << ciHist[i] << " (" << std::fixed << std::setprecision(1) << pct
              << "%)" << std::endl;
  }

  // Check tilemap entries
  std::cout << "\n=== First row of tilemap at ScreenBase 0x06006800 ==="
            << std::endl;
  for (int i = 0; i < 32; i++) {
    uint16_t entry = mem.Read16(0x06006800 + i * 2);
    int tileIndex = entry & 0x3FF;
    int hFlip = (entry >> 10) & 1;
    int vFlip = (entry >> 11) & 1;
    int palette = (entry >> 12) & 0xF;

    std::cout << "[" << std::setw(2) << std::dec << i << "] "
              << "tile=" << std::setw(4) << tileIndex << " hf=" << hFlip
              << " vf=" << vFlip << " pal=" << std::setw(2) << palette
              << std::endl;
  }

  savePPM("ogdk_palette_check.ppm", gba.GetPPU());
  std::cout << "\nSaved ogdk_palette_check.ppm" << std::endl;

  return 0;
}
