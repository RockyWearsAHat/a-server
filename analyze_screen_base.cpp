// Deep analysis of OG-DK display state
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

  // CRITICAL: BG0CNT says screenBase=13 (0x6800)
  // But the game swaps between 0x6003200 and 0x6006800
  // 0x6003200 = screen block 6 (6 * 0x800 = 0x3000... wait that's 0x3000 not
  // 0x3200)

  // Let's recalculate:
  // Screen block 0 = 0x06000000
  // Screen block 1 = 0x06000800
  // Screen block 6 = 0x06003000
  // Screen block 12 = 0x06006000
  // Screen block 13 = 0x06006800

  // But game uses 0x06003200 and 0x06006800
  // 0x3200 is NOT a 2KB boundary! 0x3200 / 0x800 = 6.25
  // This is UNUSUAL!

  std::cout << "=== Screen Buffer Address Analysis ===" << std::endl;
  std::cout << "BG0CNT says screenBase block 13 = 0x06006800" << std::endl;
  std::cout << "But game also uses 0x06003200 (block 6.25!?)" << std::endl;

  // The REAL screen base from BG0CNT bits 8-12
  uint16_t BG0CNT = mem.Read16(0x04000008);
  int screenBase = (BG0CNT >> 8) & 0x1F;
  std::cout << "\nBG0CNT raw: 0x" << std::hex << BG0CNT << std::endl;
  std::cout << "Screen base field: " << std::dec << screenBase << std::endl;
  std::cout << "Actual address: 0x0600" << std::hex << (screenBase * 0x800)
            << std::endl;

  // Check the frame select bit in DISPCNT for bitmap modes
  uint16_t DISPCNT = mem.Read16(0x04000000);
  std::cout << "\nDISPCNT: 0x" << std::hex << DISPCNT << std::endl;
  std::cout << "Frame select bit: " << ((DISPCNT >> 4) & 1) << std::endl;

  // The issue: Classic NES games use a special double-buffer technique
  // They modify BG0CNT to switch screen bases between frames
  // Let me check if BG0CNT changes during VBlank

  std::cout << "\n=== Running more frames and checking BG0CNT changes ==="
            << std::endl;

  uint16_t lastBG0CNT = BG0CNT;
  int changes = 0;

  for (int i = 0; i < 10000 && changes < 5; i++) {
    gba.Step();
    uint16_t newBG0CNT = mem.Read16(0x04000008);
    if (newBG0CNT != lastBG0CNT) {
      std::cout << "BG0CNT changed: 0x" << std::hex << lastBG0CNT << " -> 0x"
                << newBG0CNT << std::endl;
      int oldBase = (lastBG0CNT >> 8) & 0x1F;
      int newBase = (newBG0CNT >> 8) & 0x1F;
      std::cout << "  Screen base: " << std::dec << oldBase << " -> " << newBase
                << std::endl;
      lastBG0CNT = newBG0CNT;
      changes++;
    }
  }

  return 0;
}
