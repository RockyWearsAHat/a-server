#include <gtest/gtest.h>

#include "emulator/gba/ROMMetadataAnalyzer.h"

using namespace AIO::Emulator::GBA;

static std::vector<uint8_t> MakeMinimalRom(std::string title12,
                                           std::string gameCode4) {
  std::vector<uint8_t> rom(0x200, 0x00);

  // Title at 0xA0 (max 12 chars)
  if (title12.size() > 12)
    title12.resize(12);
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
  const char *marker = "EEPROM_V";
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

  const char *marker = "EEPROM_V111";
  for (size_t i = 0; marker[i] != '\0'; ++i) {
    rom[0x160 + i] = static_cast<uint8_t>(marker[i]);
  }

  ROMMetadata meta = ROMMetadataAnalyzer::Analyze(rom);
  EXPECT_EQ(meta.saveType, SaveType::EEPROM_4K);
}

TEST(ROMMetadataAnalyzerTest, DetectsFlash1MFromMarker) {
  auto rom = MakeMinimalRom("TESTTITLE", "BMBP");

  const char *marker = "FLASH1M_V";
  for (size_t i = 0; marker[i] != '\0'; ++i) {
    rom[0x170 + i] = static_cast<uint8_t>(marker[i]);
  }

  ROMMetadata meta = ROMMetadataAnalyzer::Analyze(rom);
  EXPECT_EQ(meta.saveType, SaveType::Flash1M);
  EXPECT_EQ(meta.region, Region::PAL);
}

// ============================================================================
// Documentation-Driven Tests (ROM_Metadata_Analyzer.md spec)
// ============================================================================
// Per docs: "Detect save type by searching for driver strings"
//   - SRAM_V → SRAM
//   - FLASH512_V or FLASH_V → Flash 512K

TEST(ROMMetadataAnalyzerTest, DetectsSRAMFromMarker) {
  // Spec: "SRAM_V → SRAM"
  auto rom = MakeMinimalRom("SRAM GAME", "ABCE");

  const char *marker = "SRAM_V";
  for (size_t i = 0; marker[i] != '\0'; ++i) {
    rom[0x180 + i] = static_cast<uint8_t>(marker[i]);
  }

  ROMMetadata meta = ROMMetadataAnalyzer::Analyze(rom);
  EXPECT_EQ(meta.saveType, SaveType::SRAM);
}

TEST(ROMMetadataAnalyzerTest, DetectsFlash512FromFlash512VMarker) {
  // Spec: "FLASH512_V → Flash 512K"
  auto rom = MakeMinimalRom("FLASH GAME", "XYZP");

  const char *marker = "FLASH512_V";
  for (size_t i = 0; marker[i] != '\0'; ++i) {
    rom[0x190 + i] = static_cast<uint8_t>(marker[i]);
  }

  ROMMetadata meta = ROMMetadataAnalyzer::Analyze(rom);
  EXPECT_EQ(meta.saveType, SaveType::Flash512);
}

TEST(ROMMetadataAnalyzerTest, DetectsFlash512FromFlashVMarker) {
  // Spec: "FLASH_V → Flash 512K (default)"
  auto rom = MakeMinimalRom("FLASH GAME2", "XYZJ");

  const char *marker = "FLASH_V";
  for (size_t i = 0; marker[i] != '\0'; ++i) {
    rom[0x1A0 + i] = static_cast<uint8_t>(marker[i]);
  }

  ROMMetadata meta = ROMMetadataAnalyzer::Analyze(rom);
  EXPECT_EQ(meta.saveType, SaveType::Flash512);
  EXPECT_EQ(meta.region, Region::Japan);
}

// ============================================================================
// Region Detection (per GBATEK/ROM_Metadata_Analyzer.md)
// ============================================================================
// Game code suffix: E=US, P=PAL, J=Japan, K=Korea

TEST(ROMMetadataAnalyzerTest, DetectsJapanRegion) {
  // Spec: "J → Japan"
  auto rom = MakeMinimalRom("JAPANESE", "ABCJ");
  ROMMetadata meta = ROMMetadataAnalyzer::Analyze(rom);
  EXPECT_EQ(meta.region, Region::Japan);
  EXPECT_EQ(meta.language, Language::Japanese);
}

TEST(ROMMetadataAnalyzerTest, DetectsKoreaRegion) {
  // Spec: "K → Korea"
  auto rom = MakeMinimalRom("KOREAN", "ABCK");
  ROMMetadata meta = ROMMetadataAnalyzer::Analyze(rom);
  EXPECT_EQ(meta.region, Region::Korea);
  EXPECT_EQ(meta.language, Language::Korean);
}

