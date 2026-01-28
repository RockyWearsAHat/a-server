// Trace where SWI 0x02 calls originate from
// Find the PC address when SWI 0x02 is called

#include "include/emulator/gba/ARM7TDMI.h"
#include "include/emulator/gba/GBA.h"
#include "include/emulator/gba/GBAMemory.h"
#include <cstdint>
#include <cstdio>
#include <set>

using namespace AIO::Emulator::GBA;

int main() {
  GBA gba;
  if (!gba.LoadROM("OG-DK.gba")) {
    printf("Failed to load ROM\n");
    return 1;
  }
  gba.Reset();

  printf("=== Disassembling code around likely SWI 0x02 calls ===\n\n");

  // Run for a couple frames to get into main loop
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 280896; j++) {
      gba.Step();
    }
  }

  auto &mem = gba.GetMemory();

  // The game's NES emulator decompresses code to IWRAM at 0x03007400
  // Let's look at code there
  printf("Code at 0x03007400 (decompressed NES emulator):\n");
  for (int i = 0; i < 64; i += 2) {
    uint16_t insn = mem.Read16(0x03007400 + i);
    printf("0x%08X: 0x%04X  ", 0x03007400 + i, insn);

    // Basic Thumb disassembly
    if ((insn & 0xFF00) == 0xDF00) {
      printf("SWI 0x%02X", insn & 0xFF);
    } else if ((insn & 0xF800) == 0x4800) {
      int rd = (insn >> 8) & 7;
      int offset = (insn & 0xFF) * 4;
      printf("LDR R%d, [PC, #0x%X]", rd, offset);
    } else if ((insn & 0xFFC0) == 0x4700) {
      int rm = (insn >> 3) & 0xF;
      printf("BX R%d", rm);
    } else if ((insn & 0xF800) == 0xF000) {
      printf("BL (first half)");
    } else if ((insn & 0xF800) == 0xF800) {
      printf("BL (second half)");
    } else {
      printf("???");
    }
    printf("\n");
  }

  // Let's look at the main ROM around 0x08002739 (R2 value)
  printf("\n=== Code at 0x08002739 (from SWI R2 value) ===\n");
  // Align to halfword
  uint32_t base = 0x08002738;
  for (int i = 0; i < 32; i += 2) {
    uint16_t insn = mem.Read16(base + i);
    printf("0x%08X: 0x%04X  ", base + i, insn);

    if ((insn & 0xFF00) == 0xDF00) {
      printf("SWI 0x%02X", insn & 0xFF);
    } else if ((insn & 0xFFC0) == 0x4700) {
      printf("BX R%d", (insn >> 3) & 0xF);
    } else {
      printf("???");
    }
    printf("\n");
  }

  // Check what's at the ROM at 0x08002739 - this might be data, not code
  printf("\n=== Raw bytes at 0x08002738 ===\n");
  for (int i = 0; i < 16; i++) {
    printf("%02X ", mem.Read8(0x08002738 + i));
  }
  printf("\n");

  // Let's trace where the game actually calls SWI from
  printf("\n=== Looking for SWI 0x02 instructions in ROM ===\n");
  int found = 0;
  for (uint32_t addr = 0x08000000; addr < 0x08010000 && found < 20; addr += 2) {
    uint16_t insn = mem.Read16(addr);
    if (insn == 0xDF02) { // Thumb SWI 0x02
      printf("Found SWI 0x02 at 0x%08X\n", addr);
      // Show context
      printf("  Context: ");
      for (int j = -4; j <= 4; j += 2) {
        printf("%04X ", mem.Read16(addr + j));
      }
      printf("\n");
      found++;
    }
  }

  // Also check IWRAM
  printf(
      "\n=== Looking for SWI 0x02 instructions in IWRAM (0x03007400+) ===\n");
  found = 0;
  for (uint32_t addr = 0x03007400; addr < 0x03007600 && found < 20; addr += 2) {
    uint16_t insn = mem.Read16(addr);
    if (insn == 0xDF02) { // Thumb SWI 0x02
      printf("Found SWI 0x02 at 0x%08X\n", addr);
      // Show context
      printf("  Context: ");
      for (int j = -4; j <= 4; j += 2) {
        printf("%04X ", mem.Read16(addr + j));
      }
      printf("\n");
      found++;
    }
  }

  return 0;
}
