#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include "PPU.h"
#include "APU.h"
#include "GBAMemory.h"
#include "ROMMetadataAnalyzer.h"

namespace AIO::Emulator::GBA {

    class ARM7TDMI;

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
        GBAMemory& GetMemory() { return *memory; }
        const GBAMemory& GetMemory() const { return *memory; }
        
        uint32_t ReadMem(uint32_t addr); // Debug helper
        uint16_t ReadMem16(uint32_t addr); // Debug helper
        uint32_t ReadMem32(uint32_t addr); // Debug helper
        void WriteMem(uint32_t addr, uint32_t val); // Debug helper
        void WriteMem16(uint32_t addr, uint16_t val); // Debug helper
        uint32_t GetPC() const; // Debug helper
        bool IsThumbMode() const; // Debug helper
        uint32_t GetRegister(int reg) const; // Debug helper
        void SetRegister(int reg, uint32_t val); // Debug helper
        uint32_t GetCPSR() const; // Debug helper
        void PatchROM(uint32_t addr, uint32_t val);

        // Debugger controls (forwarded to ARM7TDMI)
        void AddBreakpoint(uint32_t addr);
        void ClearBreakpoints();
        void SetSingleStep(bool enabled);
        bool IsHalted() const; // CPU halted or debugger break
        void Continue();
        void DumpCPUState(std::ostream& os) const;
        void StepBack();


    private:
        std::unique_ptr<ARM7TDMI> cpu;
        std::unique_ptr<GBAMemory> memory;
        std::unique_ptr<PPU> ppu;
        std::unique_ptr<APU> apu;
        
        bool romLoaded = false;
        std::string savePath;
        ROMMetadata romMetadata;

        // Configure boot state based on intelligently detected ROM metadata
        void ConfigureBootStateFromMetadata(const ROMMetadata& metadata);
        
        // Apply game-specific ROM patches for known compatibility issues
        void ApplyROMPatches(const ROMMetadata& metadata);

        // PC stall detection (treat long stalls as crash-equivalent)
        uint32_t lastPcForStall = 0;
        uint64_t stallCycleAccumulator = 0;
        bool stallCrashTriggered = false;
        static constexpr uint64_t STALL_CYCLE_THRESHOLD = 167800000ULL; // ~10s @16.78MHz
    };

}
