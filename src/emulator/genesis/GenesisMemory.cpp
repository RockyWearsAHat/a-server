// GenesisMemory.cpp — Genesis M68K and Z80 bus dispatch.
// Reference: Charles MacDonald "Genesis Technical Overview".

#include "emulator/genesis/GenesisMemory.h"
#include "emulator/common/SaveState.h"
#include "emulator/genesis/GenesisCartridge.h"
#include "emulator/genesis/GenesisVDP.h"
#include "emulator/genesis/SN76489.h"
#include "emulator/genesis/YM2612.h"

namespace AIO::Emulator::Genesis {

void GenesisMemory::Init(GenesisCartridge* cart,
                         GenesisVDP*       vdp,
                         YM2612*           ym,
                         SN76489*          psg) noexcept {
    cart_ = cart;
    vdp_  = vdp;
    ym_   = ym;
    psg_  = psg;
}

// ── M68K read 8-bit ─────────────────────────────────────────────────────────
uint8_t GenesisMemory::M68KRead8(uint32_t addr) {
    addr &= 0xFFFFFF;

    // Main RAM mirror: 0xE00000–0xFFFFFF
    if (addr >= 0xE00000) {
        return ram_[addr & 0xFFFF];
    }

    // ROM: 0x000000–0x3FFFFF
    if (addr <= 0x3FFFFF) {
        return cart_ ? cart_->Read8(addr) : 0xFF;
    }

    // Z80 bus window: 0xA00000–0xA0FFFF
    if (addr >= 0xA00000 && addr <= 0xA0FFFF) {
        const uint16_t z80addr = static_cast<uint16_t>(addr & 0x7FFF);
        return z80Ram_[z80addr % kZ80RamSize];
    }

    // I/O and VDP range: 0xC00000–0xDFFFFF
    if (addr >= 0xC00000 && addr <= 0xDFFFFF) {
        const uint16_t w = M68KRead16(addr & ~1u);
        return (addr & 1) ? static_cast<uint8_t>(w) : static_cast<uint8_t>(w >> 8);
    }

    return 0xFF;
}

// ── M68K read 16-bit ────────────────────────────────────────────────────────
uint16_t GenesisMemory::M68KRead16(uint32_t addr) {
    addr &= 0xFFFFFE; // word-align

    // Main RAM mirror
    if (addr >= 0xE00000) {
        const uint32_t off = addr & 0xFFFE;
        return (static_cast<uint16_t>(ram_[off]) << 8) | ram_[off + 1];
    }

    // ROM
    if (addr <= 0x3FFFFF) {
        return cart_ ? cart_->Read16(addr) : 0xFFFF;
    }

    // Z80 busreq status
    if (addr == (kZ80BusReqAddr & ~1u)) {
        return 0x0100; // Z80 bus not granted (simplified)
    }

    // VDP ports
    if (addr >= 0xC00000 && addr <= 0xC0000F) {
        if (!vdp_) { return 0xFFFF; }
        switch (addr & 0xE) {
            case 0x0: case 0x2: return vdp_->ReadData();
            case 0x4: case 0x6: return vdp_->ReadStatus();
            case 0x8: case 0xA: return vdp_->ReadHVCounter();
            default:             return 0xFFFF;
        }
    }

    return 0xFFFF;
}

// ── M68K write 8-bit ────────────────────────────────────────────────────────
void GenesisMemory::M68KWrite8(uint32_t addr, uint8_t value) {
    addr &= 0xFFFFFF;

    if (addr >= 0xE00000) {
        ram_[addr & 0xFFFF] = value;
        return;
    }

    if (addr >= 0xA13000 && addr <= 0xA130FF) {
        if (cart_) { cart_->WriteBankReg(static_cast<uint8_t>(addr & 0xFF), value); }
        return;
    }

    // PSG write port (byte write only)
    if ((addr & 0xFFFF0F) == 0xC00011) {
        if (psg_) { psg_->Write(value); }
        return;
    }

    // For most byte writes to 16-bit peripherals, do word write
    uint16_t word = M68KRead16(addr & ~1u);
    if (addr & 1) { word = (word & 0xFF00) | value; }
    else          { word = (word & 0x00FF) | (static_cast<uint16_t>(value) << 8); }
    M68KWrite16(addr & ~1u, word);
}

// ── M68K write 16-bit ───────────────────────────────────────────────────────
void GenesisMemory::M68KWrite16(uint32_t addr, uint16_t value) {
    addr &= 0xFFFFFE;

    if (addr >= 0xE00000) {
        const uint32_t off = addr & 0xFFFE;
        ram_[off]     = static_cast<uint8_t>(value >> 8);
        ram_[off + 1] = static_cast<uint8_t>(value);
        return;
    }

    if (addr >= 0xC00000 && addr <= 0xC0000F) {
        if (!vdp_) { return; }
        switch (addr & 0xE) {
            case 0x0: case 0x2: vdp_->WriteData(value); return;
            case 0x4: case 0x6: vdp_->WriteCtrl(value); return;
            default:             return;
        }
    }

    // YM2612 (accessible from M68K space)
    if (addr >= 0xA04000 && addr <= 0xA04003) {
        if (ym_) {
            ym_->Write(static_cast<uint8_t>((addr & 2) | 0), static_cast<uint8_t>(value >> 8));
            ym_->Write(static_cast<uint8_t>((addr & 2) | 1), static_cast<uint8_t>(value));
        }
        return;
    }

    // Cart register writes (SSF2, SRAM enable)
    if (addr >= 0xA13000 && addr <= 0xA130FF) {
        if (cart_) { cart_->WriteBankReg(static_cast<uint8_t>(addr & 0xFF), static_cast<uint8_t>(value)); }
        return;
    }

    // Writes to ROM range: cart mapper
    if (addr <= 0x3FFFFF) {
        if (cart_) { cart_->Write16(addr, value); }
    }
}

// ── Z80 read 8-bit ──────────────────────────────────────────────────────────
uint8_t GenesisMemory::Z80Read8(uint16_t addr) {
    if (addr < kZ80RamSize) {
        return z80Ram_[addr];
    }
    if (addr >= 0x8000) {
        // 32 KB M68K bank window
        const uint32_t m68kAddr = z80BankBase_ | (addr & 0x7FFF);
        return M68KRead8(m68kAddr);
    }
    if (addr >= 0x4000 && addr <= 0x4003) {
        return ym_ ? ym_->Read() : 0xFF;
    }
    return 0xFF;
}

void GenesisMemory::Z80Write8(uint16_t addr, uint8_t value) {
    if (addr < kZ80RamSize) {
        z80Ram_[addr] = value;
        return;
    }
    if (addr >= 0x8000) {
        const uint32_t m68kAddr = z80BankBase_ | (addr & 0x7FFF);
        M68KWrite8(m68kAddr, value);
        return;
    }
    if (addr >= 0x4000 && addr <= 0x4003) {
        if (ym_) { ym_->Write(static_cast<uint8_t>(addr & 0x3), value); }
        return;
    }
    if (addr == 0x7F11) {
        if (psg_) { psg_->Write(value); }
        return;
    }
    // Z80 bank window register at 0x6000–0x60FF
    if (addr >= 0x6000 && addr <= 0x60FF) {
        // Shift bank register one bit per write (bit 0 of value)
        z80BankBase_ = ((z80BankBase_ >> 1) | (static_cast<uint32_t>(value & 1) << 23)) & 0xFF8000u;
    }
}

uint8_t GenesisMemory::Z80In8(uint8_t /*port*/) {
    return 0xFF;
}

void GenesisMemory::Z80Out8(uint8_t /*port*/, uint8_t /*value*/) {
    // No I/O-mapped devices on Genesis Z80.
}

void GenesisMemory::SaveState(AIO::Emulator::Common::SaveStateWriter& w) const {
    w.WriteBytes(ram_.data(),    ram_.size());
    w.WriteBytes(z80Ram_.data(), z80Ram_.size());
    w.WriteU32(z80BankBase_);
}

void GenesisMemory::LoadState(AIO::Emulator::Common::SaveStateReader& r) {
    r.ReadBytes(ram_.data(),    ram_.size());
    r.ReadBytes(z80Ram_.data(), z80Ram_.size());
    z80BankBase_ = r.ReadU32();
}

} // namespace AIO::Emulator::Genesis
