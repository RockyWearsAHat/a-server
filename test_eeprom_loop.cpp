#include <emulator/gba/GBA.h>
#include <iostream>
#include <iomanip>

using namespace AIO::Emulator::GBA;

int main() {
    GBA gba;
    
    // Load SMA2 ROM
    if (!gba.LoadROM("SMA2.gba")) {
        std::cerr << "Failed to load SMA2.gba" << std::endl;
        return 1;
    }
    
    // Step through boot sequence, stopping at EEPROM loop
    int step_count = 0;
    const int MAX_STEPS = 10000000; // Run for max 10M steps
    
    while (step_count < MAX_STEPS) {
        uint32_t pc = gba.GetCPU().GetRegister(15);
        
        // Check if we're in the EEPROM validation loop range
        if (pc >= 0x0809E1CC && pc <= 0x0809E1EA) {
            std::cout << "[Step " << step_count << "] PC=0x" << std::hex << pc << std::dec << std::endl;
            
            // Show first 20 iterations of loop
            static int loop_count = 0;
            if (++loop_count > 50) {
                std::cout << "Loop count exceeded, exiting" << std::endl;
                break;
            }
        }
        
        gba.Step();
        step_count++;
        
        // Safety check: exit if we hit a very high step count
        if (step_count % 100000 == 0) {
            std::cout << "[Step " << step_count << "] PC=0x" << std::hex << gba.GetCPU().GetRegister(15) << std::dec << std::endl;
        }
    }
    
    std::cout << "Test complete. Total steps: " << step_count << std::endl;
    return 0;
}