TEST(ROMMetadataAnalyzerTest, UnknownRegionCodeReturnsUnknown) {
  // Edge case: unknown region suffix
  auto rom = MakeMinimalRom("UNKNOWN", "ABCZ");
  ROMMetadata meta = ROMMetadataAnalyzer::Analyze(rom);
  EXPECT_EQ(meta.region, Region::Unknown);
  EXPECT_EQ(meta.language, Language::Unknown);
}

// ============================================================================
// ROM Header Parsing (GBATEK: 0xA0-0xAB = title, 0xAC-0xAF = game code)
// ============================================================================

TEST(ROMMetadataAnalyzerTest, ExtractsGameTitleUpTo12Chars) {
  // Spec: "Title at 0xA0-0xAB (12 bytes, null-terminated)"
  auto rom = MakeMinimalRom("EXACTLY12CHR", "TEST");
  ROMMetadata meta = ROMMetadataAnalyzer::Analyze(rom);
  EXPECT_EQ(meta.gameTitle, "EXACTLY12CHR");
}

TEST(ROMMetadataAnalyzerTest, TrimsTrailingWhitespaceFromTitle) {
  // Per implementation: trims trailing whitespace
  auto rom = MakeMinimalRom("TRIMMED   ", "TEST");
  ROMMetadata meta = ROMMetadataAnalyzer::Analyze(rom);
  EXPECT_EQ(meta.gameTitle, "TRIMMED");
}

TEST(ROMMetadataAnalyzerTest, StopsAtNullInTitle) {
  // Title may contain null terminator before 12 chars
  std::vector<uint8_t> rom(0x200, 0x00);
  rom[0xA0] = 'S';
  rom[0xA1] = 'H';
  rom[0xA2] = 'O';
  rom[0xA3] = 'R';
  rom[0xA4] = 'T';
  rom[0xA5] = 0x00; // null terminator
  rom[0xAC] = 'T';
  rom[0xAD] = 'E';
  rom[0xAE] = 'S';
  rom[0xAF] = 'T';

  ROMMetadata meta = ROMMetadataAnalyzer::Analyze(rom);
  EXPECT_EQ(meta.gameTitle, "SHORT");
}

TEST(ROMMetadataAnalyzerTest, RomSizeReportedCorrectly) {
  // Spec: "romSize: ROM size in bytes"
  auto rom = MakeMinimalRom("TEST", "ABCE");
  ROMMetadata meta = ROMMetadataAnalyzer::Analyze(rom);
  EXPECT_EQ(meta.romSize, rom.size());
}

TEST(ROMMetadataAnalyzerTest, NoSaveMarkerReturnsAuto) {
  // When no save marker found, returns Auto for dynamic detection
  auto rom = MakeMinimalRom("NOSAVE", "ABCE");
  ROMMetadata meta = ROMMetadataAnalyzer::Analyze(rom);
  EXPECT_EQ(meta.saveType, SaveType::Auto);
  EXPECT_FALSE(meta.isSaveStateCompatible);
}

TEST(ROMMetadataAnalyzerTest, SaveMarkerFoundMakesSaveStateCompatible) {
  // Spec: "isSaveStateCompatible: Whether save format is standard"
  auto rom = MakeMinimalRom("SAVEGAME", "ABCE");
  const char *marker = "EEPROM_V";
  for (size_t i = 0; marker[i] != '\0'; ++i) {
    rom[0x150 + i] = static_cast<uint8_t>(marker[i]);
  }

  ROMMetadata meta = ROMMetadataAnalyzer::Analyze(rom);
  EXPECT_TRUE(meta.isSaveStateCompatible);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(ROMMetadataAnalyzerTest, TooSmallRomReturnsEmptyMetadata) {
  // Edge case: ROM smaller than header area
  std::vector<uint8_t> tinyRom(0x50, 0x00); // Less than 0xB0
  ROMMetadata meta = ROMMetadataAnalyzer::Analyze(tinyRom);
  EXPECT_TRUE(meta.gameCode.empty() || meta.gameCode == "\0\0\0\0");
  EXPECT_TRUE(meta.gameTitle.empty());
}

TEST(ROMMetadataAnalyzerTest, ShortGameCodeHandledGracefully) {
  // Edge case: game code area exists but has unexpected content
  std::vector<uint8_t> rom(0x200, 0x00);
  rom[0xAC] = 'A';
  rom[0xAD] = 'B';
  rom[0xAE] = 0;
  rom[0xAF] = 0;
  ROMMetadata meta = ROMMetadataAnalyzer::Analyze(rom);
  EXPECT_EQ(meta.gameCode.size(), 4u); // Always reads 4 bytes
}
