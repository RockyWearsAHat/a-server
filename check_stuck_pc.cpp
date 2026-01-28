// Analyze where the game gets stuck
#include "emulator/gba/GBA.h"
#include <cstdint>
#include <iomanip>
#include <iostream>

int main() {
  AIO::Emulator::GBA::GBA gba;
  gba.LoadROM("OG-DK.gba");

  const uint64_t CYCLES_PER_FRAME = 280896;
  uint64_t totalCycles = 0;

  // Run 60 frames to get to steady state
  while (totalCycles < 60 * CYCLES_PER_FRAME) {
    totalCycles += gba.Step();
  }

  std::cout << "=== After 60 frames ===" << std::endl;
  std::cout << "PC: 0x" << std::hex << gba.GetPC() << std::endl;
  std::cout << "CPSR: 0x" << gba.GetCPSR() << std::endl;

  // Dump IWRAM around PC 0x3005504
  auto &mem = gba.GetMemory();
  std::cout << "\n=== IWRAM at 0x03005500 ===" << std::endl;
  for (int i = 0; i < 32; i += 4) {
    uint32_t addr = 0x03005500 + i;
    uint32_t val = mem.Read32(addr);
    std::cout << "0x" << std::hex << addr << ": 0x" << std::setw(8)
              << std::setfill('0') << val << std::endl;
  }

  // Check if there's any wait loop detection
  std::cout << "\n=== Analyzing instruction at stuck PC ===" << std::endl;
  uint32_t pc = gba.GetPC();
  for (int i = -8; i <= 8; i += 4) {
    uint32_t addr = pc + i;
    uint32_t instr = mem.Read32(addr);
    std::cout << "0x" << std::hex << addr << ": 0x" << std::setw(8)
              << std::setfill('0') << instr << std::endl;
  }

  // Check what's at SRAM region (0x0E000000)
  std::cout << "\n=== SRAM region reads (0x0E000000) ===" << std::endl;
  for (int i = 0; i < 16; i++) {
    uint8_t val = mem.Read8(0x0E000000 + i);
    std::cout << std::hex << (int)val << " ";
  }
  std::cout << std::endl;

  return 0;
}
