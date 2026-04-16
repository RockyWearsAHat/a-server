#include "emulator/saturn/SaturnMemory.h"
#include "emulator/saturn/VDP1.h"
#include "emulator/saturn/VDP2.h"
#include "emulator/saturn/SaturnConstants.h"

namespace SaturnEmulator {

SaturnMemory::SaturnMemory()
    : vdp1_(nullptr), vdp2_(nullptr) {
  low_ram_.resize(kLowRamSize, 0);
  high_ram_.resize(kHighRamSize, 0);
  sound_ram_.resize(kSoundRamSize, 0);
  backup_ram_.resize(kBackupRamSize, 0);
}

void SaturnMemory::Init(VDP1* vdp1, VDP2* vdp2) {
  vdp1_ = vdp1;
  vdp2_ = vdp2;
}

uint8_t SaturnMemory::Read8(uint32_t addr) {
  // Work RAM-L: 0x00200000–0x002FFFFF
  if (addr >= kLowRamBase && addr < kLowRamBase + kLowRamSize) {
    return low_ram_[addr - kLowRamBase];
  }
  // Work RAM-H: 0x06000000–0x060FFFFF
  if (addr >= kHighRamBase && addr < kHighRamBase + kHighRamSize) {
    return high_ram_[addr - kHighRamBase];
  }
  // Sound RAM: 0x05A80000–0x05AFFFFF
  if (addr >= kSoundRamBase && addr < kSoundRamBase + kSoundRamSize) {
    return sound_ram_[addr - kSoundRamBase];
  }
  // Backup RAM: 0x00180000–0x00187FFF
  if (addr >= kBackupRamBase && addr < kBackupRamBase + kBackupRamSize) {
    return backup_ram_[addr - kBackupRamBase];
  }
  return 0xFF;
}

uint16_t SaturnMemory::Read16(uint32_t addr) {
  uint8_t hi = Read8(addr);
  uint8_t lo = Read8(addr + 1);
  return (hi << 8) | lo;
}

uint32_t SaturnMemory::Read32(uint32_t addr) {
  uint8_t b0 = Read8(addr);
  uint8_t b1 = Read8(addr + 1);
  uint8_t b2 = Read8(addr + 2);
  uint8_t b3 = Read8(addr + 3);
  return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

void SaturnMemory::Write8(uint32_t addr, uint8_t val) {
  if (addr >= kLowRamBase && addr < kLowRamBase + kLowRamSize) {
    low_ram_[addr - kLowRamBase] = val;
    return;
  }
  if (addr >= kHighRamBase && addr < kHighRamBase + kHighRamSize) {
    high_ram_[addr - kHighRamBase] = val;
    return;
  }
  if (addr >= kSoundRamBase && addr < kSoundRamBase + kSoundRamSize) {
    sound_ram_[addr - kSoundRamBase] = val;
    return;
  }
  if (addr >= kBackupRamBase && addr < kBackupRamBase + kBackupRamSize) {
    backup_ram_[addr - kBackupRamBase] = val;
    return;
  }
}

void SaturnMemory::Write16(uint32_t addr, uint16_t val) {
  Write8(addr,     (val >> 8) & 0xFF);
  Write8(addr + 1, val & 0xFF);
}

void SaturnMemory::Write32(uint32_t addr, uint32_t val) {
  Write8(addr,     (val >> 24) & 0xFF);
  Write8(addr + 1, (val >> 16) & 0xFF);
  Write8(addr + 2, (val >> 8)  & 0xFF);
  Write8(addr + 3, val & 0xFF);
}

SaturnMemory::State SaturnMemory::SaveState() const {
  return State{
    .low_ram    = low_ram_,
    .high_ram   = high_ram_,
    .sound_ram  = sound_ram_,
    .backup_ram = backup_ram_,
  };
}

void SaturnMemory::LoadState(const State& state) {
  low_ram_    = state.low_ram;
  high_ram_   = state.high_ram;
  sound_ram_  = state.sound_ram;
  backup_ram_ = state.backup_ram;
}

}  // namespace SaturnEmulator
