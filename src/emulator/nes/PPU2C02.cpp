#include "emulator/nes/PPU2C02.h"
#include "emulator/nes/NESCartridge.h"
#include "emulator/common/SaveState.h"
#include "emulator/nes/NESConstants.h"
#include <cstdint>

namespace AIO::Emulator::NES {

// NTSC colour palette — 64 entries, RGBA888 stored as 0xRRGGBBFF
static constexpr uint32_t kNtscPalette[64] = {
    0x626262FF, 0x002E98FF, 0x0C11BAFF, 0x3B00A4FF,
    0x620064FF, 0x750020FF, 0x730000FF, 0x4C1300FF,
    0x1D3400FF, 0x004E00FF, 0x005800FF, 0x004B1CFF,
    0x003870FF, 0x000000FF, 0x000000FF, 0x000000FF,
    0xABABABFF, 0x1462EAFF, 0x3B28FFFF, 0x7717F8FF,
    0xB00BCCFF, 0xC80677FF, 0xC71720FF, 0x9E3600FF,
    0x655D00FF, 0x208000FF, 0x009100FF, 0x008343FF,
    0x006CA6FF, 0x000000FF, 0x000000FF, 0x000000FF,
    0xFFFFFFFF, 0x5EBCFFFF, 0x8F8AFFFF, 0xC470FFFF,
    0xF765FFFF, 0xFF6CB3FF, 0xFF7666FF, 0xF49320FF,
    0xB9BA00FF, 0x6ADE00FF, 0x3BEB00FF, 0x28E46AFF,
    0x26CDD4FF, 0x2E2E2EFF, 0x000000FF, 0x000000FF,
    0xFFFFFFFF, 0xBCECFFFF, 0xD1D8FFFF, 0xE9C8FFFF,
    0xFFBFFFFF, 0xFFC0E0FF, 0xFFC8BAFF, 0xF7D899FF,
    0xE9E681FF, 0xC8F381FF, 0xB0F9B0FF, 0xAFFAD5FF,
    0xACF0EFFF, 0xB8B8B8FF, 0x000000FF, 0x000000FF,
};

PPU2C02::PPU2C02(NESCartridge& cartridge) : cart_(cartridge) {
    framebuffer_.fill(0xFF000000u);
}

void PPU2C02::Reset() {
    ctrl_      = 0;
    mask_      = 0;
    status_    = 0;
    oamAddr_   = 0;
    dataReadBuf_ = 0;
    v_ = t_ = 0;
    fx_ = 0;
    w_ = false;
    scanline_   = 0;
    cycle_      = 0;
    oddFrame_   = false;
    inVblank_   = false;
    frameCount_ = 0;
    vram_.fill(0);
    palette_.fill(0);
    oam_.fill(0);
    framebuffer_.fill(0xFF000000u);
}

void PPU2C02::SetNmiCallback(NmiCallback cb) {
    onNmi_ = std::move(cb);
}

const uint32_t* PPU2C02::GetFramebuffer() const noexcept {
    return framebuffer_.data();
}

// ── Register interface ────────────────────────────────────────────────────

uint8_t PPU2C02::ReadRegister(uint8_t reg) {
    switch (reg) {
        case 2: { // PPUSTATUS
            const uint8_t ret = (status_ & 0xE0) | (dataReadBuf_ & 0x1F);
            status_ &= ~0x80; // Clear VBlank flag on read
            w_ = false;
            return ret;
        }
        case 4: // OAMDATA
            return oam_[oamAddr_];
        case 7: { // PPUDATA
            uint8_t ret = dataReadBuf_;
            dataReadBuf_ = ReadVRAM(v_);
            if ((v_ & 0x3FFF) >= 0x3F00) {
                // Palette reads are immediate; buffer holds mirrored nametable
                ret = dataReadBuf_;
                dataReadBuf_ = ReadVRAM(v_ - 0x1000);
            }
            v_ += (ctrl_ & 0x04) ? 32 : 1;
            return ret;
        }
        default:
            return 0xFF; // open bus
    }
}

void PPU2C02::WriteRegister(uint8_t reg, uint8_t value) {
    switch (reg) {
        case 0: // PPUCTRL
            ctrl_ = value;
            // Nametable bits feed into t_ bits 10-11
            t_ = (t_ & 0xF3FF) | (static_cast<uint16_t>(value & 0x03) << 10);
            break;
        case 1: // PPUMASK
            mask_ = value;
            break;
        case 3: // OAMADDR
            oamAddr_ = value;
            break;
        case 4: // OAMDATA
            oam_[oamAddr_++] = value;
            break;
        case 5: // PPUSCROLL
            if (!w_) {
                fx_ = value & 0x07;
                t_ = (t_ & 0xFFE0) | (value >> 3);
            } else {
                t_ = (t_ & 0x8C1F) |
                     (static_cast<uint16_t>(value & 0xF8) << 2) |
                     (static_cast<uint16_t>(value & 0x07) << 12);
            }
            w_ = !w_;
            break;
        case 6: // PPUADDR
            if (!w_) {
                t_ = (t_ & 0x00FF) | (static_cast<uint16_t>(value & 0x3F) << 8);
            } else {
                t_ = (t_ & 0xFF00) | value;
                v_ = t_;
            }
            w_ = !w_;
            break;
        case 7: // PPUDATA
            WriteVRAM(v_, value);
            v_ += (ctrl_ & 0x04) ? 32 : 1;
            break;
        default:
            break;
    }
}

void PPU2C02::OamDma(const uint8_t* sourcePage) {
    for (int i = 0; i < 256; ++i) {
        oam_[(oamAddr_ + i) & 0xFF] = sourcePage[i];
    }
}

// ── Tick ──────────────────────────────────────────────────────────────────

void PPU2C02::Tick(uint32_t totalCycles) {
    for (uint32_t i = 0; i < totalCycles; ++i) {
        // Pre-render scanline
        if (scanline_ == static_cast<int>(kPreRenderScanline)) {
            TickPreRender();
        }
        // Visible scanlines
        else if (scanline_ < static_cast<int>(kVisibleScanlines)) {
            TickVisible();
        }
        // VBlank start
        else if (scanline_ == static_cast<int>(kVBlankScanline) && cycle_ == 1) {
            status_ |= 0x80;  // Set VBlank
            inVblank_ = true;
            if ((ctrl_ & 0x80) && onNmi_) {
                onNmi_();
            }
        }

        ++cycle_;
        if (cycle_ >= static_cast<int>(kPpuCyclesPerScanline)) {
            cycle_ = 0;
            ++scanline_;
            if (scanline_ > static_cast<int>(kPreRenderScanline)) {
                scanline_ = 0;
                oddFrame_ = !oddFrame_;
                ++frameCount_;
            }
        }
    }
}

void PPU2C02::TickPreRender() {
    if (cycle_ == 1) {
        status_ &= ~0xE0; // Clear VBlank, S0 hit, overflow
        inVblank_ = false;
    }

    if (RenderingEnabled()) {
        if (cycle_ >= 280 && cycle_ <= 304) {
            TransferAddressY();
        }
        if (cycle_ > 0 && (cycle_ <= 256 || (cycle_ >= 321 && cycle_ <= 340))) {
            UpdateShifters();
        }
        if ((cycle_ >= 1 && cycle_ <= 256) || (cycle_ >= 321 && cycle_ <= 336)) {
            switch (cycle_ & 0x07) {
                case 1: bgNextTileId_ = ReadVRAM(0x2000 | (v_ & 0x0FFF)); break;
                case 3: {
                    const uint16_t attrAddr = 0x23C0 | (v_ & 0x0C00) |
                                              ((v_ >> 4) & 0x38) | ((v_ >> 2) & 0x07);
                    bgNextAttr_ = ReadVRAM(attrAddr);
                    if ((v_ & 0x0002) != 0) bgNextAttr_ >>= 2;
                    if ((v_ & 0x0040) != 0) bgNextAttr_ >>= 4;
                    bgNextAttr_ &= 0x03;
                    break;
                }
                case 5: {
                    const uint16_t baseAddr = (ctrl_ & 0x10) ? 0x1000 : 0x0000;
                    const uint16_t fineY = (v_ >> 12) & 0x07;
                    bgNextPatLo_ = ReadVRAM(baseAddr + bgNextTileId_ * 16 + fineY);
                    break;
                }
                case 7: {
                    const uint16_t baseAddr = (ctrl_ & 0x10) ? 0x1000 : 0x0000;
                    const uint16_t fineY = (v_ >> 12) & 0x07;
                    bgNextPatHi_ = ReadVRAM(baseAddr + bgNextTileId_ * 16 + fineY + 8);
                    break;
                }
                case 0:
                    LoadBackgroundShifters();
                    IncrementScrollX();
                    break;
                default: break;
            }
        }
        if (cycle_ == 256) IncrementScrollY();
        if (cycle_ == 257) TransferAddressX();
        // Skip idle dot on odd frames
        if (cycle_ == 339 && oddFrame_) cycle_++;
    }
}

void PPU2C02::TickVisible() {
    if (!RenderingEnabled()) {
        return;
    }

    // Background rendering: fetch tile data
    if (cycle_ > 0 && cycle_ <= 256) {
        UpdateShifters();

        switch (cycle_ & 0x07) {
            case 1: bgNextTileId_ = ReadVRAM(0x2000 | (v_ & 0x0FFF)); break;
            case 3: {
                const uint16_t attrAddr = 0x23C0 | (v_ & 0x0C00) |
                                          ((v_ >> 4) & 0x38) | ((v_ >> 2) & 0x07);
                bgNextAttr_ = ReadVRAM(attrAddr);
                if ((v_ & 0x0002) != 0) bgNextAttr_ >>= 2;
                if ((v_ & 0x0040) != 0) bgNextAttr_ >>= 4;
                bgNextAttr_ &= 0x03;
                break;
            }
            case 5: {
                const uint16_t baseAddr = (ctrl_ & 0x10) ? 0x1000 : 0x0000;
                const uint16_t fineY = (v_ >> 12) & 0x07;
                bgNextPatLo_ = ReadVRAM(baseAddr + bgNextTileId_ * 16 + fineY);
                break;
            }
            case 7: {
                const uint16_t baseAddr = (ctrl_ & 0x10) ? 0x1000 : 0x0000;
                const uint16_t fineY = (v_ >> 12) & 0x07;
                bgNextPatHi_ = ReadVRAM(baseAddr + bgNextTileId_ * 16 + fineY + 8);
                break;
            }
            case 0:
                LoadBackgroundShifters();
                IncrementScrollX();
                break;
            default: break;
        }

        // Output pixel
        const int px = cycle_ - 1;
        const int py = scanline_;

        uint8_t bgPixel     = 0;
        uint8_t bgPalette   = 0;
        uint8_t fgPixel     = 0;
        uint8_t fgPalette   = 0;
        bool     fgPriority = false;

        if (mask_ & 0x08) {
            const uint16_t mux = 0x8000u >> fx_;
            const uint8_t  p0  = (bgShiftPatLo_  & mux) ? 1 : 0;
            const uint8_t  p1  = (bgShiftPatHi_  & mux) ? 2 : 0;
            const uint8_t  a0  = (bgShiftAttrLo_ & mux) ? 1 : 0;
            const uint8_t  a1  = (bgShiftAttrHi_ & mux) ? 2 : 0;
            bgPixel   = p0 | p1;
            bgPalette = a0 | a1;
        }

        if (mask_ & 0x10) {
            for (uint8_t s = 0; s < spriteCount_; ++s) {
                if (spriteX_[s] != 0) continue;
                const uint8_t sp0 = (spriteShiftPatLo_[s] & 0x80) ? 1 : 0;
                const uint8_t sp1 = (spriteShiftPatHi_[s] & 0x80) ? 2 : 0;
                fgPixel   = sp0 | sp1;
                fgPalette = (spriteAttr_[s] & 0x03) + 4;
                fgPriority= !(spriteAttr_[s] & 0x20);
                if (fgPixel != 0) break;
            }
        }

        uint8_t pixel   = 0;
        uint8_t palette = 0;
        if (bgPixel == 0 && fgPixel == 0) {
            pixel = 0; palette = 0;
        } else if (bgPixel != 0 && fgPixel == 0) {
            pixel = bgPixel; palette = bgPalette;
        } else if (bgPixel == 0 && fgPixel != 0) {
            pixel = fgPixel; palette = fgPalette;
        } else {
            pixel   = fgPriority ? fgPixel   : bgPixel;
            palette = fgPriority ? fgPalette : bgPalette;
            if (spriteZeroBeingRendered_ && spriteZeroHitPossible_ && px < 255) {
                status_ |= 0x40; // Sprite 0 hit
            }
        }

        const uint8_t paletteEntry = ReadVRAM(0x3F00 + palette * 4 + pixel) & 0x3F;
        framebuffer_[py * kVisibleDotsPerLine + px] = GetNtscColor(paletteEntry);
    }

    if (cycle_ == 256) {
        IncrementScrollY();
        SpriteEvaluation();
    }
    if (cycle_ == 257) TransferAddressX();

    // Prefetch scanline 0 tile data
    if (cycle_ >= 321 && cycle_ <= 340) {
        UpdateShifters();
        switch (cycle_ & 0x07) {
            case 1: bgNextTileId_ = ReadVRAM(0x2000 | (v_ & 0x0FFF)); break;
            case 3: {
                const uint16_t attrAddr = 0x23C0 | (v_ & 0x0C00) |
                                          ((v_ >> 4) & 0x38) | ((v_ >> 2) & 0x07);
                bgNextAttr_ = ReadVRAM(attrAddr) >> ((v_ & 0x02) ? 2 : 0) >> ((v_ & 0x40) ? 4 : 0);
                bgNextAttr_ &= 0x03;
                break;
            }
            case 5: {
                const uint16_t fineY = (v_ >> 12) & 0x07;
                bgNextPatLo_ = ReadVRAM(((ctrl_ & 0x10) ? 0x1000 : 0x0000) + bgNextTileId_ * 16 + fineY);
                break;
            }
            case 7: {
                const uint16_t fineY = (v_ >> 12) & 0x07;
                bgNextPatHi_ = ReadVRAM(((ctrl_ & 0x10) ? 0x1000 : 0x0000) + bgNextTileId_ * 16 + fineY + 8);
                break;
            }
            case 0: LoadBackgroundShifters(); IncrementScrollX(); break;
            default: break;
        }
    }
}

void PPU2C02::TickVBlank() {
    // VBlank monitoring handled in Tick() main loop
}

// ── Background helpers ────────────────────────────────────────────────────

void PPU2C02::LoadBackgroundShifters() {
    bgShiftPatLo_  = (bgShiftPatLo_  & 0xFF00) | bgNextPatLo_;
    bgShiftPatHi_  = (bgShiftPatHi_  & 0xFF00) | bgNextPatHi_;
    bgShiftAttrLo_ = (bgShiftAttrLo_ & 0xFF00) | ((bgNextAttr_ & 0x01) ? 0xFF : 0x00);
    bgShiftAttrHi_ = (bgShiftAttrHi_ & 0xFF00) | ((bgNextAttr_ & 0x02) ? 0xFF : 0x00);
}

void PPU2C02::UpdateShifters() {
    if (mask_ & 0x08) {
        bgShiftPatLo_  <<= 1;
        bgShiftPatHi_  <<= 1;
        bgShiftAttrLo_ <<= 1;
        bgShiftAttrHi_ <<= 1;
    }
    if ((mask_ & 0x10) && cycle_ >= 1 && cycle_ < 258) {
        for (uint8_t s = 0; s < spriteCount_; ++s) {
            if (spriteX_[s] > 0) {
                --spriteX_[s];
            } else {
                spriteShiftPatLo_[s] <<= 1;
                spriteShiftPatHi_[s] <<= 1;
            }
        }
    }
}

void PPU2C02::IncrementScrollX() {
    if (!RenderingEnabled()) return;
    if ((v_ & 0x001F) == 31) {
        v_ &= ~0x001F;
        v_ ^= 0x0400; // toggle nametable
    } else {
        v_++;
    }
}

void PPU2C02::IncrementScrollY() {
    if (!RenderingEnabled()) return;
    if ((v_ & 0x7000) != 0x7000) {
        v_ += 0x1000;
    } else {
        v_ &= ~0x7000;
        uint16_t y = (v_ & 0x03E0) >> 5;
        if (y == 29) {
            y = 0;
            v_ ^= 0x0800;
        } else if (y == 31) {
            y = 0;
        } else {
            y++;
        }
        v_ = (v_ & ~0x03E0) | (y << 5);
    }
}

void PPU2C02::TransferAddressX() {
    if (!RenderingEnabled()) return;
    v_ = (v_ & 0xFBE0) | (t_ & 0x041F);
}

void PPU2C02::TransferAddressY() {
    if (!RenderingEnabled()) return;
    v_ = (v_ & 0x841F) | (t_ & 0x7BE0);
}

// ── Sprite evaluation ─────────────────────────────────────────────────────

void PPU2C02::SpriteEvaluation() {
    spriteCount_ = 0;
    spriteZeroHitPossible_     = false;
    spriteZeroBeingRendered_   = false;
    spriteShiftPatLo_.fill(0);
    spriteShiftPatHi_.fill(0);

    const uint8_t spriteHeight = (ctrl_ & 0x20) ? 16 : 8;

    for (uint8_t i = 0; i < 64 && spriteCount_ < 8; ++i) {
        const int   diff = scanline_ - oam_[i * 4];
        if (diff >= 0 && diff < spriteHeight) {
            if (i == 0) spriteZeroHitPossible_ = true;
            if (spriteCount_ < 8) {
                spriteScanline_[spriteCount_ * 4 + 0] = oam_[i * 4 + 0];
                spriteScanline_[spriteCount_ * 4 + 1] = oam_[i * 4 + 1];
                spriteScanline_[spriteCount_ * 4 + 2] = oam_[i * 4 + 2];
                spriteScanline_[spriteCount_ * 4 + 3] = oam_[i * 4 + 3];
            }

            // Load pattern data
            uint8_t tileId      = spriteScanline_[spriteCount_ * 4 + 1];
            uint8_t attr        = spriteScanline_[spriteCount_ * 4 + 2];
            uint8_t xPos        = spriteScanline_[spriteCount_ * 4 + 3];
            uint8_t row         = static_cast<uint8_t>(diff);
            const bool flipV = (attr & 0x80) != 0;
            const bool flipH = (attr & 0x40) != 0;

            uint16_t patAddr = 0;
            if (spriteHeight == 8) {
                if (flipV) row = 7 - row;
                const uint16_t base = (ctrl_ & 0x08) ? 0x1000 : 0x0000;
                patAddr = base + tileId * 16 + row;
            } else {
                if (flipV) row = 15 - row;
                const uint16_t base = (tileId & 0x01) ? 0x1000 : 0x0000;
                tileId &= 0xFE;
                if (row >= 8) { tileId++; row -= 8; }
                patAddr = base + tileId * 16 + row;
            }

            uint8_t patLo = ReadVRAM(patAddr);
            uint8_t patHi = ReadVRAM(patAddr + 8);

            if (flipH) {
                // Reverse bits
                auto rev = [](uint8_t b) -> uint8_t {
                    b = ((b & 0xF0) >> 4) | ((b & 0x0F) << 4);
                    b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2);
                    b = ((b & 0xAA) >> 1) | ((b & 0x55) << 1);
                    return b;
                };
                patLo = rev(patLo);
                patHi = rev(patHi);
            }

            spriteShiftPatLo_[spriteCount_] = patLo;
            spriteShiftPatHi_[spriteCount_] = patHi;
            spriteAttr_[spriteCount_]       = attr;
            spriteX_[spriteCount_]          = xPos;

            if (i == 0 && spriteZeroHitPossible_) {
                spriteZeroBeingRendered_ = true;
            }

            ++spriteCount_;
        }
    }
}

