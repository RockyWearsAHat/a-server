#include "emulator/gb/GBMemory.h"
#include "emulator/gb/GBCartridge.h"
#include "emulator/gb/GBPPU.h"
#include "emulator/gb/GBAPU.h"

namespace GBEmulator {

GBMemory::GBMemory()
    : current_rom_bank_(1),
      current_ram_bank_(0),
      cart_(nullptr),
      ppu_(nullptr),
      apu_(nullptr) {
  vram_.resize(0x2000);
  wram_.resize(0x2000);
  hram_.resize(0x7F);
  oam_.resize(0xA0);
}

void GBMemory::Init(GBCartridge* cart, GBPPU* ppu, GBAPU* apu) {
  cart_ = cart;
  ppu_ = ppu;
  apu_ = apu;
}

uint8_t GBMemory::Read8(uint16_t addr) {
  if (addr < 0x8000) {
    // ROM area (cartridge)
    if (cart_) return cart_->Read8(addr);
    return 0xFF;
  } else if (addr < 0xA000) {
    // VRAM
    return vram_[addr - 0x8000];
  } else if (addr < 0xC000) {
    // Cartridge RAM
    if (cart_) return cart_->Read8(addr);
    return 0xFF;
  } else if (addr < 0xE000) {
    // Internal RAM (WRAM)
    return wram_[addr - 0xC000];
  } else if (addr < 0xFE00) {
    // Echo RAM (mirrors WRAM)
    return wram_[addr - 0xE000];
  } else if (addr < 0xFEA0) {
    // OAM (sprite data)
    return oam_[addr - 0xFE00];
  } else if (addr < 0xFF00) {
    // Unusable
    return 0xFF;
  } else if (addr < 0xFF80) {
    // I/O registers (PPU, APU, etc.)
    if (addr == 0xFF00) {
      uint8_t result = static_cast<uint8_t>(0xC0 | joyp_select_ | 0x0F);
      if ((joyp_select_ & 0x10) == 0) {
        result = static_cast<uint8_t>((result & 0xF0) | (joypad_state_ & 0x0F));
      }
      if ((joyp_select_ & 0x20) == 0) {
        result = static_cast<uint8_t>((result & 0xF0) | ((joypad_state_ >> 4) & 0x0F));
      }
      return result;
    }
    if (addr >= 0xFF40 && addr < 0xFF50 && ppu_) {
      return ppu_->ReadReg(addr - 0xFF40);
    } else if (addr >= 0xFF10 && addr < 0xFF40 && apu_) {
      return apu_->ReadReg(addr - 0xFF10);
    }
    return 0xFF;
  } else {
    // High RAM
    return hram_[addr - 0xFF80];
  }
}

void GBMemory::Write8(uint16_t addr, uint8_t val) {
  if (addr < 0x8000) {
    // ROM (cartridge banking)
    if (cart_) cart_->Write8(addr, val);
  } else if (addr < 0xA000) {
    // VRAM
    vram_[addr - 0x8000] = val;
  } else if (addr < 0xC000) {
    // Cartridge RAM
    if (cart_) cart_->Write8(addr, val);
  } else if (addr < 0xE000) {
    // Internal RAM (WRAM)
    wram_[addr - 0xC000] = val;
  } else if (addr < 0xFE00) {
    // Echo RAM (mirrors WRAM)
    wram_[addr - 0xE000] = val;
  } else if (addr < 0xFEA0) {
    // OAM (sprite data)
    oam_[addr - 0xFE00] = val;
  } else if (addr < 0xFF00) {
    // Unusable
    return;
  } else if (addr < 0xFF80) {
    // I/O registers
    if (addr == 0xFF00) {
      joyp_select_ = static_cast<uint8_t>(val & 0x30);
      return;
    }
    if (addr >= 0xFF40 && addr < 0xFF50 && ppu_) {
      ppu_->WriteReg(addr - 0xFF40, val);
    } else if (addr >= 0xFF10 && addr < 0xFF40 && apu_) {
      apu_->WriteReg(addr - 0xFF10, val);
    }
  } else {
    // High RAM
    hram_[addr - 0xFF80] = val;
  }
}

uint16_t GBMemory::Read16(uint16_t addr) {
  uint8_t low = Read8(addr);
  uint8_t high = Read8(static_cast<uint16_t>(addr + 1));
  return (high << 8) | low;
}

void GBMemory::Write16(uint16_t addr, uint16_t val) {
  Write8(addr, static_cast<uint8_t>(val & 0xFF));
  Write8(static_cast<uint16_t>(addr + 1), static_cast<uint8_t>((val >> 8) & 0xFF));
}

uint8_t GBMemory::ApuRead8(uint16_t addr) {
  // APU can read from its own RAM region
  if (addr < 0x2000) {
    return wram_[addr];
  }
  return 0xFF;
}

void GBMemory::ApuWrite8(uint16_t addr, uint8_t val) {
  if (addr < 0x2000) {
    wram_[addr] = val;
  }
}

GBMemory::State GBMemory::SaveState() const {
  return State{
    .vram = vram_,
    .wram = wram_,
    .hram = hram_,
    .oam = oam_,
    .current_rom_bank = current_rom_bank_,
    .current_ram_bank = current_ram_bank_,
  };
}

void GBMemory::LoadState(const State& state) {
  vram_ = state.vram;
  wram_ = state.wram;
  hram_ = state.hram;
  oam_ = state.oam;
  current_rom_bank_ = state.current_rom_bank;
  current_ram_bank_ = state.current_ram_bank;
}

}  // namespace GBEmulator
