#include "emulator/gba/GBA.h"
#include <iostream>

using namespace AIO::Emulator::GBA;

int main() {
    std::cout << "Testing GBA ROM Load..." << std::endl;
    
    GBA gba;
    bool success = gba.LoadROM("SMA2.gba");
    
    if (success) {
        std::cout << "ROM loaded successfully!" << std::endl;
        
        // Run a few steps to trigger save loading
        for (int i = 0; i < 10; i++) {
            gba.Step();
        }
    } else {
        std::cerr << "Failed to load ROM!" << std::endl;
        return 1;
    }
    
    return 0;
}
