#pragma once

#include "emulator/common/ISaveStateable.h"
#include "emulator/snes/SNESConstants.h"
#include <array>
#include <cstdint>

namespace AIO::Emulator::SNES {

class SNESCartridge;
class SNESPPU;
class SPC700;

class SNESMemory : public AIO::Emulator::Common::ISaveStateable {
public:
    SNESMemory() = default;
    ~SNESMemory() override = default;

    SNESMemory(const SNESMemory&) = delete;
    SNESMemory& operator=(const SNESMemory&) = delete;

    void Init(SNESCartridge* cart, SNESPPU* ppu, SPC700* spc) noexcept;

    [[nodiscard]] uint8_t Read8(uint32_t addr);
    [[nodiscard]] uint16_t Read16(uint32_t addr);

    void Write8(uint32_t addr, uint8_t value);
    void Write16(uint32_t addr, uint16_t value);

    [[nodiscard]] uint8_t SPCRead8(uint16_t addr) const noexcept;
    void SPCWrite8(uint16_t addr, uint8_t value) noexcept;

    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    std::array<uint8_t, kWramSize> wram_{};
    std::array<uint8_t, 0x10000> apuRam_{};

    SNESCartridge* cart_ = nullptr;
    SNESPPU* ppu_ = nullptr;
    SPC700* spc_ = nullptr;
};

} // namespace AIO::Emulator::SNES
