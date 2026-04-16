#include "emulator/snes/SNES.h"

#include "emulator/common/SaveState.h"

namespace AIO::Emulator::SNES {

SNES::SNES()
    : cpu_(memory_)
    , spc_(memory_) {
    memory_.Init(&cart_, &ppu_, &spc_);
}

void SNES::Load(std::span<const uint8_t> romData) {
    cart_.Load(romData);
    Reset();
}

void SNES::Reset() {
    masterCycles_ = 0;
    cpu_.Reset();
    spc_.Reset();
}

int SNES::Step() {
    const int cpuCycles = cpu_.Step();

    // Baseline timing: convert one CPU cycle to six master ticks.
    const uint32_t masterTicks = static_cast<uint32_t>(cpuCycles * 6);
    masterCycles_ += masterTicks;

    ppu_.Tick(masterTicks);

    int spcBudget = cpuCycles;
    while (spcBudget-- > 0) {
        static_cast<void>(spc_.Step());
    }

    return cpuCycles;
}

void SNES::RunFrame() {
    const uint64_t frameBefore = ppu_.FrameCount();
    while (ppu_.FrameCount() == frameBefore) {
        static_cast<void>(Step());
    }
}

void SNES::SaveState(AIO::Emulator::Common::SaveStateWriter& w) const {
    w.WriteU64(masterCycles_);
    cart_.SaveState(w);
    memory_.SaveState(w);
    ppu_.SaveState(w);
    cpu_.SaveState(w);
    spc_.SaveState(w);
}

void SNES::LoadState(AIO::Emulator::Common::SaveStateReader& r) {
    masterCycles_ = r.ReadU64();
    cart_.LoadState(r);
    memory_.LoadState(r);
    ppu_.LoadState(r);
    cpu_.LoadState(r);
    spc_.LoadState(r);
}

} // namespace AIO::Emulator::SNES
