// Trace OG-DK execution immediately after IWRAM code starts
#include <emulator/gba/ARM7TDMI.h>
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

  bool inIWRAM = false;
  int iwramInstrCount = 0;
  uint64_t totalCycles = 0;
  bool dumped = false;

  std::cout << std::hex << std::setfill('0');

  // Run until we see IWRAM execution, then trace carefully
  while (totalCycles < 50000000) {
    uint32_t pc = gba.GetPC();

    // Check if entering IWRAM code region
    if (pc >= 0x03007400 && pc < 0x03007600 && !inIWRAM) {
      inIWRAM = true;
      std::cout << "\n=== Entering IWRAM code at cycle " << std::dec
                << totalCycles << " ===" << std::endl;
      std::cout << std::hex;

      // Dump the first 64 bytes of decompressed code
      std::cout << "IWRAM code (0x03007400):" << std::endl;
      for (uint32_t addr = 0x03007400; addr < 0x03007440; addr += 16) {
        std::cout << "  0x" << std::setw(8) << addr << ": ";
        for (int i = 0; i < 4; i++) {
          std::cout << std::setw(8) << mem.Read32(addr + i * 4) << " ";
        }
        std::cout << std::endl;
      }
    }

    // Trace IWRAM execution
    if (inIWRAM && pc >= 0x03007400 && pc < 0x03007600) {
      iwramInstrCount++;
      if (iwramInstrCount <= 30) {
        uint32_t op = mem.Read32(pc);
        std::cout << "  [" << std::dec << std::setw(3) << iwramInstrCount
                  << "] ";
        std::cout << std::hex << "PC=0x" << std::setw(8) << pc;
        std::cout << " OP=0x" << std::setw(8) << op;

        // Show relevant registers - R6 is the source data pointer!
        std::cout << " R6=0x" << std::setw(8) << gba.GetRegister(6);
        std::cout << " R7=0x" << std::setw(8) << gba.GetRegister(7);
        std::cout << " R8=0x" << std::setw(8) << gba.GetRegister(8);
        std::cout << std::endl;
      }
    }

    // Check if IWRAM code gets overwritten
    if (inIWRAM && !dumped) {
      uint32_t firstWord = mem.Read32(0x03007400);
      if (firstWord == 0xDADADADA) {
        dumped = true;
        std::cout << "\n=== IWRAM code overwritten at cycle " << std::dec
                  << totalCycles << " ===" << std::endl;
        std::cout << std::hex;
        std::cout << "PC when overwritten: 0x" << std::setw(8) << pc
                  << std::endl;
        std::cout << "IWRAM instructions executed: " << std::dec
                  << iwramInstrCount << std::endl;

        // Check what happened
        std::cout << "\nDMA3 registers:" << std::endl;
        std::cout << "  DMA3SAD = 0x" << std::hex << std::setw(8)
                  << mem.Read32(0x040000D4) << std::endl;
        std::cout << "  DMA3DAD = 0x" << std::setw(8) << mem.Read32(0x040000D8)
                  << std::endl;
        std::cout << "  DMA3CNT = 0x" << std::setw(8) << mem.Read32(0x040000DC)
                  << std::endl;
        break;
      }
    }

    totalCycles += gba.Step();
  }

  return 0;
}
