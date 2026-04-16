#include "emulator/n64/N64Cartridge.h"
#include "emulator/n64/N64Constants.h"
#include <fstream>
#include <stdexcept>
#include <cstring>

namespace N64Emulator {

void N64Cartridge::Load(const std::string& rom_path) {
  std::ifstream file(rom_path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open ROM: " + rom_path);
  }

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  if (size < static_cast<std::streamsize>(kRomMinSize) ||
      size > static_cast<std::streamsize>(kRomMaxSize)) {
    throw std::runtime_error("Invalid ROM size: " + std::to_string(size));
  }

  rom_.resize(size);
  if (!file.read(reinterpret_cast<char*>(rom_.data()), size)) {
    throw std::runtime_error("Failed to read ROM");
  }

  file.close();

  // Detect and normalize format based on first 4 bytes
  if (rom_.size() >= 4) {
    uint32_t magic = (rom_[0] << 24) | (rom_[1] << 16) | (rom_[2] << 8) | rom_[3];
    if (magic == 0x37804012U) {
      ByteSwapToZ64();    // .v64 byte-swapped
    } else if (magic == 0x40123780U) {
      WordSwapToZ64();    // .n64 word-swapped
    }
    // 0x80371240 is already .z64 big-endian — no conversion
  }

  // Parse ROM header (all fields big-endian)
  if (rom_.size() >= 0x40) {
    entry_point_ = (rom_[0x08] << 24) | (rom_[0x09] << 16) | (rom_[0x0A] << 8) | rom_[0x0B];
    crc1_        = (rom_[0x10] << 24) | (rom_[0x11] << 16) | (rom_[0x12] << 8) | rom_[0x13];
    crc2_        = (rom_[0x14] << 24) | (rom_[0x15] << 16) | (rom_[0x16] << 8) | rom_[0x17];

    // Title: 20 ASCII bytes at 0x20
    char title_buf[21] = {};
    std::memcpy(title_buf, rom_.data() + 0x20, 20);
    title_ = title_buf;
  }

  save_data_.clear();
}

void N64Cartridge::ByteSwapToZ64() {
  // .v64 format: every 2 bytes are swapped
  for (size_t i = 0; i + 1 < rom_.size(); i += 2) {
    std::swap(rom_[i], rom_[i+1]);
  }
}

void N64Cartridge::WordSwapToZ64() {
  // .n64 format: every 4 bytes are in reverse order
  for (size_t i = 0; i + 3 < rom_.size(); i += 4) {
    std::swap(rom_[i],   rom_[i+3]);
    std::swap(rom_[i+1], rom_[i+2]);
  }
}

uint8_t N64Cartridge::Read8(uint32_t addr) {
  uint32_t offset = addr & 0x0FFFFFFFU;  // Strip segment bits
  if (offset < rom_.size()) {
    return rom_[offset];
  }
  return 0xFF;
}

uint16_t N64Cartridge::Read16(uint32_t addr) {
  uint8_t hi = Read8(addr);
  uint8_t lo = Read8(addr + 1);
  return (hi << 8) | lo;
}

uint32_t N64Cartridge::Read32(uint32_t addr) {
  uint8_t b0 = Read8(addr);
  uint8_t b1 = Read8(addr + 1);
  uint8_t b2 = Read8(addr + 2);
  uint8_t b3 = Read8(addr + 3);
  return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

N64Cartridge::State N64Cartridge::SaveState() const {
  return State{
    .save_data   = save_data_,
    .entry_point = entry_point_,
    .title       = title_,
    .crc1        = crc1_,
    .crc2        = crc2_,
  };
}

void N64Cartridge::LoadState(const State& state) {
  save_data_   = state.save_data;
  entry_point_ = state.entry_point;
  title_       = state.title;
  crc1_        = state.crc1;
  crc2_        = state.crc2;
}

}  // namespace N64Emulator
