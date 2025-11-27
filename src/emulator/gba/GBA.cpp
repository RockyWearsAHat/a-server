#include "emulator/gba/GBA.h"
#include "emulator/gba/ARM7TDMI.h"
#include "emulator/gba/GBAMemory.h"
#include <iostream>
#include <fstream>
#include <vector>

namespace AIO::Emulator::GBA {

    GBA::GBA() {
        memory = std::make_unique<GBAMemory>();
        cpu = std::make_unique<ARM7TDMI>(*memory);
        ppu = std::make_unique<PPU>(*memory);
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
            std::cout << "Loaded ROM: " << path << " (" << size << " bytes)" << std::endl;
            romLoaded = true;

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
                    std::cout << "Loaded Save: " << savePath << std::endl;
                }
            }

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
        // ppu->Reset(); // TODO: Implement PPU Reset
    }

    void GBA::Step() {
        if (!romLoaded) return;
        
        cpu->Step();
        ppu->Update(4); // Assume 4 cycles per instruction for now
        memory->UpdateTimers(4);
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

    uint32_t GBA::GetPC() const {
        if (cpu) return cpu->GetRegister(15);
        return 0;
    }

    uint32_t GBA::GetRegister(int reg) const {
        return cpu->GetRegister(reg);
    }

    void GBA::PatchROM(uint32_t addr, uint8_t val) {
        memory->WriteROM(addr, val);
    }

}
