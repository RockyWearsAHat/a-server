// Trace where high tile indices (320+) come from
// and understand if this is a valid NES-to-GBA tile mapping

#include "emulator/gba/Bus.h"
#include "emulator/gba/CPU.h"
#include "emulator/gba/PPU.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>

using namespace AIO::Emulator::GBA;

int main() {
  Bus bus;
  CPU cpu(bus);
  PPU &ppu = bus.getPPU();

  // Load ROM
  std::ifstream romFile("OG-DK.gba", std::ios::binary);
  if (!romFile) {
    std::cerr << "Failed to open OG-DK.gba" << std::endl;
    return 1;
  }
  std::vector<uint8_t> rom(std::istreambuf_iterator<char>(romFile), {});
  bus.loadROM(rom);

  // Run until we have stable display
  for (int frame = 0; frame < 120; frame++) {
    for (int scanline = 0; scanline < 228; scanline++) {
      for (int dot = 0; dot < 308; dot++) {
        cpu.step();
      }
    }
  }

  std::cout << "=== OG-DK Tile Analysis after 120 frames ===" << std::endl;

  // Get VRAM and examine the setup
  const uint8_t *vram = bus.getVRAM();

  // Read registers
  uint16_t dispcnt = bus.read16(0x04000000);
  uint16_t bg0cnt = bus.read16(0x04000008);

  // Parse BG0CNT
  int priority = bg0cnt & 0x3;
  int charBase = (bg0cnt >> 2) & 0x3;
  int screenBase = (bg0cnt >> 8) & 0x1F;
  int colorMode = (bg0cnt >> 7) & 0x1;
  int screenSize = (bg0cnt >> 14) & 0x3;

  std::cout << "\nBG0CNT Analysis:" << std::endl;
  std::cout << "  charBase=" << charBase << " (tile data at 0x" << std::hex
            << (0x06000000 + charBase * 0x4000) << ")" << std::dec << std::endl;
  std::cout << "  screenBase=" << screenBase << " (tilemap at 0x" << std::hex
            << (0x06000000 + screenBase * 0x800) << ")" << std::dec
            << std::endl;
  std::cout << "  colorMode=" << colorMode << " ("
            << (colorMode ? "8bpp/256" : "4bpp/16") << " colors)" << std::endl;
  std::cout << "  screenSize=" << screenSize << std::endl;

  uint32_t charAddr = charBase * 0x4000;    // Relative to VRAM
  uint32_t screenAddr = screenBase * 0x800; // Relative to VRAM

  // Calculate where tiles 320+ would be
  int bytesPerTile = colorMode ? 64 : 32; // 8bpp=64, 4bpp=32
  uint32_t tile320Addr = charAddr + 320 * bytesPerTile;

  std::cout << "\nMemory Layout:" << std::endl;
  std::cout << "  CharBase (tiles) starts at: 0x" << std::hex << charAddr
            << std::dec << std::endl;
  std::cout << "  ScreenBase (tilemap) starts at: 0x" << std::hex << screenAddr
            << std::dec << std::endl;
  std::cout << "  Tile 320 would be at: 0x" << std::hex << tile320Addr
            << std::dec << std::endl;

  // Check if tile 320+ overlaps with tilemap
  if (tile320Addr >= screenAddr && tile320Addr < screenAddr + 0x1000) {
    std::cout << "\n*** VRAM OVERLAP DETECTED! ***" << std::endl;
    std::cout << "  Tile 320+ (0x" << std::hex << tile320Addr
              << ") overlaps tilemap (0x" << screenAddr << ")!" << std::dec
              << std::endl;

    // Calculate which tile index starts the overlap
    int overlapStart = (screenAddr - charAddr) / bytesPerTile;
    std::cout << "  Overlap starts at tile index: " << overlapStart
              << std::endl;
  }

  // Analyze tilemap entries
  std::cout << "\n=== Tilemap Analysis ===" << std::endl;

  std::map<int, int> tileUsage; // tile -> count
  std::map<int, int> palUsage;  // palette -> count
  int highTiles = 0;
  int lowTiles = 0;

  // BG0 tilemap (32x32 or larger based on screenSize)
  int mapWidth = 32;
  int mapHeight = (screenSize >= 2) ? 64 : 32;

  for (int ty = 0; ty < mapHeight; ty++) {
    for (int tx = 0; tx < mapWidth; tx++) {
      uint32_t mapOffset = screenAddr + (ty * mapWidth + tx) * 2;
      if (mapOffset + 1 < 0x18000) { // Within VRAM
        uint16_t entry = vram[mapOffset] | (vram[mapOffset + 1] << 8);
        int tile = entry & 0x3FF;
        int pal = (entry >> 12) & 0xF;

        tileUsage[tile]++;
        palUsage[pal]++;

        if (tile >= 320)
          highTiles++;
        else
          lowTiles++;
      }
    }
  }

  std::cout << "  Total entries analyzed: " << (lowTiles + highTiles)
            << std::endl;
  std::cout << "  Tiles 0-319: " << lowTiles << " ("
            << (lowTiles * 100 / (lowTiles + highTiles)) << "%)" << std::endl;
  std::cout << "  Tiles 320+: " << highTiles << " ("
            << (highTiles * 100 / (lowTiles + highTiles)) << "%)" << std::endl;

  // Show most used tiles >= 320
  std::cout << "\nMost used tiles >= 320:" << std::endl;
  std::vector<std::pair<int, int>> highTileList;
  for (auto &p : tileUsage) {
    if (p.first >= 320) {
      highTileList.push_back(p);
    }
  }
  std::sort(highTileList.begin(), highTileList.end(),
            [](auto &a, auto &b) { return a.second > b.second; });

  for (int i = 0; i < std::min(20, (int)highTileList.size()); i++) {
    int tile = highTileList[i].first;
    int count = highTileList[i].second;
    uint32_t tileVramAddr = charAddr + tile * bytesPerTile;
    std::cout << "  tile " << tile << " (" << count << " uses) at 0x"
              << std::hex << tileVramAddr << std::dec;
    if (tileVramAddr >= screenAddr && tileVramAddr < screenAddr + 0x2000) {
      std::cout << " [IN TILEMAP!]";
    }
    std::cout << std::endl;
  }

  // Palette usage
  std::cout << "\nPalette bank usage:" << std::endl;
  for (auto &p : palUsage) {
    std::cout << "  pal " << p.first << ": " << p.second << " tiles"
              << std::endl;
  }

  // Check what's in the NES emulator's output area
  // The NES pattern tables are 0x0000-0x1FFF in NES, and typically get
  // converted to GBA tiles
  std::cout << "\n=== Checking NES-related data ===" << std::endl;

  // Look for patterns that might indicate NES tile data
  // NES uses 8x8 tiles with 2bpp per plane (16 bytes per tile)
  // GBA uses 8x8 tiles with 4bpp (32 bytes per tile)

  // Check if there's an obvious conversion happening
  // The charBase area should have GBA-format tiles derived from NES tiles

  std::cout << "\nFirst few tiles at charBase:" << std::endl;
  for (int t = 0; t < 4; t++) {
    uint32_t taddr = charAddr + t * bytesPerTile;
    std::cout << "  Tile " << t << " at 0x" << std::hex << taddr << ": ";
    for (int b = 0; b < 16; b++) {
      printf("%02X ", vram[taddr + b]);
    }
    std::cout << "..." << std::dec << std::endl;
  }

  // Check what's at the overlap area
  std::cout << "\nData at tilemap/overlap area:" << std::endl;
  for (int t = 320; t < 324; t++) {
    uint32_t taddr = charAddr + t * bytesPerTile;
    if (taddr + 32 <= 0x18000) {
      std::cout << "  'Tile' " << t << " at 0x" << std::hex << taddr << ": ";
      for (int b = 0; b < 16; b++) {
        printf("%02X ", vram[taddr + b]);
      }
      std::cout << "..." << std::dec << std::endl;

      // Interpret as tilemap entries
      std::cout << "    (as tilemap entries: ";
      for (int e = 0; e < 8; e++) {
        uint16_t entry = vram[taddr + e * 2] | (vram[taddr + e * 2 + 1] << 8);
        std::cout << std::hex << entry << " ";
      }
      std::cout << ")" << std::dec << std::endl;
    }
  }

  // Check if GBATEK mentions anything about the valid tile range
  std::cout << "\n=== VRAM Layout Validation ===" << std::endl;
  std::cout << "For Mode 0, Text BG:" << std::endl;
  std::cout << "  - Max 1024 tiles (indices 0-1023)" << std::endl;
  std::cout
      << "  - CharBase in 16KB blocks (0,1,2,3 = 0x0000,0x4000,0x8000,0xC000)"
      << std::endl;
  std::cout << "  - ScreenBase in 2KB blocks (0-31)" << std::endl;
  std::cout << "\nThis game: charBase=1, screenBase=13" << std::endl;
  std::cout << "  Tiles at: 0x4000-0xBFFF (charBase 1 spans 32KB of tile space)"
            << std::endl;
  std::cout << "  Tilemap at: 0x6800-0x7800 (2KB for 32x64 map)" << std::endl;
  std::cout << "\nWith 4bpp (32 bytes/tile):" << std::endl;
  std::cout << "  Tile 0 = 0x4000" << std::endl;
  std::cout << "  Tile 320 = 0x4000 + 320*32 = 0x" << std::hex
            << (0x4000 + 320 * 32) << std::dec << " = 0x6800" << std::endl;
  std::cout << "  EXACTLY at screenBase! This is the overlap!" << std::endl;

  // What's the intended solution?
  std::cout << "\n=== Possible Causes ===" << std::endl;
  std::cout << "1. The game expects a different VRAM layout interpretation"
            << std::endl;
  std::cout << "2. The tile indices are being calculated wrong somewhere"
            << std::endl;
  std::cout << "3. There's a masking issue (tile indices should wrap at 512?)"
            << std::endl;
  std::cout << "4. The charBase should be interpreted differently" << std::endl;

  return 0;
}
