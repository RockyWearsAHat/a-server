// Test ROM mirroring for 1 MiB ROMs (Classic NES Series)
#include "emulator/gba/GBA.h"
#include "emulator/gba/GBAMemory.h"
#include <cstdint>
#include <cstdio>
#include <memory>

int main() {
  auto gba = std::make_unique<AIO::Emulator::GBA::GBA>();

  // Load OG-DK.gba
  if (!gba->LoadROM("OG-DK.gba")) {
    printf("ERROR: Failed to load OG-DK.gba\n");
    return 1;
  }

  auto &mem = gba->GetMemory();

  // Read first 4 bytes of ROM at base address
  uint32_t base_val = mem.Read32(0x08000000);
  printf("\nROM base (0x08000000): 0x%08X\n", base_val);

  // Test mirroring at various offsets - for 1 MiB ROM:
  // 0x08000000 to 0x080FFFFF is the base
  // 0x08100000 to 0x081FFFFF should mirror (if mirroring works)
  printf("\nROM mirroring test:\n");
  uint32_t mirrors[] = {
      0x08000000, // Base
      0x08100000, // 1 MiB offset (1st mirror for 1 MiB ROM)
      0x08200000, // 2 MiB offset (2nd mirror)
      0x08300000, // 3 MiB offset (3rd mirror)
      0x08400000, // 4 MiB offset (wraps to 0)
      0x09000000, // Wait State 1 base
      0x09100000, // WS1 + 1 MiB
      0x0A000000, // Wait State 2 base
      0x0A100000, // WS2 + 1 MiB
  };

  for (uint32_t addr : mirrors) {
    uint32_t val = mem.Read32(addr);
    bool matches = (val == base_val);
    printf("  Read32(0x%08X) = 0x%08X %s\n", addr, val,
           matches ? "(matches base)" : "(DIFFERENT!)");
  }

  // Test byte reads at offset addresses
  printf("\nByte-level mirroring test (offset 0x10):\n");
  uint8_t base_byte = mem.Read8(0x08000010);
  printf("  Base byte (0x08000010): 0x%02X\n", base_byte);

  uint32_t byte_offsets[] = {0x08000010, 0x08100010, 0x08200010, 0x08300010,
                             0x09000010};
  for (uint32_t addr : byte_offsets) {
    uint8_t val = mem.Read8(addr);
    bool matches = (val == base_byte);
    printf("  Read8(0x%08X) = 0x%02X %s\n", addr, val,
           matches ? "(matches)" : "(DIFFERENT!)");
  }

  // Check the literal pool address 0x01304014 that the decompressed code
  // references For wait state region 0x09, this becomes (0x09304014 &
  // 0x01FFFFFF) = 0x01304014 For a 1 MiB ROM, this wraps: 0x01304014 % 0x100000
  // = 0x004014
  printf("\nLiteral pool address check (0x01304014 % 1MiB = 0x004014):\n");
  printf("  Read32(0x09304014) = 0x%08X\n", mem.Read32(0x09304014));
  printf("  Read32(0x08004014) = 0x%08X (expected same)\n",
         mem.Read32(0x08004014));

  // Also test the other literal pool addresses from decompressed code
  printf("\nOther literal pool addresses:\n");
  printf("  Read32(0x06002000) = 0x%08X (VRAM tilemap)\n",
         mem.Read32(0x06002000));
  printf("  Read32(0x0600b1a4) = 0x%08X (VRAM)\n", mem.Read32(0x0600b1a4));
  printf("  Read32(0x06000080) = 0x%08X (VRAM tile data)\n",
         mem.Read32(0x06000080));

  printf("\nTest complete.\n");
  return 0;
}
