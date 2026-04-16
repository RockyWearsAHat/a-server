#include "emulator/atari2600/TIA.h"
#include <cstring>

namespace Atari2600 {

TIA::TIA() noexcept {
    Reset();
}

void TIA::Reset() noexcept {
    colorClock_      = 0;
    scanline_        = 0;
    frameReady_      = false;
    wsyncRequested_  = false;
    vsync_           = false;
    vblank_          = false;
    framebuffer_.fill(0xFF000000u); // Black
}

// ─── NTSC Colour LUT (128 entries, 2 shades each = luma) ─────────────────────
// Approximated from the 2600 NTSC colour chart by Kevin Horton.
uint32_t TIA::NTSCColor(uint8_t v) noexcept {
    // v[7:1] = colour+luma index, v[0] unused
    static constexpr uint32_t kPalette[128] = {
        0xFF000000,0xFF404040,0xFF6C6C6C,0xFF909090,0xFFB0B0B0,0xFFC8C8C8,0xFFDCDCDC,0xFFECECEC,
        0xFF444400,0xFF646410,0xFF848424,0xFFA0A034,0xFFB8B840,0xFFD0D050,0xFFE8E85C,0xFFFCFC68,
        0xFF702800,0xFF844414,0xFF985C28,0xFFAC7840,0xFFC08C4C,0xFFD4A45C,0xFFE8BC70,0xFFFCD484,
        0xFF841800,0xFF983418,0xFFAC5030,0xFFC06848,0xFFD4805C,0xFFE89870,0xFFFCB484,0xFFFFCC98,
        0xFF880000,0xFF9C2020,0xFFB03C3C,0xFFC05858,0xFFD07070,0xFFE08888,0xFFF0A0A0,0xFFFFB4B4,
        0xFF78005C,0xFF8C2074,0xFFA03C88,0xFFB4589C,0xFFC870B0,0xFFDC84C0,0xFFEE9CD4,0xFFFFB0E8,
        0xFF480078,0xFF60208C,0xFF7840A0,0xFF8C5CB4,0xFFA07CC8,0xFFB494DC,0xFFC8ACEC,0xFFDCC4FC,
        0xFF140084,0xFF302098,0xFF4C3CAC,0xFF6858C0,0xFF8070D4,0xFF9488E8,0xFFA8A0F8,0xFFBCB8FF,
        0xFF000088,0xFF20209C,0xFF3C3CB0,0xFF5858C4,0xFF7070D8,0xFF8888EC,0xFFA0A0FC,0xFFB4B8FF,
        0xFF00187C,0xFF1C3890,0xFF3854A8,0xFF5470BC,0xFF6C8CD0,0xFF84A4E4,0xFF9CBCF8,0xFFB0D4FF,
        0xFF002C5C,0xFF1C4C78,0xFF386890,0xFF5484A8,0xFF6C9CBC,0xFF84B4D4,0xFF9CCCE8,0xFFB0E4FC,
        0xFF003C2C,0xFF1C5C48,0xFF387C64,0xFF549C80,0xFF6CB898,0xFF84D0B0,0xFF9CE8C8,0xFFB0FCE0,
        0xFF003C00,0xFF205C20,0xFF407C40,0xFF5C9C5C,0xFF74B474,0xFF8CCC8C,0xFFA4E4A4,0xFFBCFCBC,
        0xFF183800,0xFF385814,0xFF548030,0xFF6C9C4C,0xFF84B464,0xFF9CCC7C,0xFFB4E494,0xFFCCFCAC,
        0xFF2C3000,0xFF4C4C18,0xFF686830,0xFF848448,0xFF9C9C60,0xFFB4B478,0xFFCCCC8C,0xFFE0E0A0,
        0xFF442800,0xFF644818,0xFF846834,0xFFA08450,0xFFBC9C6C,0xFFD4B484,0xFFECCC9C,0xFFFFE0B0,
    };
    return kPalette[(v >> 1) & 0x7F];
}

// ─── Register Access ──────────────────────────────────────────────────────────

uint8_t TIA::Read(uint16_t addr) noexcept {
    switch (addr & 0x0F) {
        case 0x00: return cxm0p_;
        case 0x01: return cxm1p_;
        case 0x02: return cxp0fb_;
        case 0x03: return cxp1fb_;
        case 0x04: return cxm0fb_;
        case 0x05: return cxm1fb_;
        case 0x06: return cxblpf_;
        case 0x07: return cxppmm_;
        case 0x0C: return inpt4_;
        case 0x0D: return inpt5_;
        default:   return 0;
    }
}

void TIA::Write(uint16_t addr, uint8_t v) noexcept {
    switch (addr & 0x3F) {
        case 0x00: // VSYNC
            vsync_ = (v & 0x02) != 0;
            break;
        case 0x01: // VBLANK
            vblank_  = (v & 0x02) != 0;
            inpt4_   = (v & 0x40) ? 0x00 : 0x80; // Latch bit 6 → dump input
            inpt5_   = (v & 0x40) ? 0x00 : 0x80;
            break;
        case 0x02: // WSYNC
            wsyncRequested_ = true;
            break;
        case 0x03: // RSYNC — reset horizontal counter (rare, ignored here)
            colorClock_ = 0;
            break;
        case 0x04: nusiz0_ = v; break;
        case 0x05: nusiz1_ = v; break;
        case 0x06: colup0_ = v; break;
        case 0x07: colup1_ = v; break;
        case 0x08: colupf_ = v; break;
        case 0x09: colubk_ = v; break;
        case 0x0A: ctrlpf_ = v; break;
        case 0x0B: refp0_  = v; break;
        case 0x0C: refp1_  = v; break;
        case 0x0D: pf0_    = v; break;
        case 0x0E: pf1_    = v; break;
        case 0x0F: pf2_    = v; break;
        case 0x10: resp0_  = static_cast<uint8_t>(colorClock_ > kHBlankClocks ? colorClock_ - kHBlankClocks : 0); break; // RESP0
        case 0x11: resp1_  = static_cast<uint8_t>(colorClock_ > kHBlankClocks ? colorClock_ - kHBlankClocks : 0); break; // RESP1
        case 0x12: resm0_  = static_cast<uint8_t>(colorClock_ > kHBlankClocks ? colorClock_ - kHBlankClocks : 0); break; // RESM0
        case 0x13: resm1_  = static_cast<uint8_t>(colorClock_ > kHBlankClocks ? colorClock_ - kHBlankClocks : 0); break; // RESM1
        case 0x14: resbl_  = static_cast<uint8_t>(colorClock_ > kHBlankClocks ? colorClock_ - kHBlankClocks : 0); break; // RESBL
        case 0x1B: grp0_old_ = grp0_; grp0_ = v; break; // GRP0
        case 0x1C: grp1_old_ = grp1_; grp1_ = v; break; // GRP1
        case 0x1D: enam0_ = v; break;
        case 0x1E: enam1_ = v; break;
        case 0x1F: enabl_ = v; break;
        case 0x20: hmp0_  = v; break;
        case 0x21: hmp1_  = v; break;
        case 0x22: hmm0_  = v; break;
        case 0x23: hmm1_  = v; break;
        case 0x24: hmbl_  = v; break;
        case 0x25: vdelp0_ = v; break;
        case 0x26: vdelp1_ = v; break;
        case 0x27: vdelbl_ = v; break;
        case 0x2A: // HMOVE — apply horizontal motion
            resp0_ = static_cast<uint8_t>((resp0_ - static_cast<int8_t>(hmp0_ >> 4) + kWidth) % kWidth);
            resp1_ = static_cast<uint8_t>((resp1_ - static_cast<int8_t>(hmp1_ >> 4) + kWidth) % kWidth);
            resm0_ = static_cast<uint8_t>((resm0_ - static_cast<int8_t>(hmm0_ >> 4) + kWidth) % kWidth);
            resm1_ = static_cast<uint8_t>((resm1_ - static_cast<int8_t>(hmm1_ >> 4) + kWidth) % kWidth);
            resbl_ = static_cast<uint8_t>((resbl_ - static_cast<int8_t>(hmbl_ >> 4) + kWidth) % kWidth);
            break;
        case 0x2B: // HMCLR
            hmp0_ = hmp1_ = hmm0_ = hmm1_ = hmbl_ = 0;
            break;
        case 0x2C: // CXCLR — clear collision latches
            cxm0p_ = cxm1p_ = cxp0fb_ = cxp1fb_ = 0;
            cxm0fb_ = cxm1fb_ = cxblpf_ = cxppmm_ = 0;
            break;
        default: break;
    }
}

// ─── Sprite pixel helpers ─────────────────────────────────────────────────────

bool TIA::PlayfieldBit(int x) const noexcept {
    // Playfield is 40 bits wide displayed twice (160 pixels total)
    // PF0 bits 4–7 drive pixels 0–3 (left half)
    // PF1 bits 7–0 drive pixels 4–11
    // PF2 bits 0–7 drive pixels 12–19
    const int half = x % 80;
    bool bit = false;
    if (half < 4)       bit = (pf0_ >> (4 + half)) & 1;
    else if (half < 12) bit = (pf1_ >> (7 - (half - 4))) & 1;
    else                bit = (pf2_ >>       (half - 12)) & 1;

    if (x >= 80 && (ctrlpf_ & 0x01)) {
        // Mirror right half
        const int rHalf = 79 - (x - 80);
        if (rHalf < 4)       bit = (pf0_ >> (4 + rHalf)) & 1;
        else if (rHalf < 12) bit = (pf1_ >> (7 - (rHalf - 4))) & 1;
        else                 bit = (pf2_ >>       (rHalf - 12)) & 1;
    } else if (x >= 80) {
        bit = (pf0_ >> (4 + (x - 80))) & 1;
        if      (x - 80 < 4)  bit = (pf0_ >> (4 + (x - 80))) & 1;
        else if (x - 80 < 12) bit = (pf1_ >> (7 - (x - 80 - 4))) & 1;
        else                  bit = (pf2_ >> (x - 80 - 12)) & 1;
    }
    return bit;
}

bool TIA::PlayerBit(int x, uint8_t grp, uint8_t pos, uint8_t nusiz, bool reflect) const noexcept {
    const int copies = nusiz & 0x07;
    // Copies: 0=one, 1=two close, 2=two med, 3=three close, 4=two wide, 6=three med
    auto testPos = [&](int offset) -> bool {
        int dx = x - offset;
        if (dx < 0 || dx >= 8) return false;
        uint8_t bit = reflect ? (1 << dx) : (0x80 >> dx);
        return (grp & bit) != 0;
    };
    if (testPos(pos)) return true;
    if (copies == 1 || copies == 3) {
        if (testPos(pos + 16)) return true;
    }
    if (copies == 2 || copies == 3 || copies == 6) {
        if (testPos(pos + 32)) return true;
    }
    if (copies == 4) {
        if (testPos(pos + 64)) return true;
    }
    if (copies == 6) {
        if (testPos(pos + 64)) return true;
    }
    return false;
}

bool TIA::MissileBit(int x, uint8_t pos, uint8_t nusiz, bool ena) const noexcept {
    if (!ena) return false;
    const int size = 1 << ((nusiz >> 4) & 0x03);
    const int dx   = x - pos;
    return dx >= 0 && dx < size;
}

bool TIA::BallBit(int x, uint8_t pos, uint8_t ctrl, bool ena) const noexcept {
    if (!ena) return false;
    const int size = 1 << ((ctrl >> 4) & 0x03);
    const int dx   = x - pos;
    return dx >= 0 && dx < size;
}

// ─── Pixel Render ─────────────────────────────────────────────────────────────

void TIA::RenderPixel() noexcept {
    const int px = colorClock_ - kHBlankClocks;
    if (px < 0 || px >= kWidth) return;
    const int scanVisible = scanline_ - kScanlinesVSync - kScanlinesVBlank;
    if (scanVisible < 0 || scanVisible >= kScanlinesVisible) return;

    uint8_t displayCol = colubk_; // Default background

    const bool pf   = PlayfieldBit(px);
    const bool p0   = PlayerBit(px, vdelp0_ ? grp0_old_ : grp0_, resp0_, nusiz0_, (refp0_ & 0x08) != 0);
    const bool p1   = PlayerBit(px, vdelp1_ ? grp1_old_ : grp1_, resp1_, nusiz1_, (refp1_ & 0x08) != 0);
    const bool m0   = MissileBit(px, resm0_, nusiz0_, (enam0_ & 0x02) != 0);
    const bool m1   = MissileBit(px, resm1_, nusiz1_, (enam1_ & 0x02) != 0);
    const bool bl   = BallBit   (px, resbl_, ctrlpf_,  (vdelbl_ ? (enabl_ >> 1) : enabl_) & 0x02);

    // Collision detection
    if (m0 && p1) cxm0p_  |= 0x80;
    if (m0 && p0) cxm0p_  |= 0x40;
    if (m1 && p0) cxm1p_  |= 0x80;
    if (m1 && p1) cxm1p_  |= 0x40;
    if (p0 && (pf || bl)) cxp0fb_ |= 0x80;
    if (p0 && bl)         cxp0fb_ |= 0x40;
    if (p1 && (pf || bl)) cxp1fb_ |= 0x80;
    if (p1 && bl)         cxp1fb_ |= 0x40;
    if (m0 && (pf || bl)) cxm0fb_ |= 0x80;
    if (m0 && bl)         cxm0fb_ |= 0x40;
    if (m1 && (pf || bl)) cxm1fb_ |= 0x80;
    if (m1 && bl)         cxm1fb_ |= 0x40;
    if (bl && pf)         cxblpf_ |= 0x80;
    if (m0 && m1)         cxppmm_ |= 0x80;
    if (p0 && p1)         cxppmm_ |= 0x40;

    // Priority: PF/BL > P0/M0 > P1/M1 > BK (when CTRLPF bit2 set)
    const bool pfPriority = (ctrlpf_ & 0x04) != 0;
    const bool pfScore    = (ctrlpf_ & 0x02) != 0;

    if (pfPriority) {
        if      (pf || bl) displayCol = pfScore ? (px < 80 ? colup0_ : colup1_) : colupf_;
        else if (p0 || m0) displayCol = colup0_;
        else if (p1 || m1) displayCol = colup1_;
    } else {
        if      (p0 || m0) displayCol = colup0_;
        else if (p1 || m1) displayCol = colup1_;
        else if (pf || bl) displayCol = pfScore ? (px < 80 ? colup0_ : colup1_) : colupf_;
    }

    framebuffer_[scanVisible * kWidth + px] = NTSCColor(displayCol);
}

// ─── Tick ─────────────────────────────────────────────────────────────────────

void TIA::Tick() {
    RenderPixel();

    colorClock_++;
    if (colorClock_ >= kClocksPerScanline) {
        colorClock_ = 0;
        scanline_++;
        if (scanline_ >= kTotalScanlines) {
            scanline_  = 0;
            frameReady_ = true;
        }
    }
}

} // namespace Atari2600
