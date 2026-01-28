// Check for protection/prefetch pipeline behavior
// Classic NES Series games are known to check CPU pipeline behavior
#include "emulator/gba/GBA.h"
#include "emulator/gba/PPU.h"
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>

int main() {
  AIO::Emulator::GBA::GBA gba;
  gba.LoadROM("OG-DK.gba");

  auto &mem = gba.GetMemory();

  // Classic NES games do protection checks early in boot
  // Let's trace the first few thousand instructions to look for patterns

  std::cout << "=== Tracing early boot instructions ===" << std::endl;
  std::cout << std::hex << std::setfill('0');

  int instrCount = 0;
  uint32_t lastPC = 0;

  // Look for suspicious patterns:
  // 1. Reads from open bus / undefined areas
  // 2. Specific SWI patterns
  // 3. Protection check sequences

  while (instrCount < 50000) {
    uint32_t pc = gba.GetPC();

    // Step one instruction
    gba.Step();
    instrCount++;

    // Look for interesting patterns at specific early boot stages
    if (instrCount <= 100 || instrCount % 1000 == 0) {
      // Get some key registers
      uint32_t r0 = gba.GetRegister(0);
      uint32_t r1 = gba.GetRegister(1);
      uint32_t r15 = gba.GetPC();

      std::cout << "Instr " << std::dec << std::setw(5) << instrCount
                << " PC=0x" << std::hex << std::setw(8) << pc << " R0=0x"
                << std::setw(8) << r0 << " R1=0x" << std::setw(8) << r1
                << std::endl;
    }

    lastPC = pc;
  }

  // Check what's in IWRAM (where the NES emulator code is decompressed)
  std::cout << "\n=== IWRAM content near 0x03007400 (decompressed code) ==="
            << std::endl;
  for (int i = 0; i < 64; i++) {
    uint8_t byte = mem.Read8(0x03007400 + i);
    if (i % 16 == 0)
      std::cout << std::hex << std::setw(8) << (0x03007400 + i) << ": ";
    std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)byte
              << " ";
    if ((i + 1) % 16 == 0)
      std::cout << std::endl;
  }

  // Classic NES games check for SRAM presence - they refuse to run if SRAM
  // exists Our emulator returns 0xFF for SRAM reads (EEPROM behavior) which
  // should be correct

  // Check what's at the SRAM region
  std::cout << "\n=== SRAM region reads (should be 0xFF for EEPROM games) ==="
            << std::endl;
  for (int i = 0; i < 16; i++) {
    uint8_t sramByte = mem.Read8(0x0E000000 + i);
    std::cout << std::hex << std::setw(2) << (int)sramByte << " ";
  }
  std::cout << std::endl;

  return 0;
}
