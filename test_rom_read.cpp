// Test ROM read at address 0x08004014
#include <emulator/gba/GBA.h>
#include <emulator/gba/GBAMemory.h>
#include <fstream>
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

  // Check ROM read at various addresses
  std::cout << "=== ROM Read Test ===" << std::endl;
  std::cout << "Read8(0x08004014)  = 0x" << std::setw(2)
            << (int)mem.Read8(0x08004014) << std::endl;
  std::cout << "Read8(0x08004015)  = 0x" << std::setw(2)
            << (int)mem.Read8(0x08004015) << std::endl;
  std::cout << "Read16(0x08004014) = 0x" << std::setw(4)
            << mem.Read16(0x08004014) << std::endl;
  std::cout << "Read32(0x08004014) = 0x" << std::setw(8)
            << mem.Read32(0x08004014) << std::endl;

  // Compare with raw ROM file
  std::ifstream rom("OG-DK.gba", std::ios::binary);
  std::vector<uint8_t> romData(std::istreambuf_iterator<char>(rom), {});

  std::cout << "\nRaw ROM at offset 0x4014:" << std::endl;
  std::cout << "  [0x4014] = 0x" << std::setw(2) << (int)romData[0x4014]
            << std::endl;
  std::cout << "  [0x4015] = 0x" << std::setw(2) << (int)romData[0x4015]
            << std::endl;
  std::cout << "  [0x4016] = 0x" << std::setw(2) << (int)romData[0x4016]
            << std::endl;
  std::cout << "  [0x4017] = 0x" << std::setw(2) << (int)romData[0x4017]
            << std::endl;

  uint32_t rawVal = romData[0x4014] | (romData[0x4015] << 8) |
                    (romData[0x4016] << 16) | (romData[0x4017] << 24);
  std::cout << "  As 32-bit: 0x" << std::setw(8) << rawVal << std::endl;

  // Test ROM mirroring
  std::cout << "\n=== ROM Mirroring Test ===" << std::endl;
  uint32_t romSize = romData.size();
  std::cout << "ROM size: 0x" << romSize << " (" << std::dec << romSize
            << " bytes)" << std::endl;
  std::cout << std::hex;

  // Test various mirror addresses
  std::cout << "Read32(0x08000000) = 0x" << std::setw(8)
            << mem.Read32(0x08000000) << " (base)" << std::endl;
  std::cout << "Read32(0x08100000) = 0x" << std::setw(8)
            << mem.Read32(0x08100000) << " (mirror 1)" << std::endl;
  std::cout << "Read32(0x08200000) = 0x" << std::setw(8)
            << mem.Read32(0x08200000) << " (mirror 2)" << std::endl;

  return 0;
}
