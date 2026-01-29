// Trace OG-DK custom decompressor code execution at 0x03007400
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

  printf("=== Initial state ===\n");
  printf("PC=0x%08X\n\n", gba->GetPC());

  // Run until we hit IWRAM execution (code at 0x03007400) or timeout
  printf("Running until IWRAM code execution or timeout...\n");
  uint64_t totalCycles = 0;
  uint64_t maxCycles = 50000000; // About 3 seconds worth
  bool foundIWRAMExec = false;
  uint32_t lastPC = 0;
  int iwramExecCount = 0;

  while (totalCycles < maxCycles) {
    int cycles = gba->Step();
    totalCycles += cycles;

    uint32_t pc = gba->GetPC();

    // Detect IWRAM execution in the decompressed code region
    if (pc >= 0x03007400 && pc < 0x03007500 && pc != lastPC) {
      if (iwramExecCount < 5) {
        printf("IWRAM exec PC=0x%08X cycles=%llu\n", pc, totalCycles);
        iwramExecCount++;
      }
      if (!foundIWRAMExec) {
        foundIWRAMExec = true;

        // Dump registers
        printf("\n=== First IWRAM execution ===\n");
        for (int r = 0; r <= 14; r++) {
          printf("  R%d = 0x%08X\n", r, gba->GetRegister(r));
        }
        printf("  PC = 0x%08X  CPSR = 0x%08X\n", gba->GetPC(), gba->GetCPSR());

        // Dump palette buffer area
        printf(
            "\nPalette buffer (0x0300750C - should be zeros before init):\n  ");
        for (int j = 0; j < 32; j++) {
          printf("%02X ", mem.Read8(0x0300750C + j));
        }
        printf("\n");
      }
    }

    lastPC = pc;

    // Check periodically if palette buffer got written
    if (totalCycles % 1000000 == 0) {
      uint32_t palBuf0 = mem.Read32(0x0300750C);
      if (palBuf0 != 0) {
        printf("Palette buffer non-zero at cycle %llu: 0x%08X\n", totalCycles,
               palBuf0);
      }
    }
  }

  if (!foundIWRAMExec) {
    printf("Did not find IWRAM execution in %llu cycles\n", totalCycles);
  }

  printf("\n=== Final state after %llu cycles ===\n", totalCycles);
  printf("PC=0x%08X CPSR=0x%08X\n", gba->GetPC(), gba->GetCPSR());

  // Dump palette buffer
  printf("\nPalette buffer (0x0300750C):\n  ");
  for (int j = 0; j < 64; j++) {
    printf("%02X ", mem.Read8(0x0300750C + j));
    if ((j + 1) % 16 == 0)
      printf("\n  ");
  }

  // Dump actual palette RAM
  printf("\nPalette RAM (0x05000000):\n  ");
  for (int j = 0; j < 64; j++) {
    printf("%02X ", mem.Read8(0x05000000 + j));
    if ((j + 1) % 16 == 0)
      printf("\n  ");
  }

  // Dump VRAM tilemap
  printf("\nVRAM Tilemap (0x06006800):\n  ");
  for (int j = 0; j < 64; j++) {
    printf("%02X ", mem.Read8(0x06006800 + j));
    if ((j + 1) % 16 == 0)
      printf("\n  ");
  }

  // Check graphics registers
  uint16_t dispcnt = mem.Read16(0x04000000);
  uint16_t bg0cnt = mem.Read16(0x04000008);
  printf("\nGraphics registers:\n");
  printf("  DISPCNT = 0x%04X\n", dispcnt);
  printf("  BG0CNT  = 0x%04X\n", bg0cnt);

  printf("\nTest complete.\n");
  return 0;
}
