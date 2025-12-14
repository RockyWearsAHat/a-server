#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <regex>
#include <emulator/gba/GameDB.h>

namespace AIO::Emulator::GBA {

    enum class Region {
        Unknown,
        Japan,
        NorthAmerica,
        PAL,
        Korea
    };

    enum class Language {
        Unknown,
        English,
        Japanese,
        French,
        German,
        Spanish,
        Italian,
        Dutch,
        Simplified_Chinese,
        Traditional_Chinese,
        Korean
    };

    struct ROMMetadata {
        std::string gameCode;           // 4-char code at 0xAC
        std::string gameTitle;          // Title at 0xA0-0xAB (12 bytes, null-terminated)
        Region region;                  // Detected from game code last char
        Language language;              // Primary language (inferred from region or detected)
        SaveType saveType;              // Detected from ROM strings
        uint32_t romSize;               // Size of ROM
        bool isSaveStateCompatible;     // Whether save format matches standard
    };

    class ROMMetadataAnalyzer {
    public:
        /**
         * Analyze ROM header and detect metadata
         * @param romData Complete ROM data buffer
         * @return Metadata struct with detected information
         */
        static ROMMetadata Analyze(const std::vector<uint8_t>& romData);

    private:
        // Parse game code to detect region (last char: E=US, P=PAL, J=Japan, etc)
        static Region DetectRegionFromGameCode(const std::string& gameCode);

        // Infer language from region
        static Language InferLanguageFromRegion(Region region);

        // Search for save type markers in ROM
        static SaveType DetectSaveType(const std::vector<uint8_t>& romData);

        // Search for SAVE function call patterns and detect actual save methodology
        static SaveType AnalyzeSaveBehavior(const std::vector<uint8_t>& romData);

        // Extract null-terminated game title
        static std::string ExtractGameTitle(const std::vector<uint8_t>& romData);

        // Helper: Search for string pattern in ROM
        static bool Contains(const std::vector<uint8_t>& romData, const std::string& pattern);

        // Helper: Search for regex pattern in ROM and extract matches
        static bool ContainsRegex(const std::vector<uint8_t>& romData, const std::string& regexPattern);
    };

}
