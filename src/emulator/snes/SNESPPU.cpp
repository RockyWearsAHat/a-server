#include "emulator/snes/SNESPPU.h"

#include "emulator/common/SaveState.h"

namespace AIO::Emulator::SNES {

void SNESPPU::Tick(uint32_t masterCycles) {
    cycleAcc_ += masterCycles;

    const uint32_t lineCycles = static_cast<uint32_t>(kPpuCyclesPerLine);
    while (cycleAcc_ >= lineCycles) {
        cycleAcc_ -= lineCycles;
        hCounter_ = 0;
        ++vCounter_;

        if (vCounter_ >= kPpuLinesPerFrame) {
            vCounter_ = 0;
            ++frameCount_;
        }
    }

    hCounter_ = static_cast<int>(cycleAcc_);

    // Simple gradient output so framebuffer is non-empty during baseline bring-up.
    for (int y = 0; y < kPpuVisibleHeight; ++y) {
        for (int x = 0; x < kPpuVisibleWidth; ++x) {
            const int i = (y * kPpuVisibleWidth + x) * 4;
            framebuffer_[i + 0] = static_cast<uint8_t>((x + (vCounter_ & 0xFF)) & 0xFF);
            framebuffer_[i + 1] = static_cast<uint8_t>((y + (hCounter_ & 0xFF)) & 0xFF);
            framebuffer_[i + 2] = static_cast<uint8_t>((x ^ y) & 0xFF);
            framebuffer_[i + 3] = 0xFF;
        }
    }
}

uint8_t SNESPPU::ReadReg(uint16_t reg) const noexcept {
    return regs_[reg & 0x3F];
}

void SNESPPU::WriteReg(uint16_t reg, uint8_t value) noexcept {
    regs_[reg & 0x3F] = value;
}

void SNESPPU::SaveState(AIO::Emulator::Common::SaveStateWriter& w) const {
    w.WriteBytes(regs_.data(), regs_.size());
    w.WriteBytes(framebuffer_.data(), framebuffer_.size());
    w.WriteU32(cycleAcc_);
    w.WriteU32(static_cast<uint32_t>(hCounter_));
    w.WriteU32(static_cast<uint32_t>(vCounter_));
    w.WriteU64(frameCount_);
}

void SNESPPU::LoadState(AIO::Emulator::Common::SaveStateReader& r) {
    r.ReadBytes(regs_.data(), regs_.size());
    r.ReadBytes(framebuffer_.data(), framebuffer_.size());
    cycleAcc_ = r.ReadU32();
    hCounter_ = static_cast<int>(r.ReadU32());
    vCounter_ = static_cast<int>(r.ReadU32());
    frameCount_ = r.ReadU64();
}

} // namespace AIO::Emulator::SNES
