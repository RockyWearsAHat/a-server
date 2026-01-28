// Visualize VRAM layout conflict
#include <cstdint>
#include <iomanip>
#include <iostream>

int main() {
  std::cout << "=== GBA VRAM Layout (96KB: 0x06000000-0x06017FFF) ==="
            << std::endl;
  std::cout << std::endl;

  // What OG-DK uses:
  // CharBase = block 1 = 0x06004000
  // ScreenBase = block 13 = 0x06006800
  // Screen size = 2 (256x512) = 32x64 tiles = 4KB tilemap

  std::cout << "CharBase (block 1): 0x06004000" << std::endl;
  std::cout << "  - Tiles are 32 bytes each (4bpp)" << std::endl;
  std::cout << "  - 10-bit tile index means tiles 0-1023" << std::endl;
  std::cout << "  - Tile data spans: 0x06004000 to 0x0600C000 (32KB)"
            << std::endl;
  std::cout << std::endl;

  std::cout << "ScreenBase (block 13): 0x06006800" << std::endl;
  std::cout << "  - 256x512 tilemap = 32x64 tiles = 4096 bytes" << std::endl;
  std::cout << "  - Tilemap spans: 0x06006800 to 0x06007800" << std::endl;
  std::cout << std::endl;

  std::cout << "=== CONFLICT ANALYSIS ===" << std::endl;
  std::cout << std::endl;

  // Tile 247 = 0x06004000 + 247*32 = 0x06005EE0
  // Tile 256 = 0x06004000 + 256*32 = 0x06006000
  // Tile 320 = 0x06004000 + 320*32 = 0x06006800 <-- SAME as tilemap!
  // Tile 510 = 0x06004000 + 510*32 = 0x06007FC0

  std::cout << "Critical tile addresses:" << std::endl;
  std::cout << "  Tile 247:  0x" << std::hex << (0x06004000 + 247 * 32)
            << " (valid, before tilemap)" << std::endl;
  std::cout << "  Tile 256:  0x" << std::hex << (0x06004000 + 256 * 32)
            << std::endl;
  std::cout << "  Tile 319:  0x" << std::hex << (0x06004000 + 319 * 32)
            << " (last tile before tilemap)" << std::endl;
  std::cout << "  Tile 320:  0x" << std::hex << (0x06004000 + 320 * 32)
            << " <-- STARTS OVERLAPPING TILEMAP!" << std::endl;
  std::cout << "  Tile 436:  0x" << std::hex << (0x06004000 + 436 * 32)
            << " (IN tilemap region!)" << std::endl;
  std::cout << "  Tile 510:  0x" << std::hex << (0x06004000 + 510 * 32)
            << " (IN tilemap region!)" << std::endl;
  std::cout << std::endl;

  std::cout << "Tilemap region: 0x06006800 - 0x06007800" << std::endl;
  std::cout << "Tiles 320-383 map to:  0x06006800 - 0x06006FE0 (64 tiles, 2KB)"
            << std::endl;
  std::cout << "Tiles 384-447 map to:  0x06007000 - 0x06007FE0 (64 tiles, 2KB)"
            << std::endl;
  std::cout << std::endl;

  std::cout << "=== THE BUG ===" << std::endl;
  std::cout << "When the tilemap references tile 436 or 510, it reads from the "
               "TILEMAP ITSELF!"
            << std::endl;
  std::cout << "This causes the garbled display - tiles are reading other "
               "tilemap entries as pixel data!"
            << std::endl;
  std::cout << std::endl;

  std::cout << "=== EXPECTED BEHAVIOR ===" << std::endl;
  std::cout << "Classic NES Series games should use tile indices 0-255 only "
               "(NES has 256 tiles max)"
            << std::endl;
  std::cout << "The tilemap entries showing tiles 247, 436, 510 suggest the "
               "NES emulator is not"
            << std::endl;
  std::cout << "rendering proper tile indices, or there's a mapping issue."
            << std::endl;

  return 0;
}
