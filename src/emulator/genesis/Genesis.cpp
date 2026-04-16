// Genesis.cpp — top-level Sega Genesis system orchestration.

#include "emulator/genesis/Genesis.h"
#include "emulator/common/SaveState.h"
#include "emulator/genesis/GenesisConstants.h"

namespace AIO::Emulator::Genesis {

Genesis::Genesis()
    : cpu_(mem_)
    , z80_(mem_) {
    mem_.Init(&cart_, &vdp_, &ym_, &psg_);
    WireInterrupts();
}

void Genesis::WireInterrupts() {
    vdp_.SetVIntCallback([this]() { cpu_.SetInterruptLevel(kVdpVblankIrqLevel); });
    vdp_.SetHIntCallback([this]() { cpu_.SetInterruptLevel(kVdpHblankIrqLevel); });
}

void Genesis::Load(std::span<const uint8_t> romData) {
    cart_.Load(romData);
    Reset();
}

void Genesis::Reset() {
    totalMasterCycles_ = 0;
    frameCycleDebt_ = 0;

    // Z80 starts held in reset + bus requested by M68K.
    z80_.SetReset(true);
    z80_.SetBusRequest(true);

    cpu_.Reset();
}

int Genesis::Step() {
    const int m68kCycles = cpu_.Step();
    const int masterCycles = m68kCycles * static_cast<int>(kM68kDivider);

    totalMasterCycles_ += static_cast<uint64_t>(masterCycles);

    // Drive subordinate devices according to master-clock budget.
    vdp_.Tick(static_cast<uint32_t>(masterCycles));
    ym_.Tick(static_cast<uint32_t>(masterCycles));
    psg_.Tick(static_cast<uint32_t>(masterCycles));

    // Z80 runs at master/15. Step proportionally; if held, it returns 0.
    int z80Budget = masterCycles / static_cast<int>(kZ80Divider);
    while (z80Budget > 0) {
        const int z = z80_.Step();
        if (z <= 0) {
            break;
        }
        z80Budget -= z;
    }

    return masterCycles;
}

void Genesis::RunFrame() {
    const uint64_t startFrame = vdp_.FrameCount();
    while (vdp_.FrameCount() == startFrame) {
        static_cast<void>(Step());
    }
}

void Genesis::SaveState(AIO::Emulator::Common::SaveStateWriter& w) const {
    w.WriteU64(totalMasterCycles_);
    w.WriteU32(static_cast<uint32_t>(frameCycleDebt_));

    cart_.SaveState(w);
    mem_.SaveState(w);
    vdp_.SaveState(w);
    ym_.SaveState(w);
    psg_.SaveState(w);
    cpu_.SaveState(w);
    z80_.SaveState(w);
}

void Genesis::LoadState(AIO::Emulator::Common::SaveStateReader& r) {
    totalMasterCycles_ = r.ReadU64();
    frameCycleDebt_ = static_cast<int>(r.ReadU32());

    cart_.LoadState(r);
    mem_.LoadState(r);
    vdp_.LoadState(r);
    ym_.LoadState(r);
    psg_.LoadState(r);
    cpu_.LoadState(r);
    z80_.LoadState(r);
}

} // namespace AIO::Emulator::Genesis
