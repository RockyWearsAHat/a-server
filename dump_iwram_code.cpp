// Dump IWRAM code around the SWI 0x02 call site at 0x030054E0
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

  // Dump the IWRAM code around 0x030054E0 where SWI 0x02 is called
  std::cout << "=== IWRAM code at 0x030054C0 (SWI 0x02 call site) ==="
            << std::endl;
  std::cout << "SWI is called at PC=0x030054E0" << std::endl;
  std::cout << std::endl;

  for (uint32_t addr = 0x030054C0; addr < 0x03005520; addr += 2) {
    uint16_t insn = mem.Read16(addr);
    std::cout << "0x" << std::hex << std::setfill('0') << std::setw(8) << addr
              << ": " << std::setw(4) << insn;

    // Mark the SWI instruction
    if (addr == 0x030054E0 && insn == 0xDF02) {
      std::cout << "  <-- SWI 0x02 here";
    }
    // Decode common Thumb instructions
    else if ((insn & 0xFF00) == 0xDF00) {
      std::cout << "  ; SWI " << std::dec << (insn & 0xFF) << std::hex;
    } else if ((insn & 0xF800) == 0x4800) {
      std::cout << "  ; LDR Rx, [PC, #imm]";
    } else if ((insn & 0xF800) == 0x6000) {
      std::cout << "  ; STR Rx, [Ry, #imm]";
    } else if ((insn & 0xF800) == 0x6800) {
      std::cout << "  ; LDR Rx, [Ry, #imm]";
    } else if ((insn & 0xFF00) == 0x4700) {
      std::cout << "  ; BX Rx";
    }

    std::cout << std::endl;
  }

  // Check what's at the ROM address 0x08002739
  std::cout << "\n=== ROM data at r2=0x08002739 (first 64 bytes) ==="
            << std::endl;
  for (int i = 0; i < 64; i++) {
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << (int)mem.Read8(0x08002739 + i) << " ";
    if ((i + 1) % 16 == 0)
      std::cout << std::endl;
  }

  // Also check if there's LZ77 compressed data nearby
  std::cout << "\n=== Looking for LZ77 header (0x10) nearby ===" << std::endl;
  for (uint32_t addr = 0x08002700; addr < 0x08002780; addr++) {
    uint8_t b = mem.Read8(addr);
    if (b == 0x10) {
      uint32_t hdr = mem.Read32(addr);
      uint32_t size = hdr >> 8;
      if (size > 0 && size < 0x10000) {
        std::cout << "Potential LZ77 at 0x" << std::hex << addr
                  << " size=" << std::dec << size << std::endl;
      }
    }
  }

  return 0;
}
