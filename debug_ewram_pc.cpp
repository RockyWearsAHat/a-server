// Trace frame 7-9 to see what's happening with PC in EWRAM
#include "emulator/gba/GBA.h"
#include <cstdint>
#include <iomanip>
#include <iostream>

int main() {
  AIO::Emulator::GBA::GBA gba;
  gba.LoadROM("OG-DK.gba");

  const uint64_t CYCLES_PER_FRAME = 280896;
  uint64_t totalCycles = 0;

  // Run to frame 6
  while (totalCycles < 6 * CYCLES_PER_FRAME) {
    totalCycles += gba.Step();
  }

  std::cout << "=== After frame 6 ===" << std::endl;
  std::cout << std::hex;
  std::cout << "PC: 0x" << gba.GetPC() << std::endl;

  auto &mem = gba.GetMemory();

  // Read EWRAM at the address range
  std::cout << "\n=== EWRAM at 0x02F30600 (masked) ===" << std::endl;
  for (int i = 0; i < 64; i += 4) {
    uint32_t addr = 0x02F30600 + i;
    uint32_t val = mem.Read32(addr);
    std::cout << "0x" << std::setw(8) << std::setfill('0') << addr << ": 0x"
              << std::setw(8) << std::setfill('0') << val << std::endl;
  }

  // The address 0x02F306xx, masked with 0x3FFFF (EWRAM 256KB) = 0xF306xx
  // But EWRAM is only 256KB (0x40000 bytes), so 0xF306xx would be out of bounds
  // Actually 0x02F30600 & 0x3FFFF = 0x30600, which IS valid
  std::cout << "\n=== EWRAM at 0x02030600 (canonical) ===" << std::endl;
  for (int i = 0; i < 64; i += 4) {
    uint32_t addr = 0x02030600 + i;
    uint32_t val = mem.Read32(addr);
    std::cout << "0x" << std::setw(8) << std::setfill('0') << addr << ": 0x"
              << std::setw(8) << std::setfill('0') << val << std::endl;
  }

  // Let's step through a bit and watch PC
  std::cout << "\n=== Stepping through frames 6-10 ===" << std::endl;
  for (int step = 0; step < 500; step++) {
    uint32_t pc = gba.GetPC();
    totalCycles += gba.Step();
    uint32_t newPc = gba.GetPC();

    // Log unusual PC values
    if ((newPc >> 24) == 0x02 && (newPc & 0xFF0000) > 0x030000) {
      std::cout << "Unusual PC: 0x" << std::setw(8) << std::setfill('0')
                << newPc << " (from 0x" << pc << ")" << std::endl;
    }
  }

  return 0;
}
