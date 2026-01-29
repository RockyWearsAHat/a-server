// Analyze OG-DK DMA operations to understand why palette is zero
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

  // Run ~100 frames to let the game initialize
  constexpr int CYCLES_PER_FRAME = 280896;
  int totalCycles = 0;
  while (totalCycles < CYCLES_PER_FRAME * 100) {
    totalCycles += gba.Step();
  }

  std::cout << "=== Literal Pool Analysis ===" << std::endl;
  std::cout << std::hex << std::setfill('0');

  // The IWRAM code at 0x03007400 loads from literal pool
  // First instruction: LDR R12, [PC+0x40] => 0x03007400 + 8 + 0x40 = 0x03007448
  // Fifth instruction: LDR R7, [PC+0xF8]  => 0x03007410 + 8 + 0xF8 = 0x03007510

  std::cout << "\nKey literal pool entries:" << std::endl;
  std::cout << "  [0x03007448] = 0x" << std::setw(8) << mem.Read32(0x03007448)
            << std::endl;
  std::cout << "  [0x03007510] = 0x" << std::setw(8) << mem.Read32(0x03007510)
            << std::endl;

  // Dump full literal pool area
  std::cout << "\nFull literal pool (0x03007440-0x03007520):" << std::endl;
  for (uint32_t addr = 0x03007440; addr < 0x03007520; addr += 16) {
    std::cout << "  0x" << std::setw(8) << addr << ": ";
    for (int i = 0; i < 4; i++) {
      std::cout << std::setw(8) << mem.Read32(addr + i * 4) << " ";
    }
    std::cout << std::endl;
  }

  // Check palette buffer location
  std::cout << "\nPalette buffer (0x0300750C-0x0300760C):" << std::endl;
  for (uint32_t addr = 0x0300750C; addr < 0x0300760C; addr += 16) {
    std::cout << "  0x" << std::setw(8) << addr << ": ";
    for (int i = 0; i < 4; i++) {
      std::cout << std::setw(8) << mem.Read32(addr + i * 4) << " ";
    }
    std::cout << std::endl;
  }

  // Check actual palette RAM
  std::cout << "\nPalette RAM (0x05000000-0x05000040):" << std::endl;
  for (uint32_t addr = 0x05000000; addr < 0x05000040; addr += 16) {
    std::cout << "  0x" << std::setw(8) << addr << ": ";
    for (int i = 0; i < 4; i++) {
      std::cout << std::setw(8) << mem.Read32(addr + i * 4) << " ";
    }
    std::cout << std::endl;
  }

  return 0;
}
