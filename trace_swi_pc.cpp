// Trace execution around SWI 0x02 calls
// Log the PC when SWI 0x02 is called

#include "include/emulator/gba/ARM7TDMI.h"
#include "include/emulator/gba/GBA.h"
#include "include/emulator/gba/GBAMemory.h"
#include <cstdint>
#include <cstdio>
#include <map>

using namespace AIO::Emulator::GBA;

int main() {
  GBA gba;
  if (!gba.LoadROM("OG-DK.gba")) {
    printf("Failed to load ROM\n");
    return 1;
  }
  gba.Reset();

  printf("=== Tracing SWI calls and their source PC ===\n\n");

  // We'll run step by step and watch for SWI instructions
  auto &mem = gba.GetMemory();

  std::map<uint32_t, int> swiSources; // PC -> count

  int swiCount = 0;
  uint64_t steps = 0;

  // Run for about 2 frames worth
  while (steps < 600000 && swiCount < 30) {
    uint32_t pc = gba.GetPC();
    bool thumb = gba.IsThumbMode();

    uint16_t insn = 0;
    uint32_t insnArm = 0;

    if (thumb) {
      insn = mem.Read16(pc);
      // Check for Thumb SWI (0xDF00-0xDFFF)
      if ((insn & 0xFF00) == 0xDF00) {
        int swiNum = insn & 0xFF;
        if (swiNum == 0x02) {
          swiSources[pc]++;
          if (swiCount < 10) {
            printf(
                "Thumb SWI 0x02 at PC=0x%08X  R0=0x%08X R1=0x%08X R2=0x%08X\n",
                pc, gba.GetRegister(0), gba.GetRegister(1), gba.GetRegister(2));

            // Show surrounding code
            printf("  Context: ");
            for (int i = -6; i <= 6; i += 2) {
              if (i == 0)
                printf("[");
              printf("%04X", mem.Read16(pc + i));
              if (i == 0)
                printf("]");
              printf(" ");
            }
            printf("\n");
          }
          swiCount++;
        }
      }
    } else {
      insnArm = mem.Read32(pc);
      // Check for ARM SWI (0x0F000000 mask)
      if ((insnArm & 0x0F000000) == 0x0F000000) {
        int swiNum = insnArm & 0xFFFFFF;
        if (swiNum == 0x02 || (swiNum >> 16) == 0x02) {
          swiSources[pc]++;
          if (swiCount < 10) {
            printf(
                "ARM SWI 0x%06X at PC=0x%08X  R0=0x%08X R1=0x%08X R2=0x%08X\n",
                swiNum, pc, gba.GetRegister(0), gba.GetRegister(1),
                gba.GetRegister(2));
          }
          swiCount++;
        }
      }
    }

    gba.Step();
    steps++;
  }

  printf("\n=== SWI 0x02 call sites summary ===\n");
  for (auto &[pc, count] : swiSources) {
    printf("PC=0x%08X: %d calls\n", pc, count);
  }

  // Let's also look at what the game might be using SWI 0x02 for
  // In Classic NES games, they sometimes replace BIOS SWI handlers
  printf("\n=== Checking BIOS area for custom SWI vector ===\n");
  // The SWI vector is at 0x08 in the exception vector table
  // But GBA doesn't use a vector table - it jumps to fixed BIOS addresses
  // Let's check if the game has its own SWI handler in IWRAM

  printf("Looking for potential SWI handler setup...\n");
  // Classic NES games might set up their own interrupt handlers
  // Check the interrupt vector area at 0x03007FFC (IRQ handler address)
  uint32_t irqHandler = mem.Read32(0x03007FFC);
  printf("IRQ handler address: 0x%08X\n", irqHandler);

  return 0;
}
