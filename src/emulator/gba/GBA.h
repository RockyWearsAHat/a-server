#pragma once
#include <string>
#include <memory>
#include <vector>
#include <cstdint>

namespace AIO::Emulator::GBA {

    class GBAMemory;
    class ARM7TDMI;
    class PPU;

    class GBA {
    public:
        GBA();
        ~GBA();

        bool LoadROM(const std::string& path);
        void Reset();
        void Step();
        void UpdateInput(uint16_t keyState);
        
        uint32_t ReadMem(uint32_t addr);
        uint16_t ReadMem16(uint32_t addr);
        uint32_t ReadMem32(uint32_t addr);
        void WriteMem(uint32_t addr, uint32_t val);
        uint32_t GetPC() const;
        bool IsThumbMode() const;
        uint32_t GetRegister(int reg) const;
        void PatchROM(uint32_t addr, uint32_t val);

        const PPU* GetPPU() const { return ppu.get(); }

    private:
        void SaveGame();

        std::unique_ptr<GBAMemory> memory;
        std::unique_ptr<ARM7TDMI> cpu;
        std::unique_ptr<PPU> ppu;
        
        bool romLoaded = false;
        std::string savePath;
    };

}