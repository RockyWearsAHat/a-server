// Trace R0 and R12 values across IRQs to see if they change
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

  std::cout << "=== Starting at frame 30 ===" << std::endl;
  std::cout << std::hex;
  std::cout << "PC:  0x" << gba.GetPC() << std::endl;
  std::cout << "R0:  0x" << gba.GetRegister(0) << std::endl;
  std::cout << "R12: 0x" << gba.GetRegister(12) << std::endl;

  // Step through a few VBlanks (about 3 frames) and watch R0/R12
  uint32_t lastR0 = gba.GetRegister(0);
  uint32_t lastR12 = gba.GetRegister(12);
  uint32_t lastPC = gba.GetPC();
  int irqCount = 0;

  auto &mem = gba.GetMemory();

  for (uint64_t i = 0; i < 3 * CYCLES_PER_FRAME;) {
    i += gba.Step();

    uint32_t pc = gba.GetPC();
    uint32_t r0 = gba.GetRegister(0);
    uint32_t r12 = gba.GetRegister(12);

    // Detect IRQ entry (PC jumps to low address)
    if (pc == 0x18 && lastPC > 0x1000) {
      irqCount++;
      std::cout << "\nIRQ #" << std::dec << irqCount << " entry" << std::endl;
      std::cout << "  From PC: 0x" << std::hex << lastPC << std::endl;
      std::cout << "  R0:  0x" << r0 << " (was 0x" << lastR0 << ")"
                << std::endl;
      std::cout << "  R12: 0x" << r12 << " (was 0x" << lastR12 << ")"
                << std::endl;
      std::cout << "  IF:  0x" << mem.Read16(0x04000202) << std::endl;
    }

    // Detect return from IRQ (PC goes back to high address from IRQ handler
    // area)
    if (pc > 0x3005600 && pc < 0x3005800 && lastPC < 0x3005600) {
      std::cout << "IRQ handler running at 0x" << std::hex << pc << std::endl;
    }

    // Check if R0 or R12 change in main loop area
    if (pc >= 0x30054D0 && pc <= 0x30054F0) {
      if (r0 != lastR0 || r12 != lastR12) {
        std::cout << "In loop: R0 changed: 0x" << lastR0 << " -> 0x" << r0
                  << ", R12: 0x" << lastR12 << " -> 0x" << r12 << std::endl;
      }
    }

    lastPC = pc;
    lastR0 = r0;
    lastR12 = r12;
  }

  std::cout << "\n=== After 3 frames ===" << std::endl;
  std::cout << "Final PC:  0x" << std::hex << gba.GetPC() << std::endl;
  std::cout << "Final R0:  0x" << gba.GetRegister(0) << std::endl;
  std::cout << "Final R12: 0x" << gba.GetRegister(12) << std::dec << std::endl;
  std::cout << "IRQ count: " << irqCount << std::endl;

  return 0;
}
