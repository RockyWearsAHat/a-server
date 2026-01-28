// Trace what addresses the IWRAM code reads from ROM
// The key insight is that the code loads from R5 which points to ROM
#include "emulator/gba/GBA.h"
#include "emulator/gba/GBAMemory.h"
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

int main() {
  auto gba = std::make_unique<AIO::Emulator::GBA::GBA>();

  if (!gba->LoadROM("OG-DK.gba")) {
    printf("ERROR: Failed to load OG-DK.gba\n");
    return 1;
  }

  auto &mem = gba->GetMemory();

  // Disassemble the decompressed IWRAM code
  printf("=== Disassembling IWRAM code at 0x03007400 ===\n");

  // First run the emulator until the code is decompressed
  for (int i = 0; i < 5000; i++) {
    gba->Step();
  }

  // Dump the first 32 instructions (128 bytes) with disassembly hints
  printf("\nDecompressed ARM code:\n");
  for (int i = 0; i < 128; i += 4) {
    uint32_t addr = 0x03007400 + i;
    uint32_t insn = mem.Read32(addr);
    printf("0x%08X: 0x%08X  ", addr, insn);

    // Crude ARM disassembly for key instructions
    uint32_t cond = (insn >> 28) & 0xF;

    if ((insn & 0x0F000000) == 0x05000000) { // LDR/STR
      bool isLoad = (insn >> 20) & 1;
      bool isByte = (insn >> 22) & 1;
      bool isPreIndex = (insn >> 24) & 1;
      bool addOffset = (insn >> 23) & 1;
      int rd = (insn >> 12) & 0xF;
      int rn = (insn >> 16) & 0xF;
      int offset = insn & 0xFFF;
      printf("%s%s R%d, [R%d, #%s0x%X]%s", isLoad ? "LDR" : "STR",
             isByte ? "B" : "", rd, rn, addOffset ? "+" : "-", offset,
             isPreIndex ? "" : " (post)");
    } else if ((insn & 0x0E000000) == 0x04000000) { // LDR/STR single
      bool isLoad = (insn >> 20) & 1;
      int rd = (insn >> 12) & 0xF;
      int rn = (insn >> 16) & 0xF;
      printf("%s R%d, [R%d, ...]", isLoad ? "LDR" : "STR", rd, rn);
    } else if ((insn & 0x0F000000) == 0x0F000000) { // SWI
      printf("SWI 0x%X", insn & 0xFFFFFF);
    } else if ((insn & 0x0FE00000) == 0x03A00000) { // MOV immediate
      int rd = (insn >> 12) & 0xF;
      int imm = insn & 0xFF;
      int rot = (insn >> 8) & 0xF;
      printf("MOV R%d, #0x%X (rot %d)", rd, imm, rot * 2);
    }

    printf("\n");
  }

  // Dump literal pool area (at 0x03007400 + 0x48)
  printf("\n=== Literal pool (at 0x03007448) ===\n");
  for (int i = 0; i < 64; i += 4) {
    uint32_t addr = 0x03007448 + i;
    uint32_t val = mem.Read32(addr);
    printf("[0x%08X] = 0x%08X", addr, val);
    if (val >= 0x03000000 && val < 0x04000000) {
      printf("  (IWRAM)");
    } else if (val >= 0x04000000 && val < 0x05000000) {
      printf("  (I/O)");
    } else if (val >= 0x05000000 && val < 0x06000000) {
      printf("  (Palette)");
    } else if (val >= 0x06000000 && val < 0x08000000) {
      printf("  (VRAM)");
    } else if (val >= 0x08000000 && val < 0x10000000) {
      printf("  (ROM)");
    }
    printf("\n");
  }

  // Now what values are in ROM at key addresses that the code might read?
  printf("\n=== ROM data at key addresses ===\n");
  // The code has R5 = PC + 0x38 = 0x03007400 + 8 + 0x38 = 0x03007440
  // But R5 might point elsewhere based on the literal pool

  // Let's check what ROM address might be used
  // The literal pool at 0x03007448 contains 0x01304014
  // If upper byte 0x01 is destination reg, and lower part is offset...
  // Actually looking at GBATEK, this might be part of thumb decompressor

  // Let me check what addresses the code actually reads from ROM
  uint32_t testAddrs[] = {
      0x08000000, 0x08000004, 0x08004014, 0x08005000,
      0x08006000, 0x08100000, 0x08200000, 0x08300000, // Mirror boundaries
      0x09000000, 0x09100000,                         // Wait state 1 ROM
      0x0A000000,                                     // Wait state 2 ROM
  };

  for (auto addr : testAddrs) {
    uint32_t val = mem.Read32(addr);
    printf("Read32(0x%08X) = 0x%08X\n", addr, val);
  }

  // Now check if ROM data at addresses expected by game contains valid data
  printf("\n=== Checking ROM for palette data ===\n");
  // Classic NES games often store palettes in ROM
  // Let's search for the NES Donkey Kong palette signature

  // NES palette values are typically in range 0x0000-0x7FFF
  // Let's look for sequences that look like palette data
  bool found = false;
  for (uint32_t offset = 0; offset < 0x100000; offset += 2) {
    uint16_t val = mem.Read16(0x08000000 + offset);
    // GBA palette values: 0bBBBBBGGGGGRRRRR (5 bits each)
    // Look for a sequence of 16 reasonable color values
    bool isPalette = true;
    for (int j = 0; j < 16 && isPalette; j++) {
      uint16_t c = mem.Read16(0x08000000 + offset + j * 2);
      // Check if it looks like valid 15-bit color (not 0xFFFF typically)
      if (c == 0xFFFF || c == 0x0000)
        isPalette = false;
    }
    if (isPalette) {
      printf("Possible palette at ROM offset 0x%08X:\n  ", offset);
      for (int j = 0; j < 16; j++) {
        printf("%04X ", mem.Read16(0x08000000 + offset + j * 2));
      }
      printf("\n");
      found = true;
      if (offset > 0x10000)
        break; // Stop after finding a few
    }
  }
  if (!found) {
    printf("No obvious palette sequences found in first 1MB\n");
  }

  printf("\nTest complete.\n");
  return 0;
}
