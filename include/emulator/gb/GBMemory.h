#pragma once

#include <cstdint>
#include <vector>

namespace GBEmulator {

class GBCartridge;
class GBPPU;
class GBAPU;

// Game Boy memory map (16-bit address space)
class GBMemory {
 public:
  GBMemory();
  ~GBMemory() = default;

  // Initialize with peripheral pointers (called after construction)
  void Init(GBCartridge* cart, GBPPU* ppu, GBAPU* apu);

  // 8-bit and 16-bit reads/writes
  uint8_t Read8(uint16_t addr);
  void Write8(uint16_t addr, uint8_t val);
  uint16_t Read16(uint16_t addr);
  void Write16(uint16_t addr, uint16_t val);

  // APU RAM access (0xC000–0xDFFF in WRAM)
  uint8_t ApuRead8(uint16_t addr);
  void ApuWrite8(uint16_t addr, uint8_t val);

  // State save/restore
  struct State {
    std::vector<uint8_t> vram;          // 8 KB VRAM
    std::vector<uint8_t> wram;          // 8 KB WRAM (internal RAM)
    std::vector<uint8_t> hram;          // 127 bytes high RAM
    std::vector<uint8_t> oam;           // 160 bytes sprite data
    uint8_t current_rom_bank;
    uint8_t current_ram_bank;
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  // Memory regions
  std::vector<uint8_t> vram_;           // 0x8000–0x9FFF: VRAM (8 KB)
  std::vector<uint8_t> wram_;           // 0xC000–0xDFFF: Internal RAM (8 KB)
  std::vector<uint8_t> hram_;           // 0xFF80–0xFFFF: High RAM (127 bytes, not including IE)
  std::vector<uint8_t> oam_;            // 0xFE00–0xFE9F: Sprite data (160 bytes)

  // Paging
  uint8_t current_rom_bank_;
  uint8_t current_ram_bank_;

  // Peripheral pointers (unowned)
  GBCartridge* cart_;
  GBPPU* ppu_;
  GBAPU* apu_;
};

}  // namespace GBEmulator
