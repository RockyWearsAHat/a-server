// Trace OG-DK decompressed code execution
#include "emulator/gba/GBA.h"
#include <iomanip>
#include <iostream>

using namespace AIO::Emulator::GBA;

int main() {
  GBA gba;
  if (!gba.LoadROM("/Users/alexwaldmann/Desktop/AIO Server/OG-DK.gba")) {
    std::cerr << "Failed to load ROM" << std::endl;
    return 1;
  }

  auto &mem = gba.GetMemory();

  // Run 1 frame to get past initial setup
  constexpr int CYCLES_PER_FRAME = 280896;
  int cycles = 0;
  while (cycles < CYCLES_PER_FRAME) {
    cycles += gba.Step();
  }

  // Dump decompressed code at 0x03007400
  std::cout << "=== Decompressed code at 0x03007400 ===" << std::endl;
  for (int i = 0; i < 70; i++) {
    uint32_t addr = 0x03007400 + i * 4;
    uint32_t instr = mem.Read32(addr);
    std::cout << std::hex << "0x" << std::setw(8) << std::setfill('0') << addr
              << ": 0x" << std::setw(8) << instr << std::dec << std::endl;
  }

  // Look for literal pool values
  std::cout << "\n=== Literal pool values ===" << std::endl;
  for (int i = 0; i < 276 / 4; i++) {
    uint32_t addr = 0x03007400 + i * 4;
    uint32_t val = mem.Read32(addr);
    // Look for addresses in IWRAM range
    if ((val & 0xFF000000) == 0x03000000) {
      std::cout << "At 0x" << std::hex << addr << ": IWRAM addr 0x" << val
                << std::dec << std::endl;
    }
    // Look for addresses in ROM range
    if ((val & 0x0F000000) == 0x08000000) {
      std::cout << "At 0x" << std::hex << addr << ": ROM addr 0x" << val
                << std::dec << std::endl;
    }
  }

  return 0;
}
