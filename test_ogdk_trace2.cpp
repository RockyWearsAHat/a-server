// Deep trace OG-DK to see memory accesses at protection check
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "emulator/gba/ARM7TDMI.h"
#include "emulator/gba/GBA.h"

int main() {
  AIO::Emulator::GBA::GBA gba;

  if (!gba.LoadROM("/Users/alexwaldmann/Desktop/AIO Server/OG-DK.gba")) {
    printf("Failed to load ROM\n");
    return 1;
  }

  auto &mem = gba.GetMemory();

  // Run for 60 frames to get to the error state
  const int CYCLES_PER_FRAME = 280896;
  int cyclesRun = 0;
  printf("Running for ~60 frames...\n");
  while (cyclesRun < CYCLES_PER_FRAME * 60) {
    int c = gba.Step();
    cyclesRun += c;
  }

  printf("\n=== Dumping IWRAM at 0x03000090-0x030000E0 ===\n");
  for (uint32_t addr = 0x03000090; addr < 0x030000E0; addr += 4) {
    uint32_t val = mem.Read32(addr);
    printf("0x%08X: 0x%08X\n", addr, val);
  }

  // Look at the literal pool that should be at 0x030000CC
  printf("\n=== Literal pool analysis ===\n");
  uint32_t dma0sad_ptr = mem.Read32(0x030000CC);
  uint32_t literal_d0 = mem.Read32(0x030000D0);
  uint32_t literal_d4 = mem.Read32(0x030000D4);
  printf("LDR R1 target (0x030000CC): 0x%08X\n", dma0sad_ptr);
  printf("LDR R0 target (0x030000D0): 0x%08X\n", literal_d0);
  printf("LDR R0 target (0x030000D4): 0x%08X\n", literal_d4);

  // Check what address the game expects to read from for protection
  // Let's look at the data literal at 0x03000098 (32-bit)
  printf("\n=== Data at 0x03000098 ===\n");
  uint32_t data_98 = mem.Read32(0x03000098);
  printf("0x03000098: 0x%08X\n", data_98);

  // Now trace a few iterations of the loop
  printf("\n=== Tracing code execution ===\n");
  for (int iter = 0; iter < 3; iter++) {
    printf("\n--- Iteration %d ---\n", iter);
    for (int i = 0; i < 50; i++) {
      uint32_t pc = gba.GetPC();
      uint32_t cpsr = gba.GetCPSR();
      bool thumb = (cpsr >> 5) & 1;

      if (pc >= 0x030000A0 && pc <= 0x030000C0) {
        uint16_t instr = mem.Read16(pc);
        printf("PC=0x%08X instr=0x%04X", pc, instr);

        // Print register values for key instructions
        if ((instr & 0xF800) == 0x4800 || (instr & 0xF800) == 0x4900) {
          // LDR Rd, [PC, #imm]
          int rd = (instr >> 8) & 7;
          int imm = (instr & 0xFF) << 2;
          uint32_t target = (pc + 4 + imm) & ~3;
          uint32_t loaded = mem.Read32(target);
          printf(" | LDR R%d,[PC,#0x%X] → [0x%08X]=0x%08X", rd, imm, target,
                 loaded);
        } else if ((instr & 0xF800) == 0x6800) {
          // LDR Rd, [Rn, #imm]
          int rd = instr & 7;
          int rn = (instr >> 3) & 7;
          int imm = ((instr >> 6) & 0x1F) << 2;
          uint32_t base = gba.GetRegister(rn);
          uint32_t loaded = mem.Read32(base + imm);
          printf(" | LDR R%d,[R%d,#0x%X] → [0x%08X]=0x%08X", rd, rn, imm,
                 base + imm, loaded);
        } else if ((instr & 0xF800) == 0x6000) {
          // STR Rd, [Rn, #imm]
          int rd = instr & 7;
          int rn = (instr >> 3) & 7;
          int imm = ((instr >> 6) & 0x1F) << 2;
          uint32_t base = gba.GetRegister(rn);
          uint32_t val = gba.GetRegister(rd);
          printf(" | STR R%d,[R%d,#0x%X] → [0x%08X]←0x%08X", rd, rn, imm,
                 base + imm, val);
        }

        printf("\n");
      }
      gba.Step();
    }
  }

  // Check what the game expects from address 0x00AE0000
  printf("\n=== Reading from 0x00AE0000 (open bus area) ===\n");
  uint8_t val8 = mem.Read8(0x00AE0000);
  uint16_t val16 = mem.Read16(0x00AE0000);
  uint32_t val32 = mem.Read32(0x00AE0000);
  printf("Read8(0x00AE0000) = 0x%02X\n", val8);
  printf("Read16(0x00AE0000) = 0x%04X\n", val16);
  printf("Read32(0x00AE0000) = 0x%08X\n", val32);

  // Also check what ROM looks like at mirrors
  printf("\n=== ROM mirroring check ===\n");
  printf("ROM Read32(0x08000000) = 0x%08X\n", mem.Read32(0x08000000));
  printf("ROM Read32(0x08100000) = 0x%08X (1st mirror)\n",
         mem.Read32(0x08100000));
  printf("ROM Read32(0x08200000) = 0x%08X (2nd mirror)\n",
         mem.Read32(0x08200000));
  printf("ROM Read32(0x08300000) = 0x%08X (3rd mirror)\n",
         mem.Read32(0x08300000));
  printf("ROM Read32(0x08400000) = 0x%08X (beyond 4MB)\n",
         mem.Read32(0x08400000));
  printf("ROM Read32(0x09000000) = 0x%08X (wait state 1)\n",
         mem.Read32(0x09000000));
  printf("ROM Read32(0x0A000000) = 0x%08X (wait state 2)\n",
         mem.Read32(0x0A000000));

  return 0;
}