// ── VRAM helpers ──────────────────────────────────────────────────────────

uint8_t PPU2C02::ReadVRAM(uint16_t addr) const {
    addr &= 0x3FFF;
    if (addr < 0x2000) {
        return cart_.PpuRead(addr);
    }
    if (addr < 0x3F00) {
        return vram_[MirrorNametableAddr(addr)];
    }
    // Palette RAM
    addr &= 0x1F;
    if (addr == 0x10 || addr == 0x14 || addr == 0x18 || addr == 0x1C) {
        addr &= 0x0F;
    }
    return palette_[addr] & ((mask_ & 0x01) ? 0x30 : 0x3F);
}

void PPU2C02::WriteVRAM(uint16_t addr, uint8_t value) {
    addr &= 0x3FFF;
    if (addr < 0x2000) {
        cart_.PpuWrite(addr, value);
        return;
    }
    if (addr < 0x3F00) {
        vram_[MirrorNametableAddr(addr)] = value;
        return;
    }
    addr &= 0x1F;
    if (addr == 0x10 || addr == 0x14 || addr == 0x18 || addr == 0x1C) {
        addr &= 0x0F;
    }
    palette_[addr] = value;
}

uint16_t PPU2C02::MirrorNametableAddr(uint16_t addr) const {
    addr = (addr - 0x2000) & 0x0FFF; // 0–4095
    switch (cart_.GetMirrorMode()) {
        case MirrorMode::Horizontal:
            return ((addr & 0x0800) ? 0x0400 : 0x0000) + (addr & 0x03FF);
        case MirrorMode::Vertical:
            return addr & 0x07FF;
        case MirrorMode::SingleLow:
            return addr & 0x03FF;
        case MirrorMode::SingleHigh:
            return 0x0400 + (addr & 0x03FF);
        default:
            return addr & 0x0FFF; // Four-screen
    }
}

