// Trace BG0CNT changes during OG-DK execution
#include "include/emulator/gba/GBA.h"
#include <fstream>
#include <iostream>
#include <map>

int main() {
  AIO::Emulator::GBA::GBA gba;
  if (!gba.LoadROM("OG-DK.gba")) {
    std::cerr << "Failed to load OG-DK.gba" << std::endl;
    return 1;
  }

  std::map<uint16_t, int> bg0cntCounts;
  uint16_t lastBg0cnt = 0;

  // Run 60 frames (280896 cycles per frame)
  for (int f = 0; f < 60; f++) {
    for (int i = 0; i < 280896;) {
      i += gba.Step();

      // Check BG0CNT value
      uint16_t bg0cnt = gba.ReadMem16(0x04000008);
      if (bg0cnt != lastBg0cnt) {
        std::cout << "Frame " << f << ": BG0CNT changed from 0x" << std::hex
                  << lastBg0cnt << " to 0x" << bg0cnt << std::dec << std::endl;
        lastBg0cnt = bg0cnt;
      }
      bg0cntCounts[bg0cnt]++;
    }
  }

  std::cout << "\n=== BG0CNT Value Counts ===" << std::endl;
  for (const auto &[val, cnt] : bg0cntCounts) {
    int charBase = (val >> 2) & 0x3;
    int screenBase = (val >> 8) & 0x1F;
    int screenSize = (val >> 14) & 0x3;
    std::cout << "0x" << std::hex << val << ": " << std::dec << cnt << " times"
              << " (charBase=" << charBase << " screenBase=" << screenBase
              << " size=" << screenSize << ")" << std::endl;
  }

  return 0;
}
