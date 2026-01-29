// Trace early boot execution to find protection check
#include "emulator/gba/GBA.h"
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>

int main() {
  AIO::Emulator::GBA::GBA gba;
  gba.LoadROM("OG-DK.gba");

  // Log PCs to find where behavior diverges
  std::ofstream trace("ogdk_trace.log");

  uint64_t totalCycles = 0;
  int logCount = 0;

  // Track PC history for loop detection
  uint32_t pcHistory[10] = {0};
  int histIdx = 0;
  bool inLoop = false;

  while (totalCycles < 1000000 && logCount < 100000) {
    uint32_t pc = gba.GetPC();
    uint32_t cpsr = gba.GetCPSR();

    // Check for loop
    bool wasInHistory = false;
    for (int i = 0; i < 10; i++) {
      if (pcHistory[i] == pc)
        wasInHistory = true;
    }
    pcHistory[histIdx] = pc;
    histIdx = (histIdx + 1) % 10;

    // Only log new PCs or every 10000th cycle
    if (!wasInHistory || (logCount % 1000 == 0)) {
      trace << std::hex << "PC=0x" << std::setw(8) << std::setfill('0') << pc
            << " CPSR=0x" << cpsr << std::dec << " cycles=" << totalCycles
            << std::endl;
      logCount++;
    }

    // Log key memory addresses periodically
    if (totalCycles > 0 && (totalCycles % 100000) == 0) {
      auto &mem = gba.GetMemory();
      trace << "--- State at cycle " << totalCycles << " ---" << std::endl;
      trace << std::hex;
      trace << "DISPCNT: 0x" << mem.Read16(0x04000000) << std::endl;
      trace << "BG0CNT: 0x" << mem.Read16(0x04000008) << std::endl;
      trace << "VCOUNT: " << std::dec << mem.Read16(0x04000006) << std::endl;
      trace << "IE: 0x" << std::hex << mem.Read16(0x04000200) << std::endl;
      trace << "IF: 0x" << mem.Read16(0x04000202) << std::endl;
      trace << "IME: 0x" << mem.Read16(0x04000208) << std::endl;
      trace << std::dec << std::endl;
    }

    totalCycles += gba.Step();
  }

  trace.close();
  std::cout << "Trace saved to ogdk_trace.log" << std::endl;
  std::cout << "Final PC: 0x" << std::hex << gba.GetPC() << std::endl;

  // Print last 50 unique PCs
  std::cout << "\n=== Checking key protection areas ===" << std::endl;
  auto &mem = gba.GetMemory();

  std::cout << "Reading from unused ROM area (0x0E000000): ";
  for (int i = 0; i < 4; i++) {
    std::cout << std::hex << (int)mem.Read8(0x0E000000 + i) << " ";
  }
  std::cout << std::endl;

  std::cout << "Reading from BIOS (0x00000000): 0x" << std::hex
            << mem.Read32(0x00000000) << std::endl;

  std::cout << "Reading from after BIOS (0x00004000): 0x"
            << mem.Read32(0x00004000) << std::endl;

  return 0;
}
