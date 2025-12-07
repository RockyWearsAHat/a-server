#include "emulator/gba/GBA.h"
#include <iostream>
#include <iomanip>

using namespace AIO::Emulator::GBA;

int main(int argc, char* argv[]) {
    const char* romPath = argc > 1 ? argv[1] : "DKC.gba";
    
    GBA gba;
    if (!gba.LoadROM(romPath)) {
        std::cerr << "Failed to load ROM: " << romPath << std::endl;
        return 1;
    }
    
    std::cout << "=== DKC DISPSTAT Debug ===" << std::endl;
    
    for (int frame = 0; frame < 200; frame++) {
        // Run a frame
        for (int i = 0; i < 280000; i++) {
            gba.Step();
        }
        
        // Check DISPSTAT after each frame
        uint16_t dispstat = gba.GetMemory().Read16(0x04000004);
        uint16_t ie = gba.GetMemory().Read16(0x04000200);
        uint16_t if_reg = gba.GetMemory().Read16(0x04000202);
        uint32_t vblankFlag = gba.GetMemory().Read32(0x03000064);
        
        if (frame < 10 || frame % 50 == 0) {
            std::cout << "Frame " << std::dec << frame 
                      << " DISPSTAT=0x" << std::hex << dispstat
                      << " IE=0x" << ie 
                      << " IF=0x" << if_reg
                      << " VBlankFlag=0x" << vblankFlag 
                      << " VBlankIRQEnable=" << ((dispstat & 0x8) ? "Y" : "N")
                      << " VCountMatch=" << std::dec << ((dispstat >> 8) & 0xFF)
                      << std::endl;
        }
    }
    
    return 0;
}
