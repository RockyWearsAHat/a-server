#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace N64Emulator {

// N64 cartridge ROM loader with byte-swap detection
class N64Cartridge {
 public:
  N64Cartridge() = default;
  ~N64Cartridge() = default;

  // Load .z64 (big-endian), .v64 (byte-swapped), or .n64 (word-swapped) format
  void Load(const std::string& rom_path);

  // 8-bit, 16-bit, and 32-bit ROM reads
  uint8_t  Read8 (uint32_t addr);
  uint16_t Read16(uint32_t addr);
  uint32_t Read32(uint32_t addr);

  // ROM header fields
  uint32_t GetEntryPoint() const        { return entry_point_; }
  const std::string& GetTitle() const   { return title_; }
  uint32_t GetCRC1() const              { return crc1_; }
  uint32_t GetCRC2() const              { return crc2_; }

  // State save/restore (SRAM/EEPROM/Flash + parsed header metadata)
  struct State {
    std::vector<uint8_t> save_data;  // Persistent save (SRAM/EEPROM)
    uint32_t entry_point;
    std::string title;
    uint32_t crc1;
    uint32_t crc2;
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  std::vector<uint8_t> rom_;          // Full ROM image (always in z64/big-endian after load)
  std::vector<uint8_t> save_data_;    // Battery-backed save

  uint32_t entry_point_;             // From ROM header (0x08)
  std::string title_;                // From ROM header (0x20, 20 chars)
  uint32_t crc1_;                    // From ROM header (0x10)
  uint32_t crc2_;                    // From ROM header (0x14)

  // Byte-swap helpers
  void ByteSwapToZ64();   // Convert .v64 (1032) → .z64 (0123)
  void WordSwapToZ64();   // Convert .n64 (3210) → .z64 (0123)
};

}  // namespace N64Emulator
