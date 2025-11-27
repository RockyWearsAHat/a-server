#include <iostream>
#include <iomanip>
#include <emulator/gba/GBA.h>

void DumpCode(AIO::Emulator::GBA::GBA& gba, uint32_t startAddr, uint32_t size) {
    for (uint32_t addr = startAddr; addr < startAddr + size; addr += 4) {
        uint32_t val = gba.ReadMem(addr);
        std::cout << "0x" << std::hex << addr << ": 0x" << val << std::endl;
    }
}

int main(int argc, char** argv) {
    AIO::Emulator::GBA::GBA gba;
    std::string romPath = "/Users/alexwaldmann/Desktop/AIO Server/SMA2.gba";
    if (argc > 1) {
        romPath = argv[1];
    }
    
    std::cout << "Loading ROM: " << romPath << std::endl;
    // Load ROM
    if (!gba.LoadROM(romPath)) {
        std::cerr << "Failed to load ROM: " << romPath << std::endl;
        return 1;
    }

    std::cout << "Starting Headless Execution..." << std::endl;
    std::cout << "Resetting..." << std::endl;
    gba.Reset();

    // Patch the ROM to bypass the integrity check
    // Function at 0x080015d8
    // Replace PUSH {R4-R6, LR} with MOV R0, #1; BX LR
    std::cout << "Patching Function at 0x80015d8 to Return Success (1)..." << std::endl;
    gba.PatchROM(0x080015d8, 0x01); // MOV R0, #1
    gba.PatchROM(0x080015d9, 0x20); 
    gba.PatchROM(0x080015da, 0x70); // BX LR
    gba.PatchROM(0x080015db, 0x47);

    std::cout << "Running for 20,000,000 cycles..." << std::endl;
    for (int i = 0; i < 20000000; ++i) {
        gba.Step();
    }

    std::cout << "Finished 20,000,000 cycles without crashing." << std::endl;
    return 0;
}
