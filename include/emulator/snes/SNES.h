#pragma once

#include "emulator/common/ISaveStateable.h"
#include "emulator/snes/SNESCartridge.h"
#include "emulator/snes/SNESMemory.h"
#include "emulator/snes/SNESPPU.h"
#include "emulator/snes/SPC700.h"
#include "emulator/snes/W65C816.h"
#include <cstdint>
#include <span>

namespace AIO::Emulator::SNES {

class SNES : public AIO::Emulator::Common::ISaveStateable {
public:
    SNES();
    ~SNES() override = default;

    SNES(const SNES&) = delete;
    SNES& operator=(const SNES&) = delete;

    void Load(std::span<const uint8_t> romData);
    void Reset();

    [[nodiscard]] int Step();
    void RunFrame();

    [[nodiscard]] SNESCartridge& GetCartridge() noexcept { return cart_; }
    [[nodiscard]] SNESMemory& GetMemory() noexcept { return memory_; }
    [[nodiscard]] W65C816& GetCPU() noexcept { return cpu_; }
    [[nodiscard]] SPC700& GetSPC() noexcept { return spc_; }
    [[nodiscard]] SNESPPU& GetPPU() noexcept { return ppu_; }

    [[nodiscard]] const SNESCartridge& GetCartridge() const noexcept { return cart_; }
    [[nodiscard]] const SNESMemory& GetMemory() const noexcept { return memory_; }
    [[nodiscard]] const W65C816& GetCPU() const noexcept { return cpu_; }
    [[nodiscard]] const SPC700& GetSPC() const noexcept { return spc_; }
    [[nodiscard]] const SNESPPU& GetPPU() const noexcept { return ppu_; }

    [[nodiscard]] uint64_t GetMasterCycles() const noexcept { return masterCycles_; }

    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    SNESCartridge cart_;
    SNESMemory memory_;
    SNESPPU ppu_;
    W65C816 cpu_;
    SPC700 spc_;

    uint64_t masterCycles_ = 0;
};

} // namespace AIO::Emulator::SNES
