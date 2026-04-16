#pragma once

#include "emulator/common/ISaveStateable.h"
#include <cstdint>

namespace AIO::Emulator::SNES {

class SNESMemory;

class SPC700 : public AIO::Emulator::Common::ISaveStateable {
public:
    explicit SPC700(SNESMemory& memory) noexcept;
    ~SPC700() override = default;

    SPC700(const SPC700&) = delete;
    SPC700& operator=(const SPC700&) = delete;

    void Reset() noexcept;
    [[nodiscard]] int Step();

    [[nodiscard]] uint16_t GetPC() const noexcept { return pc_; }

    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    SNESMemory& memory_;
    uint16_t pc_ = 0;
    uint8_t a_ = 0;
    uint8_t x_ = 0;
    uint8_t y_ = 0;
    uint8_t p_ = 0;
    uint8_t sp_ = 0xFF;
};

} // namespace AIO::Emulator::SNES
