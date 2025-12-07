#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include "PPU.h"
#include "APU.h"

namespace AIO::Emulator::GBA {

    class ARM7TDMI;
    class GBAMemory;

    class GBA {
    public:
        GBA();
        ~GBA();

        bool LoadROM(const std::string& path);
        void Reset();
        int Step(); // Run one instruction/cycle, returns cycles consumed
        void UpdateInput(uint16_t keyState);
        void SaveGame();
        bool IsCPUHalted() const; // Check if CPU is waiting for interrupt

        const PPU& GetPPU() const { return *ppu; }
        APU& GetAPU() { return *apu; }
        
        uint32_t ReadMem(uint32_t addr); // Debug helper
        uint16_t ReadMem16(uint32_t addr); // Debug helper
        uint32_t ReadMem32(uint32_t addr); // Debug helper
        void WriteMem(uint32_t addr, uint32_t val); // Debug helper
        uint32_t GetPC() const; // Debug helper
        bool IsThumbMode() const; // Debug helper
        uint32_t GetRegister(int reg) const; // Debug helper
        void SetRegister(int reg, uint32_t val); // Debug helper
        uint32_t GetCPSR() const; // Debug helper
        void PatchROM(uint32_t addr, uint32_t val);


    private:
        std::unique_ptr<ARM7TDMI> cpu;
        std::unique_ptr<GBAMemory> memory;
        std::unique_ptr<PPU> ppu;
        std::unique_ptr<APU> apu;
        
        bool romLoaded = false;
        std::string savePath;
    };

}
