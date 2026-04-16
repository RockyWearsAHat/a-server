#include "emulator/atari2600/Atari2600Memory.h"
#include "emulator/atari2600/TIA.h"
#include "emulator/atari2600/PIA6532.h"
namespace Atari2600 {

Atari2600Memory::Atari2600Memory(TIA& tia, PIA6532& pia) noexcept
    : tia_(tia), pia_(pia), bankOffset_(0) {}

void Atari2600Memory::LoadROM(const std::vector<uint8_t>& rom) {
    rom_ = rom;
    bankOffset_ = 0;
}

// ─── Atari 2600 Memory Map (13-bit = 8 KB address space) ─────────────────────
//
//  0x0000–0x007F  TIA (64 write regs mirrored; 16 read regs)  — bit 12=0,7=0
//  0x0080–0x00FF  PIA RAM (128 bytes)                         — bit 9=0,7=1
//  0x0280–0x029F  PIA I/O & timer registers                   — bit 9=1,7=0
//  0x1000–0x1FFF  ROM (4 KB, large carts use bank switching)  — bit 12=1

uint8_t Atari2600Memory::Read8(uint16_t raw) {
    const uint16_t addr = raw & 0x1FFF; // 13-bit bus

    if (addr & 0x1000) {
        return CartRead(addr);
    }

    if (addr & 0x0200) {
        // PIA register / RAM space
        return pia_.Read(addr);
    }

    if (addr & 0x0080) {
        // PIA RAM (0x80–0xFF via direct read when A9=0)
        return pia_.Read(addr);
    }

    // TIA read (A12=0, A9=0, A7=0)
    return tia_.Read(addr);
}

void Atari2600Memory::Write8(uint16_t raw, uint8_t val) {
    const uint16_t addr = raw & 0x1FFF;

    if (addr & 0x1000) {
        CartWrite(addr, val);
        return;
    }

    if (addr & 0x0200) {
        pia_.Write(addr, val);
        return;
    }

    if (addr & 0x0080) {
        pia_.Write(addr, val);
        return;
    }

    // TIA write
    tia_.Write(addr, val);
}

uint8_t Atari2600Memory::Peek(uint16_t raw) const noexcept {
    const uint16_t addr = raw & 0x1FFF;
    if (addr & 0x1000) {
        if (rom_.empty()) return 0xFF;
        const uint32_t romAddr = (bankOffset_ * 0x1000u) + (addr & 0x0FFF);
        return rom_[romAddr % rom_.size()];
    }
    return 0;
}

// ─── Cartridge / Bank Switching ───────────────────────────────────────────────

uint8_t Atari2600Memory::CartRead(uint16_t addr) {
    if (rom_.empty()) return 0xFF;

    const uint16_t offset = addr & 0x0FFF;

    if (rom_.size() <= 0x1000) {
        // 4 KB or smaller — mirror into 4 KB window
        return rom_[offset % rom_.size()];
    }

    if (rom_.size() <= 0x2000) {
        // 8 KB — F8 bank switching: accesses to 0x1FF8/0x1FF9 switch banks
        if (offset == 0x0FF8) { bankOffset_ = 0; }
        if (offset == 0x0FF9) { bankOffset_ = 1; }
        const uint32_t romAddr = (static_cast<uint32_t>(bankOffset_) * 0x1000u) + offset;
        return rom_[romAddr % rom_.size()];
    }

    if (rom_.size() <= 0x4000) {
        // 16 KB — F6 bank switching: 0x1FF6–0x1FF9 select banks 0–3
        if (offset >= 0x0FF6 && offset <= 0x0FF9) { bankOffset_ = static_cast<uint8_t>(offset - 0x0FF6); }
        const uint32_t romAddr = (static_cast<uint32_t>(bankOffset_) * 0x1000u) + offset;
        return rom_[romAddr % rom_.size()];
    }

    // Larger carts — map first bank at reset, simple linear access
    const uint32_t romAddr = (static_cast<uint32_t>(bankOffset_) * 0x1000u) + offset;
    return rom_[romAddr % rom_.size()];
}

void Atari2600Memory::CartWrite(uint16_t addr, uint8_t val) {
    // Some carts use writes to ROM space for bank switching (e.g. Pitfall II / DF)
    const uint16_t offset = addr & 0x0FFF;
    (void)val;

    if (rom_.size() <= 0x2000) {
        if (offset == 0x0FF8) { bankOffset_ = 0; }
        if (offset == 0x0FF9) { bankOffset_ = 1; }
    } else if (rom_.size() <= 0x4000) {
        if (offset >= 0x0FF6 && offset <= 0x0FF9) { bankOffset_ = static_cast<uint8_t>(offset - 0x0FF6); }
    }
}

} // namespace Atari2600
