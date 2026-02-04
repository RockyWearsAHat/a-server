#include <algorithm>
#include <emulator/gba/GameDB.h>
#include <iostream>
#include <vector>

namespace AIO::Emulator::GBA {

const std::unordered_map<std::string, GameOverride> GameDB::overrides = {
    // Super Mario Advance 2 (Super Mario World)
    // Fix: Patch the main loop to wait on the correct ISR address.
    // The ISR writes to 0x03002BD1 (byte), but the main loop uses LDRH
    // (halfword).
    // We must point the main loop to 0x03002BD0 so LDRH reads the byte
    // correctly in the lower half.
    {"AMQE",
     {"AMQE",
      "Super Mario Advance 2",
      SaveType::EEPROM_64K,
      {{0x494, 0x03002BD0}, {0x560, 0x03002BD0}}}},
    {"AMQP",
     {"AMQP",
      "Super Mario Advance 2",
      SaveType::EEPROM_64K,
      {{0x494, 0x03002BD0}, {0x560, 0x03002BD0}}}},
    {"AMQJ",
     {"AMQJ",
      "Super Mario Advance 2",
      SaveType::EEPROM_64K,
      {{0x494, 0x03002BD0}, {0x560, 0x03002BD0}}}},

    // Super Mario Advance 2 (Alternate Version - e.g. Player's Choice)
    // Same patches as other versions - the main loop address mismatch affects
    // all versions
    {"AA2E",
     {"AA2E",
      "Super Mario Advance 2 (Alt)",
      SaveType::EEPROM_64K,
      {{0x494, 0x03002BD0}, {0x560, 0x03002BD0}}}},

    // Donkey Kong Country
    {"BDQE", {"BDQE", "Donkey Kong Country", SaveType::EEPROM_64K, {}}},
    {"BDQP", {"BDQP", "Donkey Kong Country", SaveType::EEPROM_64K, {}}},
    {"BDQJ", {"BDQJ", "Donkey Kong Country", SaveType::EEPROM_64K, {}}},
    {"A5NE", {"A5NE", "Donkey Kong Country", SaveType::EEPROM_64K, {}}},

    // Super Mario Advance (SMB2)
    {"AMAE", {"AMAE", "Super Mario Advance", SaveType::EEPROM_64K, {}}},
    {"AMAP", {"AMAP", "Super Mario Advance", SaveType::EEPROM_64K, {}}},
    {"AMAJ", {"AMAJ", "Super Mario Advance", SaveType::EEPROM_64K, {}}},

    // Pokemon Games (Flash 1M)
    {"BPRE", {"BPRE", "Pokemon FireRed", SaveType::Flash1M, {}}},
    {"BPGE", {"BPGE", "Pokemon LeafGreen", SaveType::Flash1M, {}}},
    {"RSE", {"RSE", "Pokemon Ruby", SaveType::Flash1M, {}}}, // Check code
    {"AXVE", {"AXVE", "Pokemon Ruby", SaveType::Flash1M, {}}},
    {"AXPE", {"AXPE", "Pokemon Sapphire", SaveType::Flash1M, {}}},
    {"BPEE", {"BPEE", "Pokemon Emerald", SaveType::Flash1M, {}}},

    // Classic NES Series (all use EEPROM 4K, require prefetch buffer emulation)
    {"FADE", {"FADE", "Classic NES - Castlevania", SaveType::EEPROM_4K, {}}},
    {"FBME", {"FBME", "Classic NES - Bomberman", SaveType::EEPROM_4K, {}}},
    {"FDKE", {"FDKE", "Classic NES - Donkey Kong", SaveType::EEPROM_4K, {}}},
    {"FDME", {"FDME", "Classic NES - Dr. Mario", SaveType::EEPROM_4K, {}}},
    {"FEBE", {"FEBE", "Classic NES - Excitebike", SaveType::EEPROM_64K, {}}},
    {"FICE", {"FICE", "Classic NES - Ice Climber", SaveType::EEPROM_4K, {}}},
    {"FLBE", {"FLBE", "Classic NES - Zelda II", SaveType::EEPROM_64K, {}}},
    {"FMRE", {"FMRE", "Classic NES - Metroid", SaveType::EEPROM_4K, {}}},
    {"FP7E", {"FP7E", "Classic NES - Pac-Man", SaveType::EEPROM_4K, {}}},
    {"FSME",
     {"FSME", "Classic NES - Super Mario Bros", SaveType::EEPROM_4K, {}}},
    {"FXVE", {"FXVE", "Classic NES - Xevious", SaveType::EEPROM_4K, {}}},
    {"FZLE",
     {"FZLE", "Classic NES - Legend of Zelda", SaveType::EEPROM_64K, {}}},
};

GameOverride GameDB::GetOverride(const std::string &gameCode) {
  auto it = overrides.find(gameCode);
  if (it != overrides.end()) {
    return it->second;
  }
  return {gameCode, "", SaveType::Auto, {}};
}

SaveType GameDB::DetectSaveType(const std::vector<uint8_t> &romData) {
  // Helper lambda for searching byte sequences
  auto contains = [&](const std::string &str) -> bool {
    if (str.length() > romData.size())
      return false;
    auto it =
        std::search(romData.begin(), romData.end(), str.begin(), str.end());
    return it != romData.end();
  };

  if (contains("EEPROM_V111"))
    return SaveType::EEPROM_4K;
  if (contains("EEPROM_V"))
    return SaveType::EEPROM_64K; // Default to 64K for other versions
  if (contains("SRAM_V"))
    return SaveType::SRAM;
  if (contains("FLASH_V"))
    return SaveType::Flash512;
  if (contains("FLASH512_V"))
    return SaveType::Flash512;
  if (contains("FLASH1M_V"))
    return SaveType::Flash1M;

  return SaveType::Auto;
}

} // namespace AIO::Emulator::GBA
