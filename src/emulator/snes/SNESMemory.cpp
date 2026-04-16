#include "emulator/snes/SNESMemory.h"

#include "emulator/common/SaveState.h"
#include "emulator/snes/SNESCartridge.h"
#include "emulator/snes/SNESPPU.h"

namespace AIO::Emulator::SNES {

void SNESMemory::Init(SNESCartridge* cart, SNESPPU* ppu, SPC700* spc) noexcept {
    cart_ = cart;
    ppu_ = ppu;
    spc_ = spc;
}

uint8_t SNESMemory::Read8(uint32_t addr) {
    addr &= 0xFFFFFFu;

    // Work RAM banks.
    if (addr >= 0x7E0000u && addr <= 0x7FFFFFu) {
        return wram_[addr - 0x7E0000u];
    }

    // PPU registers in low bank mirrors.
    if ((addr & 0xFFFFu) >= 0x2100u && (addr & 0xFFFFu) <= 0x213Fu && ppu_ != nullptr) {
        return ppu_->ReadReg(static_cast<uint16_t>(addr & 0x3Fu));
    }

    if (cart_ != nullptr) {
        return cart_->Read8(addr);
    }

    return 0xFF;
}

uint16_t SNESMemory::Read16(uint32_t addr) {
    const uint8_t lo = Read8(addr);
    const uint8_t hi = Read8(addr + 1u);
    return static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8) | lo);
}

void SNESMemory::Write8(uint32_t addr, uint8_t value) {
    addr &= 0xFFFFFFu;

    if (addr >= 0x7E0000u && addr <= 0x7FFFFFu) {
        wram_[addr - 0x7E0000u] = value;
        return;
    }

    if ((addr & 0xFFFFu) >= 0x2100u && (addr & 0xFFFFu) <= 0x213Fu && ppu_ != nullptr) {
        ppu_->WriteReg(static_cast<uint16_t>(addr & 0x3Fu), value);
        return;
    }

    if (cart_ != nullptr) {
        cart_->Write8(addr, value);
    }
}

void SNESMemory::Write16(uint32_t addr, uint16_t value) {
    Write8(addr, static_cast<uint8_t>(value));
    Write8(addr + 1u, static_cast<uint8_t>(value >> 8));
}

uint8_t SNESMemory::SPCRead8(uint16_t addr) const noexcept {
    return apuRam_[addr];
}

void SNESMemory::SPCWrite8(uint16_t addr, uint8_t value) noexcept {
    apuRam_[addr] = value;
}

void SNESMemory::SaveState(AIO::Emulator::Common::SaveStateWriter& w) const {
    w.WriteBytes(wram_.data(), wram_.size());
    w.WriteBytes(apuRam_.data(), apuRam_.size());
}

void SNESMemory::LoadState(AIO::Emulator::Common::SaveStateReader& r) {
    r.ReadBytes(wram_.data(), wram_.size());
    r.ReadBytes(apuRam_.data(), apuRam_.size());
}

} // namespace AIO::Emulator::SNES
