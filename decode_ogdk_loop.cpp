// Decode the exact loop at 0x30054e4 to understand what it's waiting for
#include "emulator/gba/GBA.h"
#include <cstdint>
#include <iomanip>
#include <iostream>

// Simple ARM instruction decoder
void decodeARM(uint32_t instr, uint32_t addr) {
  uint32_t cond = (instr >> 28) & 0xF;
  const char *condStr[] = {"EQ", "NE", "CS", "CC", "MI", "PL", "VS", "VC",
                           "HI", "LS", "GE", "LT", "GT", "LE", "",   "NV"};

  if ((instr & 0x0F000000) == 0x0A000000) {
    // Branch
    int32_t offset = (instr & 0x00FFFFFF);
    if (offset & 0x00800000)
      offset |= 0xFF000000; // Sign extend
    uint32_t target = addr + 8 + (offset << 2);
    std::cout << "B" << condStr[cond] << " 0x" << std::hex << target;
  } else if ((instr & 0x0F000000) == 0x0B000000) {
    // BL
    int32_t offset = (instr & 0x00FFFFFF);
    if (offset & 0x00800000)
      offset |= 0xFF000000;
    uint32_t target = addr + 8 + (offset << 2);
    std::cout << "BL" << condStr[cond] << " 0x" << std::hex << target;
  } else if ((instr & 0x0E000000) == 0x02000000) {
    // Data processing
    uint32_t opcode = (instr >> 21) & 0xF;
    const char *opcodeStr[] = {"AND", "EOR", "SUB", "RSB", "ADD", "ADC",
                               "SBC", "RSC", "TST", "TEQ", "CMP", "CMN",
                               "ORR", "MOV", "BIC", "MVN"};
    uint32_t rn = (instr >> 16) & 0xF;
    uint32_t rd = (instr >> 12) & 0xF;
    std::cout << opcodeStr[opcode] << condStr[cond] << " R" << rd << ", R"
              << rn;
  } else if ((instr & 0x0F000000) == 0x0F000000) {
    // SWI
    uint32_t swi = instr & 0x00FFFFFF;
    std::cout << "SWI" << condStr[cond] << " 0x" << std::hex << swi;
  } else {
    std::cout << "??? (0x" << std::hex << instr << ")";
  }
}

int main() {
  AIO::Emulator::GBA::GBA gba;
  gba.LoadROM("OG-DK.gba");

  const uint64_t CYCLES_PER_FRAME = 280896;
  uint64_t totalCycles = 0;

  // Run until game reaches stuck state
  while (totalCycles < 30 * CYCLES_PER_FRAME) {
    totalCycles += gba.Step();
  }

  auto &mem = gba.GetMemory();
  uint32_t pc = gba.GetPC();

  std::cout << "=== Game stuck at PC: 0x" << std::hex << pc
            << " ===" << std::endl;

  // Decode instructions around PC
  std::cout << "\nInstructions around stuck PC:" << std::endl;
  for (int i = -20; i <= 20; i += 4) {
    uint32_t addr = pc + i;
    uint32_t instr = mem.Read32(addr);
    std::cout << "0x" << std::setw(8) << std::setfill('0') << addr << ": 0x"
              << std::setw(8) << std::setfill('0') << instr << "  ";
    decodeARM(instr, addr);
    if (addr == pc)
      std::cout << " <-- STUCK HERE";
    std::cout << std::dec << std::endl;
  }

  // Print register state
  std::cout << "\n=== CPU Registers ===" << std::endl;
  std::cout << std::hex;
  std::cout << "R0:  0x" << std::setw(8) << std::setfill('0')
            << gba.GetRegister(0) << std::endl;
  std::cout << "R1:  0x" << std::setw(8) << std::setfill('0')
            << gba.GetRegister(1) << std::endl;
  std::cout << "R2:  0x" << std::setw(8) << std::setfill('0')
            << gba.GetRegister(2) << std::endl;
  std::cout << "R3:  0x" << std::setw(8) << std::setfill('0')
            << gba.GetRegister(3) << std::endl;
  std::cout << "R12: 0x" << std::setw(8) << std::setfill('0')
            << gba.GetRegister(12) << std::endl;
  std::cout << "R14: 0x" << std::setw(8) << std::setfill('0')
            << gba.GetRegister(14) << std::endl;
  std::cout << "CPSR: 0x" << gba.GetCPSR() << std::dec << std::endl;

  // What is the game comparing?
  std::cout << "\n=== What R0 and R12 contain ===" << std::endl;
  uint32_t r0 = gba.GetRegister(0);
  uint32_t r12 = gba.GetRegister(12);
  std::cout << "R0  = 0x" << std::hex << r0 << " (compare value)" << std::endl;
  std::cout << "R12 = 0x" << r12 << " (compare value)" << std::endl;

  if (r0 == r12) {
    std::cout << "R0 == R12, condition Z flag should be set" << std::endl;
  } else {
    std::cout << "R0 != R12, loop should NOT execute" << std::endl;
  }

  return 0;
}
