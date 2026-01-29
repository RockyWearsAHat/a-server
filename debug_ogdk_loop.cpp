// Debug the game loop to understand what it's waiting for
#include "emulator/gba/GBA.h"
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>

int main() {
  AIO::Emulator::GBA::GBA gba;
  gba.LoadROM("OG-DK.gba");

  // Run until game is stable
  uint64_t totalCycles = 0;
  while (totalCycles < 1000000) {
    totalCycles += gba.Step();
  }

  auto &mem = gba.GetMemory();

  std::cout << "=== Interrupt State ===" << std::endl;
  std::cout << std::hex;
  std::cout << "IE:   0x" << std::setw(4) << std::setfill('0')
            << mem.Read16(0x04000200) << std::endl;
  std::cout << "IF:   0x" << std::setw(4) << std::setfill('0')
            << mem.Read16(0x04000202) << std::endl;
  std::cout << "IME:  0x" << std::setw(4) << std::setfill('0')
            << mem.Read16(0x04000208) << std::endl;

  // Check VCOUNT
  std::cout << "VCOUNT: " << std::dec << mem.Read16(0x04000006) << std::endl;

  // Check DISPSTAT
  std::cout << "DISPSTAT: 0x" << std::hex << mem.Read16(0x04000004)
            << std::endl;

  // Check timer registers
  std::cout << "\n=== Timers ===" << std::endl;
  std::cout << "TM0CNT: 0x" << mem.Read32(0x04000100) << std::endl;
  std::cout << "TM1CNT: 0x" << mem.Read32(0x04000104) << std::endl;
  std::cout << "TM2CNT: 0x" << mem.Read32(0x04000108) << std::endl;
  std::cout << "TM3CNT: 0x" << mem.Read32(0x0400010C) << std::endl;

  // Check IRQ handler address
  std::cout << "\n=== IRQ Setup ===" << std::endl;
  std::cout << "IRQ Vector (0x03007FFC): 0x" << mem.Read32(0x03007FFC)
            << std::endl;
  std::cout << "BIOS_IF    (0x03007FF8): 0x" << mem.Read32(0x03007FF8)
            << std::endl;

  // Decode PC and the instructions around it
  uint32_t pc = gba.GetPC();
  std::cout << "\n=== Code at stuck PC 0x" << pc << " ===" << std::endl;
  for (int i = -16; i <= 16; i += 4) {
    uint32_t addr = pc + i;
    uint32_t instr = mem.Read32(addr);
    std::cout << "0x" << std::setw(8) << std::setfill('0') << addr << ": 0x"
              << std::setw(8) << std::setfill('0') << instr;
    if (addr == pc)
      std::cout << " <-- PC";
    std::cout << std::endl;
  }

  // Get CPU registers
  std::cout << "\n=== CPU Registers ===" << std::endl;
  for (int i = 0; i < 16; i++) {
    std::cout << "R" << std::dec << i << ": 0x" << std::hex
              << gba.GetRegister(i) << std::endl;
  }
  std::cout << "CPSR: 0x" << gba.GetCPSR() << std::endl;

  return 0;
}
