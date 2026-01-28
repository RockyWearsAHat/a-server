// Detailed trace of IWRAM code execution with register values
#include "include/emulator/gba/GBACPU.h"
#include "include/emulator/gba/GBACore.h"
#include "include/emulator/gba/GBAMemory.h"
#include <cstdint>
#include <cstdio>

using namespace AIO::Emulator::GBA;

static GBACore *g_core = nullptr;

int main() {
  GBACore core;
  g_core = &core;

  if (!core.LoadROM("OG-DK.gba")) {
    printf("Failed to load ROM\n");
    return 1;
  }

  auto &cpu = core.GetCPU();
  auto &mem = core.GetMemory();

  // Run until LZ77 decompression happens (SWI 0x11)
  bool inIWRAM = false;
  int iwramInsnCount = 0;
  uint32_t lastR5 = 0;
  uint32_t lastR6 = 0;
  uint32_t lastR7 = 0;
  uint32_t lastR8 = 0;

  for (int i = 0; i < 1000000; i++) {
    uint32_t pc = cpu.GetRegister(15);

    // Once PC enters IWRAM, trace with detail
    if (pc >= 0x03007400 && pc < 0x03007600) {
      if (!inIWRAM) {
        printf("\n=== ENTERING IWRAM CODE ===\n");
        inIWRAM = true;
      }
      iwramInsnCount++;

      uint32_t op = mem.Read32(pc);
      uint32_t r0 = cpu.GetRegister(0);
      uint32_t r1 = cpu.GetRegister(1);
      uint32_t r2 = cpu.GetRegister(2);
      uint32_t r3 = cpu.GetRegister(3);
      uint32_t r4 = cpu.GetRegister(4);
      uint32_t r5 = cpu.GetRegister(5);
      uint32_t r6 = cpu.GetRegister(6);
      uint32_t r7 = cpu.GetRegister(7);
      uint32_t r8 = cpu.GetRegister(8);
      uint32_t r9 = cpu.GetRegister(9);

      // Print first 50 instructions with full register context
      if (iwramInsnCount <= 50) {
        printf("[%04d] PC=0x%08x OP=0x%08x\n", iwramInsnCount, pc, op);
        printf("       R0=%08x R1=%08x R2=%08x R3=%08x\n", r0, r1, r2, r3);
        printf("       R4=%08x R5=%08x R6=%08x R7=%08x\n", r4, r5, r6, r7);
        printf("       R8=%08x R9=%08x\n", r8, r9);
      }

      // Track changes to R5 (data pointer) and R6/R7 (loaded data)
      if (r5 != lastR5 && lastR5 != 0) {
        printf("  ** R5 changed: 0x%08x -> 0x%08x\n", lastR5, r5);
      }
      lastR5 = r5;

      // Show when R7 is loaded (it's the IWRAM base register in this code)
      if (r7 != lastR7 && iwramInsnCount > 1) {
        printf("  ** R7 changed: 0x%08x -> 0x%08x (cycle %d, insn %d)\n",
               lastR7, r7, i, iwramInsnCount);
      }
      lastR7 = r7;

      // Every 10000 instructions, show state
      if (iwramInsnCount % 10000 == 0) {
        printf("[%d insns] R5=0x%08x R6=0x%08x R7=0x%08x R8=0x%08x\n",
               iwramInsnCount, r5, r6, r7, r8);
      }

      // Stop after 50000 instructions in IWRAM
      if (iwramInsnCount > 50000) {
        printf("\n=== REACHED 50000 IWRAM INSTRUCTIONS ===\n");
        printf("Final state:\n");
        printf("  R0=%08x R1=%08x R2=%08x R3=%08x\n", r0, r1, r2, r3);
        printf("  R4=%08x R5=%08x R6=%08x R7=%08x\n", r4, r5, r6, r7);
        printf("  R8=%08x R9=%08x\n", r8, r9);
        break;
      }
    } else if (inIWRAM) {
      printf("\n=== LEFT IWRAM at PC=0x%08x after %d instructions ===\n", pc,
             iwramInsnCount);

      // Continue execution and re-enter check
      if (pc >= 0x08000000 && pc < 0x0A000000) {
        // Back in ROM - this is expected (BX LR or similar)
        inIWRAM = false;
      }
    }

    cpu.Step();
  }

  // Dump palette buffer area
  printf("\n=== PALETTE BUFFER (0x0300750C) ===\n");
  for (int i = 0; i < 64; i += 4) {
    uint32_t addr = 0x0300750C + i;
    uint32_t val = mem.Read32(addr);
    printf("[0x%08x] = 0x%08x\n", addr, val);
  }

  // Dump palette RAM
  printf("\n=== PALETTE RAM (0x05000000) ===\n");
  for (int i = 0; i < 32; i += 4) {
    uint32_t addr = 0x05000000 + i;
    uint32_t val = mem.Read32(addr);
    printf("[0x%08x] = 0x%08x\n", addr, val);
  }

  return 0;
}
