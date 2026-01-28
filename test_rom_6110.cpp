// Test what the emulator returns for ROM at 0x08006110
#include <emulator/gba/GBA.h>
#include <emulator/gba/GBAMemory.h>
#include <iomanip>
#include <iostream>

using namespace AIO::Emulator::GBA;

int main() {
  GBA gba;
  if (!gba.LoadROM("OG-DK.gba")) {
    std::cerr << "Failed to load ROM" << std::endl;
    return 1;
  }

  auto &mem = gba.GetMemory();

  std::cout << std::hex << std::setfill('0');

  // Check data at 0x08006110 - the source for the custom decompressor
  std::cout << "=== ROM data at 0x08006110 (custom decompressor source) ==="
            << std::endl;
  for (int i = 0; i < 64; i += 4) {
    uint32_t addr = 0x08006110 + i;
    uint8_t b0 = mem.Read8(addr);
    uint8_t b1 = mem.Read8(addr + 1);
    uint8_t b2 = mem.Read8(addr + 2);
    uint8_t b3 = mem.Read8(addr + 3);
    uint32_t word = mem.Read32(addr);
    std::cout << "  [0x" << std::setw(8) << addr << "] bytes: " << std::setw(2)
              << (int)b0 << " " << std::setw(2) << (int)b1 << " "
              << std::setw(2) << (int)b2 << " " << std::setw(2) << (int)b3
              << "  word: 0x" << std::setw(8) << word << std::endl;
  }

  // Compare with raw ROM file
  std::cout << "\n=== Raw ROM file comparison ===" << std::endl;
  FILE *f = fopen("OG-DK.gba", "rb");
  if (f) {
    fseek(f, 0x6110, SEEK_SET);
    uint8_t buf[64];
    fread(buf, 1, 64, f);
    fclose(f);

    std::cout << "Raw ROM file at offset 0x6110:" << std::endl;
    for (int i = 0; i < 64; i += 16) {
      std::cout << "  ";
      for (int j = 0; j < 16; j++) {
        std::cout << std::setw(2) << (int)buf[i + j] << " ";
      }
      std::cout << std::endl;
    }

    // Check if emulator returns same data
    std::cout << "\nComparing emulator vs ROM file:" << std::endl;
    bool mismatch = false;
    for (int i = 0; i < 64; i++) {
      uint8_t emu = mem.Read8(0x08006110 + i);
      if (emu != buf[i]) {
        std::cout << "  MISMATCH at offset " << i << ": emu=0x" << std::setw(2)
                  << (int)emu << " rom=0x" << std::setw(2) << (int)buf[i]
                  << std::endl;
        mismatch = true;
      }
    }
    if (!mismatch) {
      std::cout << "  All 64 bytes match!" << std::endl;
    }
  }

  return 0;
}
