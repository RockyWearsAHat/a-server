#include <iostream>
#include <chrono>
#include <cstring>
#include <fstream>
#include "../include/emulator/gba/GBA.h"

using namespace AIO::Emulator::GBA;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <ROM_file>" << std::endl;
        return 1;
    }

    // Load ROM
    std::string romPath = argv[1];
    std::ifstream romFile(romPath, std::ios::binary);
    if (!romFile) {
        std::cerr << "Failed to open ROM: " << romPath << std::endl;
        return 1;
    }
    
    romFile.seekg(0, std::ios::end);
    size_t romSize = romFile.tellg();
    romFile.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> romData(romSize);
    romFile.read((char*)romData.data(), romSize);
    romFile.close();

    // Create GBA emulator
    GBA gba;
    gba.LoadROM(romData);
    gba.Reset();

    std::cout << "=== GBA Boot Test ===" << std::endl;
    std::cout << "ROM Size: " << romSize << " bytes" << std::endl;
    std::cout << "Running 60 frames (1 second of emulated time)..." << std::endl;

    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Run 60 frames and measure actual wall-clock time
    const int TARGET_FRAMES = 60;
    const int CYCLES_PER_FRAME = 280000;
    
    for (int frame = 0; frame < TARGET_FRAMES; frame++) {
        int totalCycles = 0;
        while (totalCycles < CYCLES_PER_FRAME && !gba.IsCPUHalted()) {
            totalCycles += gba.Step();
        }
        
        if (frame % 10 == 0) {
            uint32_t pc = gba.GetPC();
            std::cout << "Frame " << frame << ": PC=0x" << std::hex << pc << std::dec << std::endl;
        }
        
        if (gba.IsCPUHalted()) {
            std::cout << "CPU halted at frame " << frame << " PC=0x" << std::hex << gba.GetPC() << std::dec << std::endl;
            break;
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    
    std::cout << "\nElapsed time: " << elapsedMs << "ms for " << TARGET_FRAMES << " frames" << std::endl;
    std::cout << "Average per frame: " << (elapsedMs / TARGET_FRAMES) << "ms" << std::endl;
    std::cout << "Final PC: 0x" << std::hex << gba.GetPC() << std::dec << std::endl;
    std::cout << "Final CPSR: 0x" << std::hex << gba.GetCPSR() << std::dec << std::endl;
    
    return 0;
}
