// GenesisCartridge.cpp — Sega Mega Drive ROM loader and bank mapper.
// Reference: Genesis Hardware Notes (Charles MacDonald), SSF2 mapper docs.

#include "emulator/genesis/GenesisCartridge.h"
#include "emulator/common/SaveState.h"
#include "emulator/genesis/GenesisConstants.h"
#include <algorithm>
#include <stdexcept>

namespace AIO::Emulator::Genesis {

void GenesisCartridge::Load(std::span<const uint8_t> data) {
    if (data.size() < 0x200) {
        throw std::runtime_error("Genesis ROM too small (minimum 512 bytes)");
    }
    if (data.size() > kMaxCartSize) {
        throw std::runtime_error("Genesis ROM exceeds 4 MB maximum");
    }

    rom_.assign(data.begin(), data.end());

    // ── Parse SRAM header (offsets 0x1B0–0x1B7) ────────────────────────────
    // Byte 0x1B0: "RA" magic, 0x1B2: code, 0x1B4–0x1B7: start/end addresses.
    if (rom_.size() > 0x1B8) {
        const uint8_t magic0 = rom_[0x1B0];
        const uint8_t magic1 = rom_[0x1B1];
        const uint8_t code   = rom_[0x1B2];
        if (magic0 == 'R' && magic1 == 'A' && (code & 0x40) != 0) {
            hasSram_ = true;
            sramStart_ = (static_cast<uint32_t>(rom_[0x1B4]) << 24) |
                         (static_cast<uint32_t>(rom_[0x1B5]) << 16) |
                         (static_cast<uint32_t>(rom_[0x1B6]) <<  8) |
                          static_cast<uint32_t>(rom_[0x1B7]);
            sramEnd_   = sramStart_ + 0x1FFF; // 8 KB SRAM default
            sram_.assign(0x2000, 0xFF);
        }
    }

    // ── Detect SSF2 mapper ─────────────────────────────────────────────────
    // SSF2 holds "SUPER STREET FIGHTER2 THE NEW CHALLENGERS" in the header.
    if (rom_.size() > 0x180) {
        constexpr uint8_t kSSF2[] = {'S','U','P','E','R',' ','S','T','R'};
        const bool match = std::equal(kSSF2, kSSF2 + sizeof(kSSF2),
                                      rom_.data() + 0x150);
        isSSF2_ = match;
    }

    // Default bank registers: each 512 KB slot maps to itself.
    for (uint8_t i = 0; i < 8; ++i) { bankRegs_[i] = i; }
}

uint32_t GenesisCartridge::MapAddress(uint32_t addr) const noexcept {
    if (isSSF2_) {
        // SSF2: 8 × 512 KB banks, mapped via bankRegs_.
        const uint32_t slot   = (addr >> 19) & 0x7;
        const uint32_t offset = addr & 0x7FFFF;
        const uint32_t mapped = (static_cast<uint32_t>(bankRegs_[slot]) << 19) | offset;
        return mapped & (static_cast<uint32_t>(rom_.size()) - 1);
    }
    return addr % static_cast<uint32_t>(rom_.size());
}

uint8_t GenesisCartridge::Read8(uint32_t addr) const noexcept {
    if (hasSram_ && sramEnabled_ && addr >= sramStart_ && addr <= sramEnd_) {
        return sram_[(addr - sramStart_) % sram_.size()];
    }
    return rom_[MapAddress(addr)];
}

uint16_t GenesisCartridge::Read16(uint32_t addr) const noexcept {
    const uint8_t hi = Read8(addr);
    const uint8_t lo = Read8(addr + 1);
    return (static_cast<uint16_t>(hi) << 8) | lo;
}

void GenesisCartridge::Write8(uint32_t addr, uint8_t value) noexcept {
    if (hasSram_ && sramEnabled_ && addr >= sramStart_ && addr <= sramEnd_) {
        sram_[(addr - sramStart_) % sram_.size()] = value;
    }
    // ROM writes are ignored for flat mapper; SSF2 handled via WriteBankReg.
}

void GenesisCartridge::Write16(uint32_t addr, uint16_t value) noexcept {
    Write8(addr,     static_cast<uint8_t>(value >> 8));
    Write8(addr + 1, static_cast<uint8_t>(value));
}

void GenesisCartridge::WriteBankReg(uint8_t reg, uint8_t bank) noexcept {
    if (isSSF2_ && reg < 8) {
        bankRegs_[reg] = bank;
    }
    // Handle SRAM enable: 0xA130F0 bit 0 = SRAM enable, bit 1 = write protect
    if (reg == 0) {
        sramEnabled_ = (bank & 0x01) != 0;
    }
}

void GenesisCartridge::SaveState(AIO::Emulator::Common::SaveStateWriter& w) const {
    w.WriteU32(static_cast<uint32_t>(sram_.size()));
    if (!sram_.empty()) {
        w.WriteBytes(sram_.data(), sram_.size());
    }
    w.WriteBool(hasSram_);
    w.WriteBool(sramEnabled_);
    w.WriteBool(isSSF2_);
    for (uint8_t reg : bankRegs_) { w.WriteU8(reg); }
}

void GenesisCartridge::LoadState(AIO::Emulator::Common::SaveStateReader& r) {
    const uint32_t sramSize = r.ReadU32();
    if (sramSize > 0) {
        sram_.resize(sramSize);
        r.ReadBytes(sram_.data(), sramSize);
    }
    hasSram_     = r.ReadBool();
    sramEnabled_ = r.ReadBool();
    isSSF2_      = r.ReadBool();
    for (uint8_t& reg : bankRegs_) { reg = r.ReadU8(); }
}

} // namespace AIO::Emulator::Genesis