uint32_t PPU2C02::GetNtscColor(uint8_t paletteIndex) const {
    return kNtscPalette[paletteIndex & 0x3F];
}

// ── Save state ────────────────────────────────────────────────────────────

void PPU2C02::SaveState(Common::SaveStateWriter& w) const {
    w.WriteU8(ctrl_);
    w.WriteU8(mask_);
    w.WriteU8(status_);
    w.WriteU8(oamAddr_);
    w.WriteU8(dataReadBuf_);
    w.WriteU16(v_);
    w.WriteU16(t_);
    w.WriteU8(fx_);
    w.WriteBool(w_);
    w.WriteU32(scanline_);
    w.WriteU32(cycle_);
    w.WriteBool(oddFrame_);
    w.WriteBool(inVblank_);
    w.WriteU64(frameCount_);
    w.WriteBytes(vram_.data(), vram_.size());
    w.WriteBytes(palette_.data(), palette_.size());
    w.WriteBytes(oam_.data(), oam_.size());
}

void PPU2C02::LoadState(Common::SaveStateReader& r) {
    ctrl_         = r.ReadU8();
    mask_         = r.ReadU8();
    status_       = r.ReadU8();
    oamAddr_      = r.ReadU8();
    dataReadBuf_  = r.ReadU8();
    v_            = r.ReadU16();
    t_            = r.ReadU16();
    fx_           = r.ReadU8();
    w_            = r.ReadBool();
    scanline_     = static_cast<int>(r.ReadU32());
    cycle_        = static_cast<int>(r.ReadU32());
    oddFrame_     = r.ReadBool();
    inVblank_     = r.ReadBool();
    frameCount_   = r.ReadU64();
    r.ReadBytes(vram_.data(), vram_.size());
    r.ReadBytes(palette_.data(), palette_.size());
    r.ReadBytes(oam_.data(), oam_.size());
}

} // namespace AIO::Emulator::NES
