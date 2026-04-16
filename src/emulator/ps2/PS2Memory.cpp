#include "emulator/ps2/PS2Memory.h"
#include "emulator/ps2/GS.h"
#include "emulator/ps2/PS2Constants.h"

namespace PS2Emulator {

PS2Memory::PS2Memory()
    : ee_ram_(kEERamSize, 0),
      iop_ram_(kIopRamSize, 0),
      scratch_(kScratchSize, 0),
      gs_(nullptr) {}

void PS2Memory::Init(GS* gs) {
    gs_ = gs;
}

static uint32_t PhysAddr(uint32_t addr) {
    return addr & 0x1FFFFFFFU;
}

uint8_t PS2Memory::Read8(uint32_t addr) {
    addr = PhysAddr(addr);
    if (addr < kEERamSize)
        return ee_ram_[addr];
    if (addr >= kIopRamBase && addr < kIopRamBase + kIopRamSize)
        return iop_ram_[addr - kIopRamBase];
    if (addr >= kScratchBase && addr < kScratchBase + kScratchSize)
        return scratch_[addr - kScratchBase];
    return 0xFF;
}

uint16_t PS2Memory::Read16(uint32_t addr) {
    return static_cast<uint16_t>(Read8(addr)) |
          (static_cast<uint16_t>(Read8(addr + 1)) << 8);
}

uint32_t PS2Memory::Read32(uint32_t addr) {
    addr = PhysAddr(addr);
    if (addr >= kGsRegsBase && addr < kGsRegsBase + kGsRegsSize && gs_)
        return static_cast<uint32_t>(gs_->ReadPrivReg(addr - kGsRegsBase));
    return static_cast<uint32_t>(Read8(addr))        |
          (static_cast<uint32_t>(Read8(addr + 1)) << 8)  |
          (static_cast<uint32_t>(Read8(addr + 2)) << 16) |
          (static_cast<uint32_t>(Read8(addr + 3)) << 24);
}

void PS2Memory::Write8(uint32_t addr, uint8_t val) {
    addr = PhysAddr(addr);
    if (addr < kEERamSize) { ee_ram_[addr] = val; return; }
    if (addr >= kIopRamBase && addr < kIopRamBase + kIopRamSize) {
        iop_ram_[addr - kIopRamBase] = val; return;
    }
    if (addr >= kScratchBase && addr < kScratchBase + kScratchSize) {
        scratch_[addr - kScratchBase] = val;
    }
}

void PS2Memory::Write16(uint32_t addr, uint16_t val) {
    Write8(addr,     static_cast<uint8_t>(val));
    Write8(addr + 1, static_cast<uint8_t>(val >> 8));
}

void PS2Memory::Write32(uint32_t addr, uint32_t val) {
    addr = PhysAddr(addr);
    if (addr >= kGsRegsBase && addr < kGsRegsBase + kGsRegsSize && gs_) {
        gs_->WritePrivReg(addr - kGsRegsBase, val); return;
    }
    Write8(addr,     static_cast<uint8_t>(val));
    Write8(addr + 1, static_cast<uint8_t>(val >> 8));
    Write8(addr + 2, static_cast<uint8_t>(val >> 16));
    Write8(addr + 3, static_cast<uint8_t>(val >> 24));
}

uint32_t PS2Memory::IopRead32(uint32_t addr) {
    return Read32(addr);
}

void PS2Memory::IopWrite32(uint32_t addr, uint32_t val) {
    Write32(addr, val);
}

PS2Memory::State PS2Memory::SaveState() const {
    return {ee_ram_, iop_ram_, scratch_};
}

void PS2Memory::LoadState(const State& state) {
    ee_ram_  = state.ee_ram;
    iop_ram_ = state.iop_ram;
    scratch_ = state.scratch;
}

}  // namespace PS2Emulator
