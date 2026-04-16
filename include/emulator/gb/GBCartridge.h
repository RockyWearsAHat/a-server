#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include <string>

namespace GBEmulator {

// Game Boy ROM cartridge with ROM/RAM banking
class GBCartridge {
 public:
  GBCartridge() = default;
  ~GBCartridge() = default;

  // Load ROM from file (internally validates size)
  void Load(const std::string& rom_path);

  // Load ROM from in-memory bytes (internally validates size)
  void Load(std::span<const uint8_t> rom_data);

  // 8-bit and 16-bit reads
  uint8_t Read8(uint16_t addr);
  uint16_t Read16(uint16_t addr);

  // 8-bit writes (to cartridge RAM if enabled)
  void Write8(uint16_t addr, uint8_t val);

  // Get ROM/RAM bank info
  uint8_t GetRomBankNumber() const { return rom_bank_; }
  uint8_t GetRamBankNumber() const { return ram_bank_; }

  // State save/restore
  struct State {
    std::vector<uint8_t> cartridge_ram;
    uint8_t rom_bank;
    uint8_t ram_bank;
    bool ram_enabled;
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  // ROM data (copied at Load time)
  std::vector<uint8_t> rom_;         // Full ROM image
  std::vector<uint8_t> cartridge_ram_;  // Battery-backed RAM (if present)

  // MBC state
  uint8_t rom_bank_;                 // Current ROM bank (typically 1–127)
  uint8_t ram_bank_;                 // Current RAM bank (typically 0–3)
  bool ram_enabled_;                 // Whether cartridge RAM is accessible

  // Cartridge type detection
  enum CartridgeType {
    kMBC1,      // Most common
    kMBC2,      // Simpler with built-in RAM
    kMBC3,      // With RTC (Real Time Clock)
    kMBC5,      // Modern games
    kSimple,    // No banking (32 KB)
  } type_;

  void DetectType();
  void SetRomBank(uint8_t bank);
  void SetRamBank(uint8_t bank);
};

}  // namespace GBEmulator
