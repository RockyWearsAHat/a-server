// OG-DK Tilemap Debug - Analyze BG register and tilemap state
#include <iostream>
#include <fstream>
#include <memory>
#include <iomanip>
#include "emulator/gba/GBA.h"
#include "emulator/gba/PPU.h"
#include "emulator/gba/GBAMemory.h"

using namespace AIO::Emulator::GBA;

int main() {
    auto gba = std::make_unique<GBA>();
    if (!gba->LoadROM("/Users/alexwaldmann/Desktop/AIO Server/OG-DK.gba")) {
        std::cerr << "Failed to load ROM\n";
        return 1;
    }
    
    // Run 200 frames
    constexpr int CYCLES_PER_FRAME = 280896;
    for (int f = 0; f < 200; ++f) {
        for (int c = 0; c < CYCLES_PER_FRAME; ) {
            c += gba->Step();
        }
    }
    
    // Dump display registers
    uint16_t dispcnt = gba->ReadMem16(0x04000000);
    uint16_t bg0cnt = gba->ReadMem16(0x04000008);
    
    std::cout << "=== Display Registers ===" << std::endl;
    std::cout << "DISPCNT: 0x" << std::hex << dispcnt << std::dec << std::endl;
    std::cout << "  Mode: " << (dispcnt & 7) << std::endl;
    std::cout << "  BG0 enabled: " << ((dispcnt >> 8) & 1) << std::endl;
    std::cout << "  BG1 enabled: " << ((dispcnt >> 9) & 1) << std::endl;
    std::cout << "  BG2 enabled: " << ((dispcnt >> 10) & 1) << std::endl;
    std::cout << "  BG3 enabled: " << ((dispcnt >> 11) & 1) << std::endl;
    
    int priority = bg0cnt & 3;
    int charBase = (bg0cnt >> 2) & 3;
    int screenBase = (bg0cnt >> 8) & 0x1F;
    int screenSize = (bg0cnt >> 14) & 3;
    bool is8bpp = (bg0cnt >> 7) & 1;
    
    std::cout << "\nBG0CNT: 0x" << std::hex << bg0cnt << std::dec << std::endl;
    std::cout << "  Priority: " << priority << std::endl;
    std::cout << "  CharBase: " << charBase << " (0x" << std::hex << (charBase * 0x4000) << std::dec << ")" << std::endl;
    std::cout << "  ScreenBase: " << screenBase << " (0x" << std::hex << (screenBase * 0x800) << std::dec << ")" << std::endl;
    std::cout << "  ScreenSize: " << screenSize << std::endl;
    std::cout << "  8bpp: " << is8bpp << std::endl;
    
    // Dump scroll registers
    uint16_t bg0hofs = gba->ReadMem16(0x04000010);
    uint16_t bg0vofs = gba->ReadMem16(0x04000012);
    std::cout << "\nBG0 Scroll: HOFS=" << (bg0hofs & 0x1FF) << " VOFS=" << (bg0vofs & 0x1FF) << std::endl;
    
    // Dump first row of tilemap entries
    uint32_t screenAddr = 0x06000000 + (screenBase * 0x800);
    std::cout << "\n=== First Row Tilemap (32 entries at 0x" << std::hex << screenAddr << std::dec << ") ===" << std::endl;
    for (int i = 0; i < 32; i++) {
        uint16_t entry = gba->ReadMem16(screenAddr + i * 2);
        int tileIdx = entry & 0x3FF;
        int palBank = (entry >> 12) & 0xF;
        if (entry != 0) {
            std::cout << "  [" << std::setw(2) << i << "] 0x" << std::hex << std::setw(4) 
                      << std::setfill('0') << entry << std::dec << std::setfill(' ')
                      << " tile=" << std::setw(3) << tileIdx << " pal=" << palBank << std::endl;
        }
    }
    
    // Check several rows
    std::cout << "\n=== Non-zero entries in first 20 rows ===" << std::endl;
    for (int row = 0; row < 20; row++) {
        for (int col = 0; col < 32; col++) {
            uint16_t entry = gba->ReadMem16(screenAddr + (row * 32 + col) * 2);
            if (entry != 0) {
                int tileIdx = entry & 0x3FF;
                int palBank = (entry >> 12) & 0xF;
                std::cout << "  row=" << std::setw(2) << row << " col=" << std::setw(2) << col
                          << " entry=0x" << std::hex << std::setw(4) << std::setfill('0') << entry
                          << std::dec << std::setfill(' ') << " tile=" << tileIdx << " pal=" << palBank << std::endl;
            }
        }
    }
    
    // Dump a few tiles from char base
    uint32_t charAddr = 0x06000000 + (charBase * 0x4000);
    std::cout << "\n=== Tile 0 data (at 0x" << std::hex << charAddr << std::dec << ") ===" << std::endl;
    std::cout << "  ";
    for (int b = 0; b < 32; b++) {
        uint8_t byte = gba->ReadMem(charAddr + b) & 0xFF;
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)byte << " ";
    }
    std::cout << std::dec << std::setfill(' ') << std::endl;
    
    return 0;
}
