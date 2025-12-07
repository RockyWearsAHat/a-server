#include "emulator/gba/GBA.h"
#include "emulator/gba/ARM7TDMI.h"
#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/APU.h"
#include "emulator/gba/GameDB.h"
#include <iostream>
#include <fstream>
#include <vector>

namespace AIO::Emulator::GBA {

    GBA::GBA() {
        memory = std::make_unique<GBAMemory>();
        cpu = std::make_unique<ARM7TDMI>(*memory);
        ppu = std::make_unique<PPU>(*memory);
        apu = std::make_unique<APU>(*memory);
        
        // Wire up APU to memory for timer overflow callbacks
        memory->SetAPU(apu.get());
        // Wire up PPU to memory for DMA updates
        memory->SetPPU(ppu.get());
        // Wire up CPU to memory for debug
        memory->SetCPU(cpu.get());
    }

    GBA::~GBA() {
        SaveGame();
    }

    bool GBA::LoadROM(const std::string& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "Failed to open ROM file: " << path << std::endl;
            return false;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(size);
        if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            memory->LoadGamePak(buffer);
            romLoaded = true;

            // Apply GameDB Patches
            if (buffer.size() >= 0xB0) {
                std::string gameCode(reinterpret_cast<const char*>(&buffer[0xAC]), 4);
                std::cout << "[LoadROM] Detected GameCode: " << gameCode << std::endl;
                GameOverride override = GameDB::GetOverride(gameCode);
                if (!override.patches.empty()) {
                    std::cout << "[LoadROM] Found " << override.patches.size() << " patches." << std::endl;
                    for (const auto& patch : override.patches) {
                        PatchROM(patch.first, patch.second);
                    }
                } else {
                    std::cout << "[LoadROM] No patches found for " << gameCode << std::endl;
                }
            }

            // Load Save
            size_t lastDot = path.find_last_of('.');
            if (lastDot != std::string::npos) {
                savePath = path.substr(0, lastDot) + ".sav";
            } else {
                savePath = path + ".sav";
            }

            std::ifstream saveFile(savePath, std::ios::binary | std::ios::ate);
            if (saveFile.is_open()) {
                std::streamsize saveSize = saveFile.tellg();
                saveFile.seekg(0, std::ios::beg);
                std::vector<uint8_t> saveData(saveSize);
                if (saveFile.read(reinterpret_cast<char*>(saveData.data()), saveSize)) {
                    memory->LoadSave(saveData);
                } else {
                    // Failed to read save file, use default initialization
                    memory->LoadSave(std::vector<uint8_t>());
                }
            } else {
                // Save file doesn't exist - this is normal on first run
                // Call LoadSave with empty data to ensure proper initialization
                memory->LoadSave(std::vector<uint8_t>());
            }
            
            // Give memory the save path for auto-flush
            memory->SetSavePath(savePath);

            Reset();
            return true;
        }

        std::cerr << "Failed to read ROM file: " << path << std::endl;
        return false;
    }

    void GBA::SaveGame() {
        if (savePath.empty() || !memory) return;
        
        std::vector<uint8_t> data = memory->GetSaveData();
        if (data.empty()) return;

        std::ofstream file(savePath, std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(data.data()), data.size());
        }
    }

    void GBA::Reset() {
        cpu->Reset();
        memory->Reset();
        apu->Reset();
        // ppu->Reset(); // TODO: Implement PPU Reset
    }

    int GBA::Step() {
        if (!romLoaded) return 0;
        
        cpu->Step();
        int dmaCycles = memory->GetLastDMACycles();
        
        // DMA cycles were already applied to APU/PPU/Timers inside PerformDMA
        // When CPU is halted, simulate fast-forward through time until an interrupt might fire
        // A scanline takes ~1232 cycles, so advance by that much when halted
        int cpuCycles = cpu->IsHalted() ? 1232 : 4;
        
        ppu->Update(cpuCycles); 
        memory->UpdateTimers(cpuCycles);
        apu->Update(cpuCycles); 
        
        return cpuCycles + dmaCycles;
    }

    bool GBA::IsCPUHalted() const {
        return cpu && cpu->IsHalted();
    }

    void GBA::UpdateInput(uint16_t keyState) {
        if (memory) {
            memory->SetKeyInput(keyState);
        }
    }

    uint32_t GBA::ReadMem(uint32_t addr) {
        if (memory) return memory->Read32(addr);
        return 0;
    }

    uint16_t GBA::ReadMem16(uint32_t addr) {
        if (memory) return memory->Read16(addr);
        return 0;
    }

    uint32_t GBA::ReadMem32(uint32_t addr) {
        if (memory) return memory->Read32(addr);
        return 0;
    }

    void GBA::WriteMem(uint32_t addr, uint32_t val) {
        if (memory) memory->Write32(addr, val);
    }

    uint32_t GBA::GetPC() const {
        if (cpu) return cpu->GetRegister(15);
        return 0;
    }

    bool GBA::IsThumbMode() const {
        if (cpu) return (cpu->GetCPSR() & 0x20) != 0;
        return false;
    }

    uint32_t GBA::GetRegister(int reg) const {
        return cpu->GetRegister(reg);
    }

    uint32_t GBA::GetCPSR() const {
        if (cpu) return cpu->GetCPSR();
        return 0;
    }

    void GBA::PatchROM(uint32_t addr, uint32_t val) {
        std::cout << "[PatchROM] Addr=" << std::hex << addr << " Val=" << val << std::dec << std::endl;
        memory->WriteROM32(addr, val);
    }

}
