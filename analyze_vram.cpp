// Check what's at the game's double-buffer addresses
#include "emulator/gba/GBA.h"
#include "emulator/gba/PPU.h"
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>

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

  // The game uses 0x06003200 and 0x06006800
  // 0x06003200 = charBase(0x4000) + 0x3200 - 0x4000 = -0xE00??? No...
  // Let me think about this differently:

  // 256x512 tilemap = 32x64 tiles = 2048 entries = 4096 bytes
  // ScreenBase 13 = 0x06006800
  // For a 256x512 screen (size 2), tilemap spans 0x6800-0x7800 (4KB)

  // CharBase 1 = 0x06004000
  // With 4bpp tiles, each tile is 32 bytes
  // Maximum tile index with 10 bits = 1023
  // Tiles 0-1023 span 0x6004000 to 0x6004000 + 1024*32 = 0x600C000

  std::cout << "=== VRAM Layout Analysis ===" << std::endl;
  std::cout << "CharBase: 0x06004000 (tiles 0-1023)" << std::endl;
  std::cout << "  Tile 0:    0x06004000" << std::endl;
  std::cout << "  Tile 100:  0x" << std::hex << (0x06004000 + 100 * 32)
            << std::endl;
  std::cout << "  Tile 247:  0x" << std::hex << (0x06004000 + 247 * 32)
            << std::endl;
  std::cout << "  Tile 510:  0x" << std::hex << (0x06004000 + 510 * 32)
            << std::endl;
  std::cout << "  Tile 1023: 0x" << std::hex << (0x06004000 + 1023 * 32)
            << std::endl;

  std::cout << "\nScreenBase: 0x06006800 (tilemap)" << std::endl;
  std::cout << "  For 256x512, tilemap is 4KB: 0x06006800-0x06007800"
            << std::endl;

  std::cout << "\nGame buffer addresses:" << std::endl;
  std::cout << "  0x06003200 - this is BEFORE charBase (0x4000)!" << std::endl;
  std::cout << "  0x06006800 - same as screenBase" << std::endl;

  // So the game's buffer at 0x06003200 OVERLAPS with nothing useful
  // But 0x06006800 IS the tilemap!

  // Wait - the NES emulator might be writing directly to VRAM as a framebuffer
  // Let's check what mode the game SHOULD be in

  std::cout << "\n=== Checking if game writes tilemap or bitmap data ==="
            << std::endl;

  // Dump raw bytes at 0x06003200 and 0x06006800
  std::cout << "\nData at 0x06003200 (first 64 bytes):" << std::endl;
  for (int i = 0; i < 64; i++) {
    if (i % 16 == 0)
      std::cout << std::hex << std::setw(8) << (0x06003200 + i) << ": ";
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << (int)mem.Read8(0x06003200 + i) << " ";
    if ((i + 1) % 16 == 0)
      std::cout << std::endl;
  }

  std::cout << "\nData at 0x06006800 (first 64 bytes, this is the tilemap):"
            << std::endl;
  for (int i = 0; i < 64; i++) {
    if (i % 16 == 0)
      std::cout << std::hex << std::setw(8) << (0x06006800 + i) << ": ";
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << (int)mem.Read8(0x06006800 + i) << " ";
    if ((i + 1) % 16 == 0)
      std::cout << std::endl;
  }

  // Check if 0x06003200 is used as ANOTHER tilemap
  // The double-buffer might be for tilemaps, not framebuffers
  std::cout << "\nDecoding 0x06003200 as tilemap entries:" << std::endl;
  for (int i = 0; i < 16; i++) {
    uint16_t entry = mem.Read16(0x06003200 + i * 2);
    int tileIndex = entry & 0x3FF;
    int hFlip = (entry >> 10) & 1;
    int vFlip = (entry >> 11) & 1;
    int palette = (entry >> 12) & 0xF;

    std::cout << "[" << std::setw(2) << std::dec << i << "] raw=0x" << std::hex
              << std::setw(4) << entry << " tile=" << std::setw(4) << std::dec
              << tileIndex << " hf=" << hFlip << " vf=" << vFlip
              << " pal=" << palette << std::endl;
  }

  return 0;
}
