#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <memory>

namespace AIO::Emulator::GBA {

    class GBAMemory;

    struct CheatEntry {
        uint32_t address;
        uint32_t value;
        int type; // Internal type ID
    };

    struct Cheat {
        std::string description;
        std::string code;
        bool enabled;
        std::vector<CheatEntry> entries;
    };

    class CheatManager {
    public:
        CheatManager(GBAMemory* memory);
        ~CheatManager();

        void AddCheat(const std::string& description, const std::string& code);
        void RemoveCheat(int index);
        void ToggleCheat(int index, bool enabled);
        void ClearCheats();
        
        const std::vector<Cheat>& GetCheats() const;
        
        void ApplyCheats();

    private:
        GBAMemory* memory;
        std::vector<Cheat> cheats;

        void ParseCheat(Cheat& cheat);
        void ParseGameShark(Cheat& cheat, const std::string& line);
        void ParseCodeBreaker(Cheat& cheat, const std::string& line);
    };

}
