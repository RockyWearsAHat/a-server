// Trace IE/IF/IME changes during boot
#include "emulator/gba/GBA.h"
#include <cstdint>
#include <iomanip>
#include <iostream>

int main() {
  AIO::Emulator::GBA::GBA gba;
  gba.LoadROM("OG-DK.gba");

  const uint64_t CYCLES_PER_FRAME = 280896;
  uint64_t totalCycles = 0;

  uint16_t lastIE = 0, lastIME = 0;

  // Run first 20 frames and log any IE/IME changes
  while (totalCycles < 20 * CYCLES_PER_FRAME) {
    totalCycles += gba.Step();

    auto &mem = gba.GetMemory();
    uint16_t ie = mem.Read16(0x04000200);
    uint16_t ime = mem.Read16(0x04000208);

    if (ie != lastIE || ime != lastIME) {
      std::cout << "Cycle " << totalCycles << " PC=0x" << std::hex
                << gba.GetPC() << " IE: 0x" << std::setw(4) << std::setfill('0')
                << lastIE << " -> 0x" << std::setw(4) << ie << "  IME: 0x"
                << lastIME << " -> 0x" << ime << std::dec << std::endl;
      lastIE = ie;
      lastIME = ime;
    }
  }

  std::cout << "\nFinal state:" << std::endl;
  std::cout << "IE:  0x" << std::hex << lastIE << std::endl;
  std::cout << "IME: 0x" << lastIME << std::endl;
  std::cout << "PC:  0x" << gba.GetPC() << std::dec << std::endl;

  return 0;
}
