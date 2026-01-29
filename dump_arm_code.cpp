// Dump IWRAM code around 0x030054E0 in ARM mode
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>

#include "emulator/gba/GBA.h"

int main() {
  AIO::Emulator::GBA::GBA gba;
  gba.LoadROM("OG-DK.gba");

  const uint64_t CYCLES_PER_FRAME = 280896;

  // Run for 120 frames
  for (int f = 0; f < 120; f++) {
    uint64_t target = (f + 1) * CYCLES_PER_FRAME;
    uint64_t current = 0;
    while (current < target) {
      current += gba.Step();
    }
  }

  auto &mem = gba.GetMemory();

  // Dump IWRAM as 32-bit ARM instructions
  std::cout << "=== IWRAM ARM code at 0x030054C0 ===" << std::endl;
  std::cout << "SWI is called at PC=0x030054E0" << std::endl;
  std::cout << std::endl;

  for (uint32_t addr = 0x030054C0; addr < 0x03005540; addr += 4) {
    uint32_t insn = mem.Read32(addr);
    std::cout << "0x" << std::hex << std::setfill('0') << std::setw(8) << addr
              << ": " << std::setw(8) << insn;

    // Decode ARM instructions
    uint32_t cond = (insn >> 28) & 0xF;
    const char *condStr[] = {"EQ", "NE", "CS", "CC", "MI", "PL", "VS", "VC",
                             "HI", "LS", "GE", "LT", "GT", "LE", "AL", "NV"};

    // Check for SWI: cond 1111 xxxx xxxx xxxx xxxx xxxx xxxx
    if ((insn & 0x0F000000) == 0x0F000000) {
      uint32_t imm = insn & 0x00FFFFFF;
      uint32_t swi = (imm & 0xFF) ? (imm & 0xFF) : ((imm >> 16) & 0xFF);
      std::cout << "  ; SWI" << condStr[cond] << " 0x" << std::hex << swi;
      if (addr == 0x030054E0) {
        std::cout << "  <-- THIS ONE";
      }
    }
    // Branch
    else if ((insn & 0x0E000000) == 0x0A000000) {
      bool link = (insn & 0x01000000) != 0;
      int32_t offset = (insn & 0x00FFFFFF);
      if (offset & 0x800000)
        offset |= 0xFF000000;
      offset = (offset << 2) + 8;
      uint32_t target = addr + offset;
      std::cout << "  ; B" << (link ? "L" : "") << condStr[cond] << " 0x"
                << std::hex << target;
    }
    // LDR/STR
    else if ((insn & 0x0C000000) == 0x04000000) {
      bool load = (insn & 0x00100000) != 0;
      std::cout << "  ; " << (load ? "LDR" : "STR") << condStr[cond];
    }
    // CMP
    else if ((insn & 0x0FF00000) == 0x01500000) {
      std::cout << "  ; CMP" << condStr[cond];
    }

    std::cout << std::endl;
  }

  return 0;
}
