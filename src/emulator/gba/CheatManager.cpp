#include <emulator/gba/CheatManager.h>
#include <emulator/gba/GBAMemory.h>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace AIO::Emulator::GBA {

    CheatManager::CheatManager(GBAMemory* memory) : memory(memory) {}
    CheatManager::~CheatManager() {}

    void CheatManager::AddCheat(const std::string& description, const std::string& code) {
        Cheat cheat;
        cheat.description = description;
        cheat.code = code;
        cheat.enabled = true;
        ParseCheat(cheat);
        cheats.push_back(cheat);
    }

    void CheatManager::RemoveCheat(int index) {
        if (index >= 0 && index < cheats.size()) {
            cheats.erase(cheats.begin() + index);
        }
    }

    void CheatManager::ToggleCheat(int index, bool enabled) {
        if (index >= 0 && index < cheats.size()) {
            cheats[index].enabled = enabled;
        }
    }

    void CheatManager::ClearCheats() {
        cheats.clear();
    }

    const std::vector<Cheat>& CheatManager::GetCheats() const {
        return cheats;
    }

    void CheatManager::ApplyCheats() {
        for (const auto& cheat : cheats) {
            if (!cheat.enabled) continue;

            for (const auto& entry : cheat.entries) {
                switch (entry.type) {
                    case 0: // 8-bit Write
                        memory->Write8(entry.address, entry.value & 0xFF);
                        break;
                    case 1: // 16-bit Write
                        memory->Write16(entry.address, entry.value & 0xFFFF);
                        break;
                    case 2: // 32-bit Write
                        memory->Write32(entry.address, entry.value);
                        break;
                    // TODO: Conditionals
                }
            }
        }
    }

    void CheatManager::ParseCheat(Cheat& cheat) {
        cheat.entries.clear();
        std::stringstream ss(cheat.code);
        std::string line;
        
        while (std::getline(ss, line)) {
            // Remove whitespace and convert to uppercase
            std::string cleanLine;
            for (char c : line) {
                if (isalnum(c)) cleanLine += toupper(c);
            }

            if (cleanLine.length() < 8) continue;

            // Try to detect format
            // Standard GBA codes are usually 12-16 hex digits (8 bytes)
            // Format: XXXXXXXX YYYYYYYY
            
            if (cleanLine.length() == 16) {
                uint32_t part1 = std::stoul(cleanLine.substr(0, 8), nullptr, 16);
                uint32_t part2 = std::stoul(cleanLine.substr(8, 8), nullptr, 16);

                // CodeBreaker / GameShark v3 (Unencrypted)
                uint8_t type = (part1 >> 28) & 0xF;
                uint32_t address = part1 & 0x0FFFFFFF;
                
                // Basic CodeBreaker Types
                if (type == 0x3) { // 8-bit write
                    cheat.entries.push_back({address, part2 & 0xFF, 0});
                } else if (type == 0x8) { // 16-bit write
                    cheat.entries.push_back({address, part2 & 0xFFFF, 1});
                } else if (type == 0x0) { // 32-bit write (GameShark)
                    // GameShark v3: 0XXXXXXX YYYYYYYY = 32-bit write to 02XXXXXX?
                    // Usually address is 02XXXXXX.
                    // If address is small (offset), add base?
                    // Let's assume full address for now if it looks like a pointer (02/03)
                    if ((address >> 24) == 0) address |= 0x02000000;
                    cheat.entries.push_back({address, part2, 2});
                } else if (type == 0x1) { // 16-bit write (GameShark)
                    if ((address >> 24) == 0) address |= 0x02000000;
                    cheat.entries.push_back({address, part2 & 0xFFFF, 1});
                } else if (type == 0x2) { // 8-bit write (GameShark)
                    if ((address >> 24) == 0) address |= 0x02000000;
                    cheat.entries.push_back({address, part2 & 0xFF, 0});
                }
                // Raw Address Write (if it looks like a valid RAM address)
                else if ((part1 >> 24) == 0x02 || (part1 >> 24) == 0x03) {
                    // Assume 32-bit write if not specified? Or 16?
                    // Let's guess based on alignment
                    if ((part1 & 3) == 0) {
                        cheat.entries.push_back({part1, part2, 2});
                    } else if ((part1 & 1) == 0) {
                        cheat.entries.push_back({part1, part2 & 0xFFFF, 1});
                    } else {
                        cheat.entries.push_back({part1, part2 & 0xFF, 0});
                    }
                }
            }
            // CodeBreaker 12-digit format? XXXXXXXX YYYY
            else if (cleanLine.length() == 12) {
                uint32_t part1 = std::stoul(cleanLine.substr(0, 8), nullptr, 16);
                uint32_t part2 = std::stoul(cleanLine.substr(8, 4), nullptr, 16);
                
                uint8_t type = (part1 >> 28) & 0xF;
                uint32_t address = part1 & 0x0FFFFFFF;

                if (type == 0x3) { // 8-bit
                    cheat.entries.push_back({address, part2 & 0xFF, 0});
                } else if (type == 0x8) { // 16-bit
                    cheat.entries.push_back({address, part2 & 0xFFFF, 1});
                }
            }
        }
    }

}
