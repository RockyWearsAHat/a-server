// Check what the custom decompressor wrote to IWRAM vs what VRAM contains
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

  std::cout << std::hex << std::setfill('0');

  // Run for 100 frames
  for (int f = 0; f < 100; f++) {
    for (int i = 0; i < 280896; i++) {
      gba.Step();
    }
  }

  // The IWRAM code decompresses from ROM at 0x08006110 to IWRAM at 0x03000000
  // This decompressed data should eventually be copied to VRAM

  std::cout << "=== IWRAM 0x03000000 (decompressor output) ===" << std::endl;
  for (int i = 0; i < 256; i += 16) {
    std::cout << "  [0x" << std::setw(4) << i << "]: ";
    for (int j = 0; j < 16; j++) {
      std::cout << std::setw(2) << (int)mem.Read8(0x03000000 + i + j) << " ";
    }
    std::cout << std::endl;
  }

  // Check what's in VRAM tile data area
  uint16_t bg0cnt = mem.Read16(0x04000008);
  uint32_t charBase = 0x06000000 + (((bg0cnt >> 2) & 3) * 0x4000);

  std::cout << "\n=== VRAM Tile Data (first 256 bytes at char base) ==="
            << std::endl;
  std::cout << "charBase = 0x" << std::setw(8) << charBase << std::endl;
  for (int i = 0; i < 256; i += 16) {
    std::cout << "  [0x" << std::setw(4) << i << "]: ";
    for (int j = 0; j < 16; j++) {
      std::cout << std::setw(2) << (int)mem.Read8(charBase + i + j) << " ";
    }
    std::cout << std::endl;
  }

  // Check if VRAM contains decompressed graphics data
  // The tile data should have patterns that look like NES-style 4bpp tiles

  // Look for DMA that copied data to VRAM
  std::cout << "\n=== VRAM analysis ===" << std::endl;

  // Count non-zero bytes in tile region
  int nonZeroTiles = 0;
  int totalBytes = 0;
  for (uint32_t addr = charBase; addr < charBase + 0x4000; addr++) {
    if (mem.Read8(addr) != 0)
      nonZeroTiles++;
    totalBytes++;
  }
  std::cout << "Non-zero bytes in tile data: " << std::dec << nonZeroTiles
            << " / " << totalBytes << std::endl;

  // Check if data looks like valid tiles or random garbage
  // Valid 4bpp tiles should have indices 0-15, so individual bytes should have
  // both nibbles in range 0-F (always true for 8-bit), but patterns should look
  // regular

  // Sample a few tiles and check their patterns
  std::cout << "\n=== Sample Tile Analysis ===" << std::endl;
  for (int t = 0; t < 10; t++) {
    uint32_t tileAddr = charBase + t * 32;
    int zeroCount = 0, threeCount = 0, otherCount = 0;
    for (int b = 0; b < 32; b++) {
      uint8_t byte = mem.Read8(tileAddr + b);
      uint8_t lo = byte & 0xF;
      uint8_t hi = (byte >> 4) & 0xF;
      if (lo == 0)
        zeroCount++;
      else if (lo == 3)
        threeCount++;
      else
        otherCount++;
      if (hi == 0)
        zeroCount++;
      else if (hi == 3)
        threeCount++;
      else
        otherCount++;
    }
    std::cout << "Tile " << std::dec << t << ": zeros=" << zeroCount
              << " threes=" << threeCount << " other=" << otherCount
              << std::endl;
  }

  return 0;
}
