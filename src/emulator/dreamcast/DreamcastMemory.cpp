#include "emulator/dreamcast/DreamcastMemory.h"
#include "emulator/dreamcast/PowerVR2.h"
#include "emulator/dreamcast/DreamcastConstants.h"

namespace DreamcastEmulator {

DreamcastMemory::DreamcastMemory()
    : ram_(kRamSize, 0),
      aica_ram_(kAicaSize, 0),
      pvr_(nullptr) {}

void DreamcastMemory::Init(PowerVR2* pvr) {
    pvr_ = pvr;
}

// Physical address masking: SH-4 P0/P1/P2 all mask top 3 bits → 29-bit physical
static uint32_t PhysAddr(uint32_t addr) {
    return addr & 0x1FFFFFFFU;
}

uint8_t DreamcastMemory::Read8(uint32_t addr) {
    addr = PhysAddr(addr);
    if (addr >= kRamBase && addr < kRamBase + kRamSize)
        return ram_[addr - kRamBase];
    if (addr >= kAicaBase && addr < kAicaBase + kAicaSize)
        return aica_ram_[addr - kAicaBase];
    if (addr >= kVramBase && addr < kVramBase + kVramSize && pvr_)
        return pvr_->ReadVram8(addr - kVramBase);
    return 0xFF;
}

uint16_t DreamcastMemory::Read16(uint32_t addr) {
    addr = PhysAddr(addr);
    return static_cast<uint16_t>(Read8(addr)) |
          (static_cast<uint16_t>(Read8(addr + 1)) << 8);
}

uint32_t DreamcastMemory::Read32(uint32_t addr) {
    addr = PhysAddr(addr);
    if (addr >= kPvrRegsBase && addr < kPvrRegsBase + 0x1000U && pvr_)
        return pvr_->ReadReg(addr - kPvrRegsBase);
    return static_cast<uint32_t>(Read8(addr))        |
          (static_cast<uint32_t>(Read8(addr + 1)) << 8)  |
          (static_cast<uint32_t>(Read8(addr + 2)) << 16) |
          (static_cast<uint32_t>(Read8(addr + 3)) << 24);
}

void DreamcastMemory::Write8(uint32_t addr, uint8_t val) {
    addr = PhysAddr(addr);
    if (addr >= kRamBase && addr < kRamBase + kRamSize) {
        ram_[addr - kRamBase] = val;
        return;
    }
    if (addr >= kAicaBase && addr < kAicaBase + kAicaSize) {
        aica_ram_[addr - kAicaBase] = val;
        return;
    }
    if (addr >= kVramBase && addr < kVramBase + kVramSize && pvr_) {
        pvr_->WriteVram8(addr - kVramBase, val);
    }
}

void DreamcastMemory::Write16(uint32_t addr, uint16_t val) {
    Write8(addr,     static_cast<uint8_t>(val));
    Write8(addr + 1, static_cast<uint8_t>(val >> 8));
}

void DreamcastMemory::Write32(uint32_t addr, uint32_t val) {
    addr = PhysAddr(addr);
    if (addr >= kPvrRegsBase && addr < kPvrRegsBase + 0x1000U && pvr_) {
        pvr_->WriteReg(addr - kPvrRegsBase, val);
        return;
    }
    Write8(addr,     static_cast<uint8_t>(val));
    Write8(addr + 1, static_cast<uint8_t>(val >> 8));
    Write8(addr + 2, static_cast<uint8_t>(val >> 16));
    Write8(addr + 3, static_cast<uint8_t>(val >> 24));
}

DreamcastMemory::State DreamcastMemory::SaveState() const {
    return {ram_, aica_ram_};
}

void DreamcastMemory::LoadState(const State& state) {
    ram_      = state.ram;
    aica_ram_ = state.aica_ram;
}

}  // namespace DreamcastEmulator
