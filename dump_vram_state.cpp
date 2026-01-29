// Dump VRAM and tilemap state to understand the graphics corruption
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
  for (int f = 0; f < 100; f++) {
    for (int i = 0; i < 280896; i++) {
      gba.Step();
    }
  }

  std::cout << std::hex << std::setfill('0');

  // Graphics registers
  uint16_t dispcnt = mem.Read16(0x04000000);
  uint16_t bg0cnt = mem.Read16(0x04000008);
  uint16_t bg1cnt = mem.Read16(0x0400000A);
  uint16_t bg2cnt = mem.Read16(0x0400000C);
  uint16_t bg3cnt = mem.Read16(0x0400000E);

  std::cout << "=== Graphics Registers ===" << std::endl;
  std::cout << "  DISPCNT = 0x" << std::setw(4) << dispcnt << std::endl;
  std::cout << "    Mode: " << (dispcnt & 7) << std::endl;
  std::cout << "    BG0: " << ((dispcnt >> 8) & 1) << std::endl;
  std::cout << "    BG1: " << ((dispcnt >> 9) & 1) << std::endl;
  std::cout << "    BG2: " << ((dispcnt >> 10) & 1) << std::endl;
  std::cout << "    BG3: " << ((dispcnt >> 11) & 1) << std::endl;
  std::cout << "    OBJ: " << ((dispcnt >> 12) & 1) << std::endl;

  std::cout << "\n  BG0CNT = 0x" << std::setw(4) << bg0cnt << std::endl;
  std::cout << "    Priority: " << (bg0cnt & 3) << std::endl;
  std::cout << "    Char Base: " << ((bg0cnt >> 2) & 3) << " (0x0600"
            << std::setw(4) << (((bg0cnt >> 2) & 3) * 0x4000) << ")"
            << std::endl;
  std::cout << "    Screen Base: " << ((bg0cnt >> 8) & 0x1F) << " (0x0600"
            << std::setw(4) << (((bg0cnt >> 8) & 0x1F) * 0x800) << ")"
            << std::endl;
  std::cout << "    Size: " << ((bg0cnt >> 14) & 3) << std::endl;

  // BG0 tilemap base
  uint32_t bg0CharBase = 0x06000000 + (((bg0cnt >> 2) & 3) * 0x4000);
  uint32_t bg0ScreenBase = 0x06000000 + (((bg0cnt >> 8) & 0x1F) * 0x800);

  // Dump tilemap entries
  std::cout << "\n=== BG0 Tilemap (first 64 entries) ===" << std::endl;
  std::cout << "Screen base: 0x" << std::setw(8) << bg0ScreenBase << std::endl;
  for (int row = 0; row < 4; row++) {
    std::cout << "  Row " << row << ": ";
    for (int col = 0; col < 16; col++) {
      uint16_t entry = mem.Read16(bg0ScreenBase + (row * 32 + col) * 2);
      uint16_t tileNum = entry & 0x3FF;
      uint8_t palette = (entry >> 12) & 0xF;
      std::cout << std::setw(3) << tileNum << "p" << palette << " ";
    }
    std::cout << std::endl;
  }

  // Dump first few tiles and tile 0xF7
  std::cout << "\n=== Tile Data (first 4 tiles at char base) ===" << std::endl;
  std::cout << "Char base: 0x" << std::setw(8) << bg0CharBase << std::endl;
  for (int tile = 0; tile < 4; tile++) {
    std::cout << "  Tile " << tile << ": ";
    for (int i = 0; i < 8; i++) {
      std::cout << std::setw(2) << (int)mem.Read8(bg0CharBase + tile * 32 + i)
                << " ";
    }
    std::cout << "..." << std::endl;
  }

  // Dump tile 0xF7 (247) - the first tile in the tilemap
  std::cout << "\n=== Tile 0xF7 (247) - first tile referenced by tilemap ==="
            << std::endl;
  std::cout << "Address: 0x" << std::setw(8) << (bg0CharBase + 0xF7 * 32)
            << std::endl;
  for (int row = 0; row < 8; row++) {
    std::cout << "  Row " << row << ": ";
    for (int col = 0; col < 4; col++) {
      uint8_t b = mem.Read8(bg0CharBase + 0xF7 * 32 + row * 4 + col);
      // Show as nibbles (4bpp pixels)
      int lo = b & 0xF;
      int hi = (b >> 4) & 0xF;
      std::cout << std::hex << lo << hi << " ";
    }
    std::cout << std::endl;
  }

  // Find a blank tile (all zeros)
  std::cout << "\n=== Looking for blank (all-zero) tiles ===" << std::endl;
  for (int tile = 0; tile < 320; tile++) {
    bool allZero = true;
    for (int i = 0; i < 32; i++) {
      if (mem.Read8(bg0CharBase + tile * 32 + i) != 0) {
        allZero = false;
        break;
      }
    }
    if (allZero) {
      std::cout << "  Tile " << std::dec << tile << " (0x" << std::hex << tile
                << ") is blank" << std::endl;
    }
  }
  std::cout << std::dec;

  // Check tile 510 (0x1FE) - referenced by tilemap entry 1
  std::cout << "\n=== Tile 0x1FE (510) - overlaps with tilemap! ==="
            << std::endl;
  uint32_t tile510Addr = bg0CharBase + 510 * 32;
  std::cout << "Address: 0x" << std::hex << tile510Addr << std::dec
            << std::endl;
  std::cout << "Tilemap at 0x06006800, so tiles >= 320 overlap!" << std::endl;
  for (int row = 0; row < 8; row++) {
    std::cout << "  Row " << row << ": ";
    for (int col = 0; col < 4; col++) {
      uint8_t b = mem.Read8(tile510Addr + row * 4 + col);
      int lo = b & 0xF;
      int hi = (b >> 4) & 0xF;
      std::cout << std::hex << lo << hi << " ";
    }
    std::cout << std::endl;
  }
  std::cout << std::dec;

  // Check charBase=0 region for comparison
  std::cout << "\n=== CharBase=0 (0x06000000) for comparison ===" << std::endl;
  std::cout << "Checking if tile data exists at charBase=0:" << std::endl;
  int nonZeroAtBase0 = 0;
  for (int i = 0; i < 0x2000; i++) {
    if (mem.Read8(0x06000000 + i) != 0)
      nonZeroAtBase0++;
  }
  std::cout << "  Non-zero bytes in first 8KB: " << nonZeroAtBase0 << std::endl;

  // Show first few tiles at charBase=0
  std::cout << "First 4 tiles at charBase=0:" << std::endl;
  for (int tile = 0; tile < 4; tile++) {
    std::cout << "  Tile " << tile << ": ";
    for (int col = 0; col < 8; col++) {
      uint8_t b = mem.Read8(0x06000000 + tile * 32 + col);
      std::cout << std::hex << std::setw(2) << (int)b << " ";
    }
    std::cout << "..." << std::endl;
  }
  std::cout << std::dec;

  // Compare tile 0xF7 at charBase=0 vs charBase=1
  std::cout << "\n=== Comparing tile 0xF7 at different charBases ==="
            << std::endl;
  std::cout << "CharBase=0 (0x06000000 + 0xF7*32 = 0x06001EE0):" << std::endl;
  for (int row = 0; row < 8; row++) {
    std::cout << "  Row " << row << ": ";
    for (int col = 0; col < 4; col++) {
      uint8_t b = mem.Read8(0x06000000 + 0xF7 * 32 + row * 4 + col);
      int lo = b & 0xF;
      int hi = (b >> 4) & 0xF;
      std::cout << std::hex << lo << hi << " ";
    }
    std::cout << std::endl;
  }
  std::cout << "CharBase=1 (0x06004000 + 0xF7*32 = 0x06005EE0):" << std::endl;
  for (int row = 0; row < 8; row++) {
    std::cout << "  Row " << row << ": ";
    for (int col = 0; col < 4; col++) {
      uint8_t b = mem.Read8(0x06004000 + 0xF7 * 32 + row * 4 + col);
      int lo = b & 0xF;
      int hi = (b >> 4) & 0xF;
      std::cout << std::hex << lo << hi << " ";
    }
    std::cout << std::endl;
  }
  std::cout << std::dec;

  // Full palette dump
  std::cout << "\n=== Full Palette (BG) ===" << std::endl;
  for (int pal = 0; pal < 16; pal++) {
    std::cout << "  Palette " << std::dec << pal << ": " << std::hex;
    for (int c = 0; c < 16; c++) {
      uint16_t color = mem.Read16(0x05000000 + pal * 32 + c * 2);
      if (color != 0) {
        std::cout << std::setw(4) << color << " ";
      } else {
        std::cout << "---- ";
      }
    }
    std::cout << std::endl;
  }

  // Check VRAM usage
  std::cout << "\n=== VRAM Non-Zero Regions ===" << std::endl;
  for (uint32_t addr = 0x06000000; addr < 0x06018000; addr += 0x1000) {
    int nonZeroCount = 0;
    for (int i = 0; i < 0x1000; i++) {
      if (mem.Read8(addr + i) != 0)
        nonZeroCount++;
    }
    if (nonZeroCount > 0) {
      std::cout << "  0x" << std::setw(8) << addr << ": " << std::dec
                << nonZeroCount << " non-zero bytes" << std::endl;
    }
  }

  return 0;
}
