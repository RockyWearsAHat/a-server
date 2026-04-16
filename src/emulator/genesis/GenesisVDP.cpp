// GenesisVDP.cpp — Sega VDP scaffold with timing and register plumbing.

#include "emulator/genesis/GenesisVDP.h"
#include "emulator/common/SaveState.h"
#include "emulator/genesis/GenesisConstants.h"
#include <algorithm>

namespace AIO::Emulator::Genesis {

uint16_t GenesisVDP::ReadCRAM(uint8_t idx) const noexcept {
    return cram_[idx & 0x3F];
}

uint16_t GenesisVDP::ReadVSRAM(uint8_t idx) const noexcept {
    return vsram_[idx % vsram_.size()];
}

int GenesisVDP::ActiveWidth() const noexcept {
    // Reg 12 bit0 selects H40/H32 (1=H40, 0=H32)
    return (regs_[12] & 0x01) ? static_cast<int>(kPixelsH40) : static_cast<int>(kPixelsH32);
}

int GenesisVDP::ActiveLines() const noexcept {
    // Reg 1 bit3 can enable 240-line mode in some configurations.
    return (regs_[1] & 0x08) ? 240 : 224;
}

int GenesisVDP::CyclesPerLine() const noexcept {
    // Approximate master cycles per scanline for NTSC timing.
    return 488;
}

uint16_t GenesisVDP::ReadData() {
    uint16_t value = 0xFFFF;

    switch ((addrCode_ >> 1) & 0x03) {
        case 0: // VRAM
            value = static_cast<uint16_t>(vram_[addrReg_ & 0xFFFF] << 8);
            value |= vram_[(addrReg_ + 1) & 0xFFFF];
            break;
        case 1: // CRAM
            value = cram_[(addrReg_ >> 1) & 0x3F];
            break;
        case 2: // VSRAM
            value = vsram_[(addrReg_ >> 1) % vsram_.size()];
            break;
        default:
            break;
    }

    AdvanceAddress();
    return value;
}

void GenesisVDP::WriteData(uint16_t value) {
    switch ((addrCode_ >> 1) & 0x03) {
        case 0: // VRAM
            vram_[addrReg_ & 0xFFFF] = static_cast<uint8_t>(value >> 8);
            vram_[(addrReg_ + 1) & 0xFFFF] = static_cast<uint8_t>(value);
            break;
        case 1: // CRAM
            cram_[(addrReg_ >> 1) & 0x3F] = static_cast<uint16_t>(value & 0x0EEE);
            break;
        case 2: // VSRAM
            vsram_[(addrReg_ >> 1) % vsram_.size()] = static_cast<uint16_t>(value & 0x03FF);
            break;
        default:
            break;
    }

    AdvanceAddress();
}

uint16_t GenesisVDP::ReadStatus() {
    uint16_t s = 0;
    if (vintPending_) { s |= 0x0080; }
    if (hintPending_) { s |= 0x0040; }
    if (dmaPending_)  { s |= 0x0002; }

    // Clear pending IRQ flags on status read (matches hardware behavior).
    hintPending_ = false;
    vintPending_ = false;

    return s;
}

void GenesisVDP::WriteCtrl(uint16_t value) {
    if (firstWord_) {
        ctrlLatch_ = value;
        firstWord_ = false;
        return;
    }

    ctrlLatch_ = (ctrlLatch_ << 16) | value;
    firstWord_ = true;

    // Register write command: 10rrrddd dddddddd pattern
    if ((value & 0xC000u) == 0x8000u) {
        const uint8_t reg = static_cast<uint8_t>((value >> 8) & 0x1F);
        regs_[reg] = static_cast<uint8_t>(value);

        if (reg == 10) {
            hintCounter_ = regs_[10];
        }
        return;
    }

    // Address set command.
    addrReg_ = ((static_cast<uint32_t>(ctrlLatch_ & 0x0003) << 14) |
                (static_cast<uint32_t>(ctrlLatch_ & 0x3FFF0000u) >> 16)) & 0xFFFF;
    addrCode_ = static_cast<uint8_t>((ctrlLatch_ >> 2) & 0x3F);

    // DMA trigger if enabled and code indicates DMA write.
    if ((regs_[1] & 0x10) != 0 && (addrCode_ & 0x20) != 0) {
        dmaPending_ = true;
        dmaLen_ = static_cast<uint16_t>((static_cast<uint16_t>(regs_[20]) << 8) | regs_[19]);
        dmaSrc_ = (static_cast<uint32_t>(regs_[23] & 0x7F) << 17) |
                  (static_cast<uint32_t>(regs_[22]) << 9) |
                  (static_cast<uint32_t>(regs_[21]) << 1);
        dmaType_ = static_cast<uint8_t>(regs_[23] >> 6);
    }
}

uint16_t GenesisVDP::ReadHVCounter() const noexcept {
    const uint8_t h = static_cast<uint8_t>(hCounter_ & 0xFF);
    const uint8_t v = static_cast<uint8_t>(vCounter_ & 0xFF);
    return static_cast<uint16_t>((h << 8) | v);
}

void GenesisVDP::AdvanceAddress() noexcept {
    const uint8_t inc = regs_[15] == 0 ? 2 : regs_[15];
    addrReg_ = (addrReg_ + inc) & 0xFFFF;
}

uint16_t GenesisVDP::CRAMToRGBA(uint16_t cramWord) const noexcept {
    // Genesis CRAM stores 3-bit BGR (0bbb0ggg0rrr0) style values.
    const uint8_t r = static_cast<uint8_t>((cramWord >> 1) & 0x0E);
    const uint8_t g = static_cast<uint8_t>((cramWord >> 5) & 0x0E);
    const uint8_t b = static_cast<uint8_t>((cramWord >> 9) & 0x0E);
    const uint8_t rr = static_cast<uint8_t>((r << 4) | (r << 1));
    const uint8_t gg = static_cast<uint8_t>((g << 4) | (g << 1));
    const uint8_t bb = static_cast<uint8_t>((b << 4) | (b << 1));
    return static_cast<uint16_t>((rr << 8) | (gg ^ bb));
}

void GenesisVDP::RenderPlane(int /*line*/, bool /*planeA*/, int* pixels) const {
    // Bring-up renderer: fill with backdrop color index from reg7.
    const int backdrop = regs_[7] & 0x3F;
    const int width = ActiveWidth();
    for (int x = 0; x < width; ++x) {
        pixels[x] = backdrop;
    }
}

void GenesisVDP::RenderSprites(int /*line*/, int* pixels) const {
    const int width = ActiveWidth();
    for (int x = 0; x < width; ++x) {
        pixels[x] = -1; // transparent
    }
}

void GenesisVDP::CompositeOutput(int line, const int* bg, const int* fg, const int* sp) {
    const int width = ActiveWidth();
    const int outY = std::min(line, kFramebufferHeight - 1);

    for (int x = 0; x < kFramebufferWidth; ++x) {
        const int srcX = std::min(x, width - 1);
        int colorIndex = bg[srcX];
        if (fg[srcX] >= 0) { colorIndex = fg[srcX]; }
        if (sp[srcX] >= 0) { colorIndex = sp[srcX]; }

        const uint16_t c = cram_[colorIndex & 0x3F];
        const uint8_t r = static_cast<uint8_t>(((c >> 1) & 0x0E) * 18);
        const uint8_t g = static_cast<uint8_t>(((c >> 5) & 0x0E) * 18);
        const uint8_t b = static_cast<uint8_t>(((c >> 9) & 0x0E) * 18);

        const int base = (outY * kFramebufferWidth + x) * 4;
        framebuffer_[base + 0] = r;
        framebuffer_[base + 1] = g;
        framebuffer_[base + 2] = b;
        framebuffer_[base + 3] = 0xFF;
    }
}

void GenesisVDP::RenderScanline(int line) {
    const int width = ActiveWidth();
    int bg[320] {};
    int fg[320] {};
    int sp[320] {};

    for (int i = 0; i < width; ++i) {
        bg[i] = fg[i] = 0;
        sp[i] = -1;
    }

    RenderPlane(line, false, bg);
    RenderPlane(line, true,  fg);
    RenderSprites(line, sp);
    CompositeOutput(line, bg, fg, sp);
}

void GenesisVDP::ExecuteDMA() {
    if (!dmaPending_ || dmaLen_ == 0) {
        dmaPending_ = false;
        return;
    }

    // Simplified DMA behavior for bring-up:
    // - Fill: write low byte of source to VRAM
    // - Copy: VRAM to VRAM one word
    // - 68K: leave as no-op until memory callback wiring is added
    const uint32_t words = dmaLen_ == 0 ? 0x10000u : static_cast<uint32_t>(dmaLen_);

    if (dmaType_ == 2) {
        const uint8_t fill = static_cast<uint8_t>(dmaSrc_ & 0xFF);
        for (uint32_t i = 0; i < words; ++i) {
            vram_[addrReg_ & 0xFFFF] = fill;
            AdvanceAddress();
        }
    } else if (dmaType_ == 3) {
        uint16_t src = static_cast<uint16_t>(dmaSrc_ & 0xFFFF);
        for (uint32_t i = 0; i < words; ++i) {
            const uint8_t v = vram_[src++];
            vram_[addrReg_ & 0xFFFF] = v;
            AdvanceAddress();
        }
    }

    dmaPending_ = false;
    dmaLen_ = 0;
}

void GenesisVDP::RunScanline() {
    const int activeLines = ActiveLines();

    if (vCounter_ < activeLines) {
        RenderScanline(vCounter_);
    }

    // HINT counter
    if (hintCounter_ == 0) {
        hintPending_ = true;
        hintCounter_ = regs_[10];
        if ((regs_[0] & 0x10) != 0 && hintCb_) {
            hintCb_();
        }
    } else {
        --hintCounter_;
    }

    ++vCounter_;
    if (vCounter_ == activeLines) {
        vintPending_ = true;
        if ((regs_[1] & 0x20) != 0 && vintCb_) {
            vintCb_();
        }
    }

    const int totalLines = static_cast<int>(kLinesNTSC);
    if (vCounter_ >= totalLines) {
        vCounter_ = 0;
        ++frameCount_;
    }
}

void GenesisVDP::Tick(uint32_t masterCycles) {
    cycleAcc_ += masterCycles;

    while (cycleAcc_ >= static_cast<uint32_t>(CyclesPerLine())) {
        cycleAcc_ -= static_cast<uint32_t>(CyclesPerLine());
        hCounter_ = 0;
        RunScanline();
    }

    hCounter_ = static_cast<int>((cycleAcc_ * static_cast<uint32_t>(ActiveWidth())) /
                                 static_cast<uint32_t>(CyclesPerLine()));

    if (dmaPending_) {
        ExecuteDMA();
    }
}

void GenesisVDP::SaveState(AIO::Emulator::Common::SaveStateWriter& w) const {
    w.WriteBytes(vram_.data(), vram_.size());

    for (uint16_t c : cram_)  { w.WriteU16(c); }
    for (uint16_t v : vsram_) { w.WriteU16(v); }
    w.WriteBytes(regs_.data(), regs_.size());

    w.WriteBytes(framebuffer_.data(), framebuffer_.size());

    w.WriteU32(addrReg_);
    w.WriteU8(addrCode_);
    w.WriteBool(firstWord_);
    w.WriteU32(ctrlLatch_);

    w.WriteBool(dmaPending_);
    w.WriteU32(dmaSrc_);
    w.WriteU16(dmaLen_);
    w.WriteU8(dmaType_);

    w.WriteU32(static_cast<uint32_t>(hCounter_));
    w.WriteU32(static_cast<uint32_t>(vCounter_));
    w.WriteU32(cycleAcc_);
    w.WriteU64(frameCount_);

    w.WriteBool(vintPending_);
    w.WriteBool(hintPending_);
    w.WriteU32(static_cast<uint32_t>(hintCounter_));
}

void GenesisVDP::LoadState(AIO::Emulator::Common::SaveStateReader& r) {
    r.ReadBytes(vram_.data(), vram_.size());

    for (uint16_t& c : cram_)  { c = r.ReadU16(); }
    for (uint16_t& v : vsram_) { v = r.ReadU16(); }
    r.ReadBytes(regs_.data(), regs_.size());

    r.ReadBytes(framebuffer_.data(), framebuffer_.size());

    addrReg_ = r.ReadU32();
    addrCode_ = r.ReadU8();
    firstWord_ = r.ReadBool();
    ctrlLatch_ = r.ReadU32();

    dmaPending_ = r.ReadBool();
    dmaSrc_ = r.ReadU32();
    dmaLen_ = r.ReadU16();
    dmaType_ = r.ReadU8();

    hCounter_ = static_cast<int>(r.ReadU32());
    vCounter_ = static_cast<int>(r.ReadU32());
    cycleAcc_ = r.ReadU32();
    frameCount_ = r.ReadU64();

    vintPending_ = r.ReadBool();
    hintPending_ = r.ReadBool();
    hintCounter_ = static_cast<int>(r.ReadU32());
}

} // namespace AIO::Emulator::Genesis
