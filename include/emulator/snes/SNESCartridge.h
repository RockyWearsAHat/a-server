#pragma once

#include "emulator/common/ISaveStateable.h"
#include <cstdint>
#include <span>
#include <vector>

namespace AIO::Emulator::SNES {

class SNESCartridge : public AIO::Emulator::Common::ISaveStateable {
public:
    SNESCartridge() = default;
    ~SNESCartridge() override = default;

    SNESCartridge(const SNESCartridge&) = delete;
    SNESCartridge& operator=(const SNESCartridge&) = delete;

    void Load(std::span<const uint8_t> data);

    [[nodiscard]] uint8_t Read8(uint32_t addr) const noexcept;
    [[nodiscard]] uint16_t Read16(uint32_t addr) const noexcept;

    void Write8(uint32_t addr, uint8_t value) noexcept;
    void Write16(uint32_t addr, uint16_t value) noexcept;

    [[nodiscard]] bool HasSram() const noexcept { return !sram_.empty(); }

    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    std::vector<uint8_t> rom_;
    std::vector<uint8_t> sram_;
};

} // namespace AIO::Emulator::SNES
