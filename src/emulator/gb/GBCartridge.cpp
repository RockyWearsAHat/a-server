#include "emulator/gb/GBCartridge.h"
#include "emulator/gb/GBConstants.h"

#include <fstream>
#include <stdexcept>

namespace GBEmulator {

void GBCartridge::Load(const std::string& rom_path) {
  std::ifstream file(rom_path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open ROM: " + rom_path);
  }

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> file_data(static_cast<size_t>(size));
  if (!file.read(reinterpret_cast<char*>(file_data.data()), size)) {
    throw std::runtime_error("Failed to read ROM file");
  }

  Load(file_data);
}

void GBCartridge::Load(std::span<const uint8_t> rom_data) {
  // Validate size (between 32 KB minimum and 8 MB maximum)
  if (rom_data.size() < 0x8000 || rom_data.size() > kRomMaxSize) {
    throw std::runtime_error("Invalid ROM size: " + std::to_string(rom_data.size()));
  }

  rom_.assign(rom_data.begin(), rom_data.end());

  // Initialize banking state
  rom_bank_ = 1;
  ram_bank_ = 0;
  ram_enabled_ = false;
  
  // Detect cartridge type from ROM header (0x0147)
  if (rom_.size() > 0x0147) {
    DetectType();
  }

  // Allocate cartridge RAM based on header (0x0149)
  uint32_t ram_size = 0;
  if (rom_.size() > 0x0149) {
    uint8_t ram_code = rom_[0x0149];
    switch (ram_code) {
      case 0x00: ram_size = 0; break;
      case 0x01: ram_size = 0x800; break;      // 2 KB
      case 0x02: ram_size = 0x2000; break;     // 8 KB
      case 0x03: ram_size = 0x8000; break;     // 32 KB
      case 0x04: ram_size = 0x20000; break;    // 128 KB
      case 0x05: ram_size = 0x10000; break;    // 64 KB
      default: ram_size = 0x2000; break;       // Default to 8 KB
    }
  }

  if (ram_size > 0) {
    cartridge_ram_.resize(ram_size);
  }
}

void GBCartridge::DetectType() {
  uint8_t type_code = rom_[0x0147];
  // Simplified type detection
  if (type_code >= 0x01 && type_code <= 0x03) {
    type_ = kMBC1;
  } else if (type_code >= 0x05 && type_code <= 0x06) {
    type_ = kMBC2;
  } else if (type_code >= 0x0F && type_code <= 0x13) {
    type_ = kMBC3;
  } else if (type_code >= 0x19 && type_code <= 0x1E) {
    type_ = kMBC5;
  } else {
    type_ = kSimple;
  }
}

uint8_t GBCartridge::Read8(uint16_t addr) {
  if (addr < 0x4000) {
    // ROM bank 0 (fixed)
    if (rom_.empty() || addr >= rom_.size()) {
      return 0xFF;
    }
    return rom_[addr];
  } else if (addr < 0x8000) {
    // ROM bank N (switchable)
    uint32_t offset = (addr - 0x4000) + (static_cast<uint32_t>(rom_bank_) * 0x4000);
    if (offset < rom_.size()) {
      return rom_[offset];
    }
    return 0xFF;
  } else if (addr >= 0xA000 && addr < 0xC000) {
    // Cartridge RAM
    if (!cartridge_ram_.empty() && ram_enabled_) {
      uint32_t ram_offset = (addr - 0xA000) + (static_cast<uint32_t>(ram_bank_) * 0x2000);
      if (ram_offset < cartridge_ram_.size()) {
        return cartridge_ram_[ram_offset];
      }
    }
    return 0xFF;
  }
  return 0xFF;
}

uint16_t GBCartridge::Read16(uint16_t addr) {
  uint8_t low = Read8(addr);
  uint8_t high = Read8(static_cast<uint16_t>(addr + 1));
  return (high << 8) | low;
}

void GBCartridge::Write8(uint16_t addr, uint8_t val) {
  // Banking control (MBC registers)
  if (addr < 0x2000) {
    // RAM enable (0x0000–0x1FFF)
    ram_enabled_ = (val & 0x0F) == 0x0A;
  } else if (addr < 0x4000) {
    // ROM bank select (0x2000–0x3FFF)
    rom_bank_ = val & 0x7F;
    if (rom_bank_ == 0) rom_bank_ = 1;  // Bank 0 is always lower 16 KB
  } else if (addr >= 0xA000 && addr < 0xC000) {
    // Cartridge RAM write
    if (!cartridge_ram_.empty() && ram_enabled_) {
      uint32_t ram_offset = (addr - 0xA000) + (static_cast<uint32_t>(ram_bank_) * 0x2000);
      if (ram_offset < cartridge_ram_.size()) {
        cartridge_ram_[ram_offset] = val;
      }
    }
  }
}

GBCartridge::State GBCartridge::SaveState() const {
  return State{
    .cartridge_ram = cartridge_ram_,
    .rom_bank = rom_bank_,
    .ram_bank = ram_bank_,
    .ram_enabled = ram_enabled_,
  };
}

void GBCartridge::LoadState(const State& state) {
  cartridge_ram_ = state.cartridge_ram;
  rom_bank_ = state.rom_bank;
  ram_bank_ = state.ram_bank;
  ram_enabled_ = state.ram_enabled;
}

}  // namespace GBEmulator
