#include "emulator/snes/SNESCartridge.h"

#include "emulator/common/SaveState.h"
#include "emulator/snes/SNESConstants.h"

#include <stdexcept>

namespace AIO::Emulator::SNES {

void SNESCartridge::Load(std::span<const uint8_t> data) {
    if (data.size() < 0x8000) {
        throw std::runtime_error("SNES ROM too small");
    }
    if (data.size() > kRomMaxSize) {
        throw std::runtime_error("SNES ROM too large");
    }

    rom_.assign(data.begin(), data.end());

    if (sram_.empty()) {
        sram_.assign(0x8000, 0xFF);
    }
}

uint8_t SNESCartridge::Read8(uint32_t addr) const noexcept {
    if (rom_.empty()) {
        return 0xFF;
    }
    return rom_[addr % static_cast<uint32_t>(rom_.size())];
}

uint16_t SNESCartridge::Read16(uint32_t addr) const noexcept {
    const uint8_t lo = Read8(addr);
    const uint8_t hi = Read8(addr + 1);
    return static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8) | lo);
}

void SNESCartridge::Write8(uint32_t addr, uint8_t value) noexcept {
    if (!sram_.empty() && addr >= 0x700000u && addr <= 0x70FFFFu) {
        sram_[(addr - 0x700000u) % static_cast<uint32_t>(sram_.size())] = value;
    }
}

void SNESCartridge::Write16(uint32_t addr, uint16_t value) noexcept {
    Write8(addr, static_cast<uint8_t>(value));
    Write8(addr + 1, static_cast<uint8_t>(value >> 8));
}

void SNESCartridge::SaveState(AIO::Emulator::Common::SaveStateWriter& w) const {
    w.WriteU32(static_cast<uint32_t>(sram_.size()));
    if (!sram_.empty()) {
        w.WriteBytes(sram_.data(), sram_.size());
    }
}

void SNESCartridge::LoadState(AIO::Emulator::Common::SaveStateReader& r) {
    const uint32_t sramSize = r.ReadU32();
    sram_.assign(sramSize, 0xFF);
    if (!sram_.empty()) {
        r.ReadBytes(sram_.data(), sram_.size());
    }
}

} // namespace AIO::Emulator::SNES
