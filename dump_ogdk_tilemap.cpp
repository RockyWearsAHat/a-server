// Dump tilemap at screenBase 13 (0x06006800) to understand OG-DK corruption
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>

#include "emulator/gba/GBA.h"
#include "emulator/gba/PPU.h"

int main() {
  AIO::Emulator::GBA::GBA gba;
  gba.LoadROM("OG-DK.gba");

  const uint64_t CYCLES_PER_FRAME = 280896;

  // Run for 120 frames to let the game initialize
  for (int f = 0; f < 120; f++) {
    uint64_t target = (f + 1) * CYCLES_PER_FRAME;
    uint64_t current = 0;
    while (current < target) {
      current += gba.Step();
    }
  }

  auto &mem = gba.GetMemory();

  // Dump the IWRAM code around 0x030054E0 where SWI 0x02 is called
  std::cout << "=== IWRAM code at 0x030054D0 (SWI call site) ===" << std::endl;
  for (uint32_t addr = 0x030054D0; addr < 0x03005510; addr += 2) {
    uint16_t insn = mem.Read16(addr);
    std::cout << "0x" << std::hex << addr << ": " << std::setw(4)
              << std::setfill('0') << insn << std::endl;
  }

  // Check what the decompressed NES tilemap data should look like
  // at the source address in ROM (0x08002739)
  std::cout << "\n=== ROM data at 0x08002739 (first 64 bytes) ===" << std::endl;
  for (int i = 0; i < 64; i++) {
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << (int)mem.Read8(0x08002739 + i) << " ";
    if ((i + 1) % 16 == 0)
      std::cout << std::endl;
  }

  return 0;
}

std::cout << "=== OG-DK VRAM Analysis at Frame 120 ===" << std::endl;
std::cout << "DISPCNT: 0x" << std::hex << mem.Read16(0x04000000) << std::endl;
std::cout << "BG0CNT: 0x" << mem.Read16(0x04000008) << std::dec << std::endl;

uint16_t bg0cnt = mem.Read16(0x04000008);
int charBase = (bg0cnt >> 2) & 0x3;
int screenBase = (bg0cnt >> 8) & 0x1F;
int screenSize = (bg0cnt >> 14) & 0x3;

std::cout << "charBase: " << charBase << " (0x" << std::hex
          << (0x06000000 + charBase * 0x4000) << ")" << std::endl;
std::cout << "screenBase: " << std::dec << screenBase << " (0x" << std::hex
          << (0x06000000 + screenBase * 0x800) << ")" << std::endl;
std::cout << "screenSize: " << std::dec << screenSize << std::endl;

// Dump first 64 tilemap entries (first 4 rows of 32x32 tiles)
uint32_t tilemapAddr = 0x06000000 + screenBase * 0x800;
std::cout << "\n=== Tilemap at 0x" << std::hex << tilemapAddr
          << " (first 64 entries) ===" << std::endl;

for (int row = 0; row < 4; row++) {
  std::cout << "Row " << row << ": ";
  for (int col = 0; col < 16; col++) {
    uint16_t entry = mem.Read16(tilemapAddr + (row * 32 + col) * 2);
    std::cout << std::hex << std::setw(4) << std::setfill('0') << entry << " ";
  }
  std::cout << std::endl;
}

// Check what tile 0 contains (should be blank)
uint32_t tile0Addr = 0x06000000 + charBase * 0x4000;
std::cout << "\n=== Tile 0 data at 0x" << std::hex << tile0Addr
          << " ===" << std::endl;
for (int i = 0; i < 32; i++) {
  std::cout << std::hex << std::setw(2) << std::setfill('0')
            << (int)mem.Read8(tile0Addr + i) << " ";
  if ((i + 1) % 8 == 0)
    std::cout << std::endl;
}

// Check tile 247 (the one at top-left)
uint32_t tile247Addr = tile0Addr + 247 * 32;
std::cout << "\n=== Tile 247 data at 0x" << std::hex << tile247Addr
          << " ===" << std::endl;
for (int i = 0; i < 32; i++) {
  std::cout << std::hex << std::setw(2) << std::setfill('0')
            << (int)mem.Read8(tile247Addr + i) << " ";
  if ((i + 1) % 8 == 0)
    std::cout << std::endl;
}

// Check palette at index 11 (where cyan comes from)
std::cout << "\n=== Palette bank 0 (first 16 colors) ===" << std::endl;
for (int i = 0; i < 16; i++) {
  uint16_t color = mem.Read16(0x05000000 + i * 2);
  int r = (color & 0x1F) << 3;
  int g = ((color >> 5) & 0x1F) << 3;
  int b = ((color >> 10) & 0x1F) << 3;
  std::cout << "Index " << std::dec << i << ": 0x" << std::hex << color
            << " = RGB(" << std::dec << r << "," << g << "," << b << ")"
            << std::endl;
}

// Also dump the OTHER tilemap buffer at 0x06003200
uint32_t altTilemapAddr = 0x06003200;
std::cout << "\n=== ALTERNATIVE Tilemap at 0x" << std::hex << altTilemapAddr
          << " (first 64 entries) ===" << std::endl;
for (int row = 0; row < 4; row++) {
  std::cout << "Row " << std::dec << row << ": ";
  for (int col = 0; col < 16; col++) {
    uint16_t entry = mem.Read16(altTilemapAddr + (row * 32 + col) * 2);
    std::cout << std::hex << std::setw(4) << std::setfill('0') << entry << " ";
  }
  std::cout << std::endl;
}

// Check if the screenBase might point to 6 (0x6003000) on some frames
std::cout << "\n=== What screenBase 6 would be (0x6003000) ===" << std::endl;
uint32_t sb6Addr = 0x06003000;
for (int row = 0; row < 4; row++) {
  std::cout << "Row " << std::dec << row << ": ";
  for (int col = 0; col < 16; col++) {
    uint16_t entry = mem.Read16(sb6Addr + (row * 32 + col) * 2);
    std::cout << std::hex << std::setw(4) << std::setfill('0') << entry << " ";
  }
  std::cout << std::endl;
}

return 0;
}
