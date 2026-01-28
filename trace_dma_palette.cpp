// Trace what the custom decompressor writes to IWRAM, then what DMA copies
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

  // Run until we see the first DMA to palette RAM
  uint64_t totalCycles = 0;
  int dmaCount = 0;

  while (totalCycles < 10000000 && dmaCount < 5) {
    uint32_t pc = gba.GetPC();

    // Check DMA3 control register
    uint32_t dma3cnt = mem.Read32(0x040000DC);
    uint16_t dma3cntH = (dma3cnt >> 16) & 0xFFFF;

    // If DMA3 is triggered (bit 15 set) and destination is palette
    if ((dma3cntH & 0x8000) != 0) {
      uint32_t src = mem.Read32(0x040000D4);
      uint32_t dst = mem.Read32(0x040000D8);

      // Check if destination is palette RAM
      if ((dst & 0xFF000000) == 0x05000000) {
        dmaCount++;
        std::cout << "\n=== DMA3 to Palette #" << std::dec << dmaCount
                  << " ===" << std::endl;
        std::cout << std::hex;
        std::cout << "  PC = 0x" << std::setw(8) << pc << std::endl;
        std::cout << "  SRC = 0x" << std::setw(8) << src << std::endl;
        std::cout << "  DST = 0x" << std::setw(8) << dst << std::endl;
        std::cout << "  CNT = 0x" << std::setw(8) << dma3cnt << std::endl;
        std::cout << "  Cycle = " << std::dec << totalCycles << std::endl;

        // Dump source data
        std::cout << std::hex
                  << "\n  Source data (first 64 bytes):" << std::endl;
        for (int i = 0; i < 64; i += 16) {
          std::cout << "    [0x" << std::setw(8) << (src + i) << "]: ";
          for (int j = 0; j < 16; j++) {
            std::cout << std::setw(2) << (int)mem.Read8(src + i + j) << " ";
          }
          std::cout << std::endl;
        }

        // If source is in IWRAM, also check what the decompressor produced
        if ((src & 0xFF000000) == 0x03000000) {
          std::cout << "\n  IWRAM 0x03000000 (decompressor output):"
                    << std::endl;
          for (int i = 0; i < 64; i += 16) {
            std::cout << "    [0x" << std::setw(8) << (0x03000000 + i) << "]: ";
            for (int j = 0; j < 16; j++) {
              std::cout << std::setw(2) << (int)mem.Read8(0x03000000 + i + j)
                        << " ";
            }
            std::cout << std::endl;
          }
        }
      }
    }

    totalCycles += gba.Step();
  }

  // Final state
  std::cout << "\n=== Final State ===" << std::endl;

  // IWRAM at 0x03000000 (decompressor output)
  std::cout << "IWRAM 0x03000000 (first 64 bytes):" << std::endl;
  for (int i = 0; i < 64; i += 16) {
    std::cout << "  [0x" << std::setw(8) << (0x03000000 + i) << "]: ";
    for (int j = 0; j < 16; j++) {
      std::cout << std::setw(2) << (int)mem.Read8(0x03000000 + i + j) << " ";
    }
    std::cout << std::endl;
  }

  // Palette buffer area
  std::cout << "\nIWRAM 0x0300750C (palette buffer):" << std::endl;
  for (int i = 0; i < 64; i += 16) {
    std::cout << "  [0x" << std::setw(8) << (0x0300750C + i) << "]: ";
    for (int j = 0; j < 16; j++) {
      std::cout << std::setw(2) << (int)mem.Read8(0x0300750C + i + j) << " ";
    }
    std::cout << std::endl;
  }

  // Actual palette RAM
  std::cout << "\nPalette RAM 0x05000000 (first 64 bytes):" << std::endl;
  for (int i = 0; i < 64; i += 16) {
    std::cout << "  [0x" << std::setw(8) << (0x05000000 + i) << "]: ";
    for (int j = 0; j < 16; j++) {
      std::cout << std::setw(2) << (int)mem.Read8(0x05000000 + i + j) << " ";
    }
    std::cout << std::endl;
  }

  return 0;
}
