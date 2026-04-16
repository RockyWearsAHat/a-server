#include "emulator/gamecube/GameCubeMemory.h"
#include "emulator/gamecube/Flipper.h"
#include "emulator/gamecube/GameCubeConstants.h"
namespace GameCubeEmulator {

GameCubeMemory::GameCubeMemory()
    : ram_(kRamSize, 0)
    , flipper_(nullptr) {}

void GameCubeMemory::Init(Flipper* flipper) {
    flipper_ = flipper;
}

uint8_t GameCubeMemory::Read8(uint32_t addr) {
    if (addr < kRamSize) return ram_[addr];
    return 0;
}

uint16_t GameCubeMemory::Read16(uint32_t addr) {
    if (addr + 1 < kRamSize)
        return (static_cast<uint16_t>(ram_[addr]) << 8) | ram_[addr + 1];
    return 0;
}

uint32_t GameCubeMemory::Read32(uint32_t addr) {
    // Main RAM
    if (addr < kRamSize) {
        return (static_cast<uint32_t>(ram_[addr    ]) << 24) |
               (static_cast<uint32_t>(ram_[addr + 1]) << 16) |
               (static_cast<uint32_t>(ram_[addr + 2]) <<  8) |
                static_cast<uint32_t>(ram_[addr + 3]);
    }
    // Flipper regs (0xCC000000 range)
    if (addr >= kFlipperRegsBase && addr < kFlipperRegsBase + kFlipperRegsSize) {
        if (flipper_) return flipper_->ReadReg(addr - kFlipperRegsBase);
    }
    return 0;
}

void GameCubeMemory::Write8(uint32_t addr, uint8_t val) {
    if (addr < kRamSize) ram_[addr] = val;
}

void GameCubeMemory::Write16(uint32_t addr, uint16_t val) {
    if (addr + 1 < kRamSize) {
        ram_[addr    ] = static_cast<uint8_t>(val >> 8);
        ram_[addr + 1] = static_cast<uint8_t>(val);
    }
}

void GameCubeMemory::Write32(uint32_t addr, uint32_t val) {
    if (addr < kRamSize) {
        ram_[addr    ] = static_cast<uint8_t>(val >> 24);
        ram_[addr + 1] = static_cast<uint8_t>(val >> 16);
        ram_[addr + 2] = static_cast<uint8_t>(val >>  8);
        ram_[addr + 3] = static_cast<uint8_t>(val);
        return;
    }
    // GP FIFO pass-through
    if (addr >= kFifoBase && addr < kFifoBase + 0x100000U) {
        if (flipper_) flipper_->WriteFifo(val);
        return;
    }
    // Flipper regs
    if (addr >= kFlipperRegsBase && addr < kFlipperRegsBase + kFlipperRegsSize) {
        if (flipper_) flipper_->WriteReg(addr - kFlipperRegsBase, val);
    }
}

GameCubeMemory::State GameCubeMemory::SaveState() const {
    return { ram_ };
}

void GameCubeMemory::LoadState(const State& s) {
    ram_ = s.ram;
}

}  // namespace GameCubeEmulator
