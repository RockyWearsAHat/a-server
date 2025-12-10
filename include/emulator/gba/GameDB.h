#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

namespace AIO::Emulator::GBA {

    enum class SaveType {
        Auto,
        None,
        SRAM,
        Flash512,
        Flash1M,
        EEPROM_4K,
        EEPROM_64K
    };

    struct GameOverride {
        std::string gameCode;
        std::string gameName;
        SaveType saveType = SaveType::Auto;
        std::vector<std::pair<uint32_t, uint32_t>> patches; // Address, Value
    };

    class GameDB {
    public:
        static GameOverride GetOverride(const std::string& gameCode);
        static SaveType DetectSaveType(const std::vector<uint8_t>& romData);
    
    private:
        static const std::unordered_map<std::string, GameOverride> overrides;
    };

}
