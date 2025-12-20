#include <gtest/gtest.h>

#include "emulator/gba/ROMMetadataAnalyzer.h"

using namespace AIO::Emulator::GBA;

static std::vector<uint8_t> MakeMinimalRom(std::string title12, std::string gameCode4) {
    std::vector<uint8_t> rom(0x200, 0x00);

    // Title at 0xA0 (max 12 chars)
    if (title12.size() > 12) title12.resize(12);
    for (size_t i = 0; i < title12.size(); ++i) {
        rom[0xA0 + i] = static_cast<uint8_t>(title12[i]);
    }

    // Game code at 0xAC (4 bytes)
    if (gameCode4.size() < 4) {
        gameCode4.append(4 - gameCode4.size(), 'X');
    } else if (gameCode4.size() > 4) {
        gameCode4.resize(4);
    }
    for (size_t i = 0; i < 4; ++i) {
        rom[0xAC + i] = static_cast<uint8_t>(gameCode4[i]);
    }

    return rom;
}

TEST(ROMMetadataAnalyzerTest, DetectsEEPROM64KFromMarker) {
    auto rom = MakeMinimalRom("TESTTITLE", "AA2E");

    // Inject driver marker anywhere in ROM.
    const char* marker = "EEPROM_V";
    for (size_t i = 0; marker[i] != '\0'; ++i) {
        rom[0x150 + i] = static_cast<uint8_t>(marker[i]);
    }

    ROMMetadata meta = ROMMetadataAnalyzer::Analyze(rom);
    EXPECT_EQ(meta.gameCode, "AA2E");
    EXPECT_EQ(meta.saveType, SaveType::EEPROM_64K);
    EXPECT_EQ(meta.region, Region::NorthAmerica);
    EXPECT_EQ(meta.language, Language::English);
}

TEST(ROMMetadataAnalyzerTest, DetectsEEPROM4KFromEEPROMV111) {
    auto rom = MakeMinimalRom("TESTTITLE", "ABCD");

    const char* marker = "EEPROM_V111";
    for (size_t i = 0; marker[i] != '\0'; ++i) {
        rom[0x160 + i] = static_cast<uint8_t>(marker[i]);
    }

    ROMMetadata meta = ROMMetadataAnalyzer::Analyze(rom);
    EXPECT_EQ(meta.saveType, SaveType::EEPROM_4K);
}

TEST(ROMMetadataAnalyzerTest, DetectsFlash1MFromMarker) {
    auto rom = MakeMinimalRom("TESTTITLE", "BMBP");

    const char* marker = "FLASH1M_V";
    for (size_t i = 0; marker[i] != '\0'; ++i) {
        rom[0x170 + i] = static_cast<uint8_t>(marker[i]);
    }

    ROMMetadata meta = ROMMetadataAnalyzer::Analyze(rom);
    EXPECT_EQ(meta.saveType, SaveType::Flash1M);
    EXPECT_EQ(meta.region, Region::PAL);
}
