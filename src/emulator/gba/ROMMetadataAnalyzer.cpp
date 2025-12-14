#include <emulator/gba/ROMMetadataAnalyzer.h>
#include <iostream>
#include <algorithm>
#include <cctype>

namespace AIO::Emulator::GBA {

    ROMMetadata ROMMetadataAnalyzer::Analyze(const std::vector<uint8_t>& romData) {
        ROMMetadata metadata;
        metadata.romSize = romData.size();

        // Extract game code from 0xAC (4 bytes)
        if (romData.size() >= 0xB0) {
            metadata.gameCode = std::string(reinterpret_cast<const char*>(&romData[0xAC]), 4);
            std::cout << "[ROMAnalyzer] Game Code: " << metadata.gameCode << std::endl;
        }

        // Extract game title from 0xA0 (12 bytes)
        metadata.gameTitle = ExtractGameTitle(romData);
        std::cout << "[ROMAnalyzer] Game Title: " << metadata.gameTitle << std::endl;

        // Detect region from game code
        metadata.region = DetectRegionFromGameCode(metadata.gameCode);
        std::cout << "[ROMAnalyzer] Detected Region: ";
        switch (metadata.region) {
            case Region::Japan: std::cout << "Japan (J)"; break;
            case Region::NorthAmerica: std::cout << "North America (E)"; break;
            case Region::PAL: std::cout << "PAL/Europe (P)"; break;
            case Region::Korea: std::cout << "Korea (K)"; break;
            default: std::cout << "Unknown"; break;
        }
        std::cout << std::endl;

        // Infer language from region
        metadata.language = InferLanguageFromRegion(metadata.region);
        std::cout << "[ROMAnalyzer] Inferred Language: ";
        switch (metadata.language) {
            case Language::English: std::cout << "English"; break;
            case Language::Japanese: std::cout << "Japanese"; break;
            case Language::French: std::cout << "French"; break;
            case Language::German: std::cout << "German"; break;
            case Language::Spanish: std::cout << "Spanish"; break;
            case Language::Italian: std::cout << "Italian"; break;
            case Language::Dutch: std::cout << "Dutch"; break;
            default: std::cout << "Unknown"; break;
        }
        std::cout << std::endl;

        // Detect save type - first try analyzing actual save behavior patterns
        metadata.saveType = AnalyzeSaveBehavior(romData);
        if (metadata.saveType == SaveType::Auto) {
            // Fall back to string detection if behavior analysis didn't find anything
            metadata.saveType = DetectSaveType(romData);
        }

        std::cout << "[ROMAnalyzer] Detected Save Type: ";
        switch (metadata.saveType) {
            case SaveType::SRAM: std::cout << "SRAM"; break;
            case SaveType::Flash512: std::cout << "Flash 512K"; break;
            case SaveType::Flash1M: std::cout << "Flash 1M"; break;
            case SaveType::EEPROM_4K: std::cout << "EEPROM 4K"; break;
            case SaveType::EEPROM_64K: std::cout << "EEPROM 64K"; break;
            case SaveType::Auto: std::cout << "Auto-detect"; break;
            default: std::cout << "Unknown"; break;
        }
        std::cout << std::endl;

        metadata.isSaveStateCompatible = (metadata.saveType != SaveType::Auto);
        return metadata;
    }

    Region ROMMetadataAnalyzer::DetectRegionFromGameCode(const std::string& gameCode) {
        if (gameCode.length() < 4) {
            return Region::Unknown;
        }

        char regionChar = gameCode[3];
        switch (regionChar) {
            case 'E': return Region::NorthAmerica;  // Europe/US in GBA context (Nintendo used E for US)
            case 'P': return Region::PAL;           // PAL (Europe, Australia)
            case 'J': return Region::Japan;         // Japan
            case 'K': return Region::Korea;         // Korea
            default: return Region::Unknown;
        }
    }

    Language ROMMetadataAnalyzer::InferLanguageFromRegion(Region region) {
        switch (region) {
            case Region::Japan:         return Language::Japanese;
            case Region::NorthAmerica:  return Language::English;
            case Region::PAL:           return Language::English;  // Most PAL games primary lang is English
            case Region::Korea:         return Language::Korean;
            default:                    return Language::Unknown;
        }
    }

    std::string ROMMetadataAnalyzer::ExtractGameTitle(const std::vector<uint8_t>& romData) {
        if (romData.size() < 0xAC) {
            return "";
        }

        // Game title is at 0xA0, up to 12 bytes (may include null terminator)
        std::string title;
        for (int i = 0; i < 12; ++i) {
            char c = static_cast<char>(romData[0xA0 + i]);
            if (c == 0 || !std::isprint(c)) break;
            title += c;
        }

        // Trim trailing whitespace
        while (!title.empty() && std::isspace(title.back())) {
            title.pop_back();
        }

        return title;
    }

    SaveType ROMMetadataAnalyzer::DetectSaveType(const std::vector<uint8_t>& romData) {
        // Search for known save type identifiers in ROM

        // Order matters: check for more specific patterns first
        if (Contains(romData, "EEPROM_V111")) {
            std::cout << "[ROMAnalyzer] Found EEPROM_V111 marker (4K)" << std::endl;
            return SaveType::EEPROM_4K;
        }
        if (Contains(romData, "EEPROM_V")) {
            std::cout << "[ROMAnalyzer] Found EEPROM_V marker (64K default)" << std::endl;
            return SaveType::EEPROM_64K;
        }
        if (Contains(romData, "SRAM_V")) {
            std::cout << "[ROMAnalyzer] Found SRAM_V marker" << std::endl;
            return SaveType::SRAM;
        }
        if (Contains(romData, "FLASH1M_V")) {
            std::cout << "[ROMAnalyzer] Found FLASH1M_V marker" << std::endl;
            return SaveType::Flash1M;
        }
        if (Contains(romData, "FLASH512_V")) {
            std::cout << "[ROMAnalyzer] Found FLASH512_V marker" << std::endl;
            return SaveType::Flash512;
        }
        if (Contains(romData, "FLASH_V")) {
            std::cout << "[ROMAnalyzer] Found FLASH_V marker (512K default)" << std::endl;
            return SaveType::Flash512;
        }

        return SaveType::Auto;
    }

    SaveType ROMMetadataAnalyzer::AnalyzeSaveBehavior(const std::vector<uint8_t>& romData) {
        // Analyze actual save behavior patterns by looking for:
        // 1. Calls to save functions (BIOS SWI or custom)
        // 2. Writes to save memory region (0x0E000000+)
        // 3. Patterns that indicate save methodology

        // Strategy: String detection is more reliable than pattern analysis
        // The driver code strings are inserted into ROM by devkit and are reliable markers
        // Only fall back to pattern analysis if string detection fails
        
        // For now, return Auto to let string detection handle it
        return SaveType::Auto;
    }

    bool ROMMetadataAnalyzer::Contains(const std::vector<uint8_t>& romData, const std::string& pattern) {
        if (pattern.length() > romData.size()) return false;
        auto it = std::search(romData.begin(), romData.end(), pattern.begin(), pattern.end());
        return it != romData.end();
    }

    bool ROMMetadataAnalyzer::ContainsRegex(const std::vector<uint8_t>& romData, const std::string& regexPattern) {
        // Convert ROM data to string for regex matching
        std::string romStr(romData.begin(), romData.end());
        std::regex pattern(regexPattern);
        return std::regex_search(romStr, pattern);
    }

}
