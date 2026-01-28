// Check if VBlank interrupts are being generated and IRQs firing
#include "emulator/gba/GBA.h"
#include <cstdint>
#include <iomanip>
#include <iostream>

int main() {
  AIO::Emulator::GBA::GBA gba;
  gba.LoadROM("OG-DK.gba");

  const uint64_t CYCLES_PER_FRAME = 280896;
  uint64_t totalCycles = 0;

  // Run to frame 30 where game should be stable
  while (totalCycles < 30 * CYCLES_PER_FRAME) {
    totalCycles += gba.Step();
  }

  auto &mem = gba.GetMemory();

  std::cout << "=== After 30 frames ===" << std::endl;
  std::cout << std::hex;
  std::cout << "PC:   0x" << gba.GetPC() << std::endl;
  std::cout << "IE:   0x" << mem.Read16(0x04000200) << std::endl;
  std::cout << "IF:   0x" << mem.Read16(0x04000202) << std::endl;
  std::cout << "IME:  0x" << mem.Read16(0x04000208) << std::endl;
  std::cout << "CPSR: 0x" << gba.GetCPSR() << std::endl;

  // Check DISPSTAT for VBlank IRQ enable
  uint16_t dispstat = mem.Read16(0x04000004);
  std::cout << "\nDISPSTAT: 0x" << dispstat << std::endl;
  std::cout << "  VBlank Flag (bit 0): " << (dispstat & 1) << std::endl;
  std::cout << "  HBlank Flag (bit 1): " << ((dispstat >> 1) & 1) << std::endl;
  std::cout << "  VCount Match (bit 2): " << ((dispstat >> 2) & 1) << std::endl;
  std::cout << "  VBlank IRQ Enable (bit 3): " << ((dispstat >> 3) & 1)
            << std::endl;
  std::cout << "  HBlank IRQ Enable (bit 4): " << ((dispstat >> 4) & 1)
            << std::endl;
  std::cout << "  VCount IRQ Enable (bit 5): " << ((dispstat >> 5) & 1)
            << std::endl;

  std::cout << "\nVCOUNT: " << std::dec << mem.Read16(0x04000006) << std::endl;

  // Check IRQ handler
  std::cout << "\nIRQ Handler at 0x03007FFC: 0x" << std::hex
            << mem.Read32(0x03007FFC) << std::endl;
  std::cout << "BIOS_IF at 0x03007FF8: 0x" << mem.Read32(0x03007FF8)
            << std::endl;

  // Now step a few more times and see if IE/IF change
  std::cout << "\n=== Stepping 10000 more cycles ===" << std::endl;
  uint16_t lastIF = mem.Read16(0x04000202);
  int ifChanges = 0;

  for (int i = 0; i < 10000; i++) {
    totalCycles += gba.Step();
    uint16_t newIF = mem.Read16(0x04000202);
    if (newIF != lastIF && ifChanges < 10) {
      std::cout << "IF changed: 0x" << std::hex << lastIF << " -> 0x" << newIF
                << " at PC=0x" << gba.GetPC() << std::endl;
      lastIF = newIF;
      ifChanges++;
    }
  }

  std::cout << "\nFinal IF: 0x" << std::hex << mem.Read16(0x04000202)
            << std::endl;
  std::cout << "Final PC: 0x" << gba.GetPC() << std::dec << std::endl;

  return 0;
}
