// Check BG scroll registers at runtime
#include "include/emulator/gba/GBA.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

int main() {
  AIO::Emulator::GBA::GBA gba;
  if (!gba.LoadROM("OG-DK.gba")) {
    std::cerr << "Failed to load OG-DK.gba" << std::endl;
    return 1;
  }

  // Run 60 frames (280896 cycles per frame)
  for (int f = 0; f < 60; f++) {
    for (int i = 0; i < 280896;) {
      i += gba.Step();
    }
  }

  // Read BG0 scroll registers using memory interface
  // BG0HOFS = 0x04000010 (2 bytes)
  // BG0VOFS = 0x04000012 (2 bytes)
  uint16_t bg0hofs = gba.ReadMem16(0x04000010);
  uint16_t bg0vofs = gba.ReadMem16(0x04000012);

  // BG0CNT is at 0x04000008
  uint16_t bg0cnt = gba.ReadMem16(0x04000008);

  std::cout << "\n=== BG0 Configuration ===" << std::endl;
  std::cout << "BG0CNT: 0x" << std::hex << bg0cnt << std::endl;
  std::cout << "  CharBase: " << std::dec << ((bg0cnt >> 2) & 0x3) << std::endl;
  std::cout << "  ScreenBase: " << ((bg0cnt >> 8) & 0x1F) << std::endl;
  std::cout << "  ScreenSize: " << ((bg0cnt >> 14) & 0x3) << std::endl;
  std::cout << "\n=== BG0 Scroll ===" << std::endl;
  std::cout << "BG0HOFS: " << std::dec << bg0hofs << " (0x" << std::hex
            << bg0hofs << ")" << std::endl;
  std::cout << "BG0VOFS: " << std::dec << bg0vofs << " (0x" << std::hex
            << bg0vofs << ")" << std::endl;

  // Check if any vertical scrolling brings the overlap tiles into view
  // Screen is 160 pixels tall, rows 24-63 start at y=192
  int firstVisibleRow = bg0vofs / 8;
  int lastVisibleRow = (bg0vofs + 159) / 8;
  std::cout << "\n=== Visible Rows ===" << std::endl;
  std::cout << "First visible row: " << std::dec << firstVisibleRow
            << std::endl;
  std::cout << "Last visible row: " << std::dec << lastVisibleRow << std::endl;
  std::cout << "Overlap tiles (>=320) are mostly in rows 24+ (y>=192)"
            << std::endl;

  if (lastVisibleRow >= 24) {
    std::cout << "WARNING: Some overlap rows ARE visible!" << std::endl;
  } else {
    std::cout << "Overlap rows are offscreen (BG0VOFS too low)" << std::endl;
  }

  // Now scan the actual tilemap for overlap tiles in visible region
  uint32_t screenAddr = (((bg0cnt >> 8) & 0x1F) * 0x800);
  int charBase = (bg0cnt >> 2) & 0x3;

  std::cout << "\n=== Checking visible area for overlap tiles ===" << std::endl;
  std::cout << "screenAddr=0x" << std::hex << screenAddr
            << " charBase=" << std::dec << charBase << std::endl;

  int overlapInVisible = 0;
  // Check visible rows
  for (int row = firstVisibleRow; row <= lastVisibleRow && row < 64; row++) {
    for (int col = 0; col < 32; col++) {
      int actualRow = row % 64; // Handle wrap for 512px height
      uint32_t mapOffset = 0x06000000 + screenAddr + (actualRow / 32) * 0x800 +
                           (actualRow % 32) * 64 + col * 2;
      uint16_t entry = gba.ReadMem16(mapOffset);
      int tileIdx = entry & 0x3FF;

      // Tile 320 overlaps with screenBase=13 tilemap
      if (charBase == 1 && tileIdx >= 320) {
        if (overlapInVisible < 20) {
          std::cout << "  Row " << std::dec << row << " Col " << col
                    << ": tile " << tileIdx << " OVERLAPS!" << std::endl;
        }
        overlapInVisible++;
      }
    }
  }

  std::cout << "Total overlap tiles in visible region: " << std::dec
            << overlapInVisible << std::endl;

  return 0;
}
