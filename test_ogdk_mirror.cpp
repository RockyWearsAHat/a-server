// Trace ROM mirroring behavior for Classic NES games
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "emulator/gba/GBA.h"

int main() {
  AIO::Emulator::GBA::GBA gba;

  if (!gba.LoadROM("/Users/alexwaldmann/Desktop/AIO Server/OG-DK.gba")) {
    printf("Failed to load ROM\n");
    return 1;
  }

  auto &mem = gba.GetMemory();

  printf("=== Testing ROM mirroring for Classic NES Series ===\n\n");

  // Test read8 at various addresses
  printf("Read8 tests:\n");
  for (uint32_t addr :
       {0x08000000u, 0x08100000u, 0x08200000u, 0x08300000u, 0x08400000u,
        0x08400001u, 0x08400002u, 0x08400003u, 0x09000000u, 0x0A000000u}) {
    uint8_t val = mem.Read8(addr);
    printf("  Read8(0x%08X) = 0x%02X\n", addr, val);
  }

  printf("\nRead16 tests:\n");
  for (uint32_t addr : {0x08400000u, 0x08400002u, 0x09000000u}) {
    uint16_t val = mem.Read16(addr);
    printf("  Read16(0x%08X) = 0x%04X\n", addr, val);
  }

  printf("\nRead32 tests:\n");
  for (uint32_t addr : {0x08400000u, 0x09000000u, 0x0A000000u}) {
    uint32_t val = mem.Read32(addr);
    printf("  Read32(0x%08X) = 0x%08X\n", addr, val);
  }

  printf("\n=== Manual open bus calculation ===\n");
  // Calculate what open bus should return for 0x08400000
  uint32_t testAddr = 0x08400000;
  printf("For address 0x%08X:\n", testAddr);
  printf("  (addr >> 1) = 0x%08X\n", testAddr >> 1);
  printf("  Byte 0: ((addr >> 1) >> ((addr & 1) * 8)) & 0xFF = 0x%02X\n",
         ((testAddr >> 1) >> ((testAddr & 1) * 8)) & 0xFF);
  testAddr = 0x08400001;
  printf("  Byte 1: ((addr >> 1) >> ((addr & 1) * 8)) & 0xFF = 0x%02X\n",
         ((testAddr >> 1) >> ((testAddr & 1) * 8)) & 0xFF);
  testAddr = 0x08400002;
  printf("  Byte 2: ((addr >> 1) >> ((addr & 1) * 8)) & 0xFF = 0x%02X\n",
         ((testAddr >> 1) >> ((testAddr & 1) * 8)) & 0xFF);
  testAddr = 0x08400003;
  printf("  Byte 3: ((addr >> 1) >> ((addr & 1) * 8)) & 0xFF = 0x%02X\n",
         ((testAddr >> 1) >> ((testAddr & 1) * 8)) & 0xFF);

  printf("\n=== What mGBA expects (LOAD_CART pattern) ===\n");
  // mGBA pattern: ((aligned >> 1) & 0xFFFF) | (((aligned + 2) >> 1) << 16)
  uint32_t aligned = 0x08400000 & ~3u;
  uint32_t expected = ((aligned >> 1) & 0xFFFF) | (((aligned + 2) >> 1) << 16);
  printf("Address 0x%08X aligned = 0x%08X\n", 0x08400000, aligned);
  printf("Expected = ((0x%08X >> 1) & 0xFFFF) | (((0x%08X + 2) >> 1) << 16)\n",
         aligned, aligned);
  printf("         = ((0x%08X) & 0xFFFF) | ((0x%08X) << 16)\n", aligned >> 1,
         (aligned + 2) >> 1);
  printf("         = (0x%04X) | (0x%04X << 16)\n", (aligned >> 1) & 0xFFFF,
         (aligned + 2) >> 1);
  printf("         = 0x%08X\n", expected);

  return 0;
}
