#pragma once

#include "emulator/common/ISaveStateable.h"
#include "emulator/snes/SNESConstants.h"
#include <array>
#include <cstdint>

namespace AIO::Emulator::SNES {

class SNESPPU : public AIO::Emulator::Common::ISaveStateable {
public:
    SNESPPU() = default;
    ~SNESPPU() override = default;

    SNESPPU(const SNESPPU&) = delete;
    SNESPPU& operator=(const SNESPPU&) = delete;

    void Tick(uint32_t masterCycles);

    [[nodiscard]] uint8_t ReadReg(uint16_t reg) const noexcept;
    void WriteReg(uint16_t reg, uint8_t value) noexcept;

    [[nodiscard]] const uint8_t* GetFramebuffer() const noexcept {
        return framebuffer_.data();
    }

    [[nodiscard]] uint64_t FrameCount() const noexcept { return frameCount_; }

    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    std::array<uint8_t, 0x40> regs_{};
    std::array<uint8_t, kPpuVisibleWidth * kPpuVisibleHeight * 4> framebuffer_{};

    uint32_t cycleAcc_ = 0;
    int hCounter_ = 0;
    int vCounter_ = 0;
    uint64_t frameCount_ = 0;
};

} // namespace AIO::Emulator::SNES
