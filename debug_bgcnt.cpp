// Debug BG0CNT and VRAM addresses
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

  // Run for 100 frames
  for (int f = 0; f < 100; f++) {
    for (int i = 0; i < 280896; i++) {
      gba.Step();
    }
  }

  std::cout << std::hex << std::setfill('0');

  // Read BG0CNT
  uint16_t bg0cnt = mem.Read16(0x04000008);
  std::cout << "BG0CNT (raw) = 0x" << std::setw(4) << bg0cnt << std::endl;

  // Parse fields
  int charBaseBlock = (bg0cnt >> 2) & 3;
  int screenBaseBlock = (bg0cnt >> 8) & 0x1F;

  std::cout << "Char base block = " << std::dec << charBaseBlock << std::endl;
  std::cout << "Screen base block = " << screenBaseBlock << std::endl;

  // Calculate addresses
  uint32_t charBase = 0x06000000 + (charBaseBlock * 0x4000);
  uint32_t screenBase = 0x06000000 + (screenBaseBlock * 0x800);

  std::cout << std::hex;
  std::cout << "Char base addr = 0x" << std::setw(8) << charBase << std::endl;
  std::cout << "Screen base addr = 0x" << std::setw(8) << screenBase
            << std::endl;

  // Now read VRAM directly at the calculated address
  std::cout << "\nReading VRAM at 0x06004000:" << std::endl;
  for (int i = 0; i < 32; i++) {
    std::cout << std::setw(2) << (int)mem.Read8(0x06004000 + i) << " ";
  }
  std::cout << std::endl;

  std::cout << "\nReading VRAM at charBase:" << std::endl;
  for (int i = 0; i < 32; i++) {
    std::cout << std::setw(2) << (int)mem.Read8(charBase + i) << " ";
  }
  std::cout << std::endl;

  return 0;
}
