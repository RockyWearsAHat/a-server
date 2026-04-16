#pragma once

#include "emulator/common/ISaveStateable.h"
#include <cstdint>

namespace AIO::Emulator::SNES {

class SNESMemory;

class W65C816 : public AIO::Emulator::Common::ISaveStateable {
public:
    explicit W65C816(SNESMemory& memory) noexcept;
    ~W65C816() override = default;

    W65C816(const W65C816&) = delete;
    W65C816& operator=(const W65C816&) = delete;

    void Reset();
    [[nodiscard]] int Step();

    [[nodiscard]] uint16_t GetPC() const noexcept { return pc_; }
    [[nodiscard]] uint8_t GetPBR() const noexcept { return pbr_; }
    [[nodiscard]] uint16_t GetSP() const noexcept { return sp_; }

    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    SNESMemory& memory_;

    uint16_t pc_ = 0;
    uint16_t sp_ = 0x01FF;
    uint16_t a_ = 0;
    uint16_t x_ = 0;
    uint16_t y_ = 0;
    uint8_t p_ = 0x34;
    uint8_t dbr_ = 0;
    uint8_t pbr_ = 0;
    bool emulationMode_ = true;

    [[nodiscard]] uint8_t Read8(uint32_t addr);
    [[nodiscard]] uint16_t Read16(uint32_t addr);
    void Push8(uint8_t value);
    [[nodiscard]] uint8_t Pop8();
};

} // namespace AIO::Emulator::SNES
