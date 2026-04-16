#include "emulator/nes/NESMemory.h"
#include "emulator/nes/NESCartridge.h"
#include "emulator/common/SaveState.h"
#include <stdexcept>

namespace AIO::Emulator::NES {

NESMemory::NESMemory() {
    wram_.fill(0);
}

void NESMemory::Init(NESCartridge* cartridge) {
    if (cartridge == nullptr) {
        throw std::invalid_argument("NESMemory::Init: cartridge must not be null");
    }
    cartridge_ = cartridge;
}

void NESMemory::Reset() {
    wram_.fill(0);
}

uint8_t NESMemory::Read8(uint32_t rawAddr) {
    const uint16_t addr = static_cast<uint16_t>(rawAddr & 0xFFFF);

    if (addr < 0x2000) {
        // WRAM: $0000–$07FF mirrored ×4 to $0000–$1FFF
        return wram_[addr & (kWramSize - 1)];
    }
    if (addr < 0x4000) {
        // PPU registers: $2000–$2007 mirrored to $2000–$3FFF
        const uint8_t reg = static_cast<uint8_t>(addr & 0x07);
        return ReadPpuRegister(reg);
    }
    if (addr < 0x4020) {
        // APU / IO: $4000–$4017
        return ReadApuIo(addr);
    }
    // Cartridge: $4020–$FFFF
    return cartridge_->CpuRead(addr);
}

void NESMemory::Write8(uint32_t rawAddr, uint8_t value) {
    const uint16_t addr = static_cast<uint16_t>(rawAddr & 0xFFFF);

    if (addr < 0x2000) {
        wram_[addr & (kWramSize - 1)] = value;
        return;
    }
    if (addr < 0x4000) {
        const uint8_t reg = static_cast<uint8_t>(addr & 0x07);
        WritePpuRegister(reg, value);
        return;
    }
    if (addr < 0x4020) {
        WriteApuIo(addr, value);
        return;
    }
    cartridge_->CpuWrite(addr, value);
}

uint8_t NESMemory::ReadWRAM(uint16_t addr) const noexcept {
    return wram_[addr & (kWramSize - 1)];
}

void NESMemory::WriteWRAM(uint16_t addr, uint8_t value) noexcept {
    wram_[addr & (kWramSize - 1)] = value;
}

void NESMemory::SetPpuCallbacks(PpuReadFn onRead, PpuWriteFn onWrite) {
    ppuRead_  = std::move(onRead);
    ppuWrite_ = std::move(onWrite);
}

void NESMemory::SetApuCallbacks(ApuReadFn onRead, ApuWriteFn onWrite) {
    apuRead_  = std::move(onRead);
    apuWrite_ = std::move(onWrite);
}

uint8_t NESMemory::ReadPpuRegister(uint8_t reg) const {
    if (ppuRead_) {
        return ppuRead_(reg);
    }
    return 0xFF; // open bus
}

void NESMemory::WritePpuRegister(uint8_t reg, uint8_t value) {
    if (ppuWrite_) {
        ppuWrite_(reg, value);
    }
}

uint8_t NESMemory::ReadApuIo(uint16_t addr) const {
    if (apuRead_) {
        return apuRead_(addr);
    }
    return 0xFF;
}

void NESMemory::WriteApuIo(uint16_t addr, uint8_t value) {
    if (apuWrite_) {
        apuWrite_(addr, value);
    }
}

void NESMemory::SaveState(Common::SaveStateWriter& w) const {
    w.WriteBytes(wram_.data(), wram_.size());
}

void NESMemory::LoadState(Common::SaveStateReader& r) {
    r.ReadBytes(wram_.data(), wram_.size());
}

} // namespace AIO::Emulator::NES
