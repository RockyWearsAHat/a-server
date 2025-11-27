#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include "PPU.h"

namespace AIO::Emulator::GBA {

    class ARM7TDMI;
    class GBAMemory;

    class GBA {
    public:
        GBA();
        ~GBA();

        bool LoadROM(const std::string& path);
        void Reset();
        void Step(); // Run one instruction/cycle
        void UpdateInput(uint16_t keyState);
        void SaveGame();

        const PPU& GetPPU() const { return *ppu; }
        
        uint32_t ReadMem(uint32_t addr); // Debug helper
        uint32_t GetPC() const; // Debug helper
        uint32_t GetRegister(int reg) const; // Debug helper
        void PatchROM(uint32_t addr, uint8_t val); // Debug helper

    private:
        std::unique_ptr<ARM7TDMI> cpu;
        std::unique_ptr<GBAMemory> memory;
        std::unique_ptr<PPU> ppu;
        
        bool romLoaded = false;
        std::string savePath;
    };

}
