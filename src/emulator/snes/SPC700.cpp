#include "emulator/snes/SPC700.h"

#include "emulator/common/SaveState.h"
#include "emulator/snes/SNESMemory.h"

namespace AIO::Emulator::SNES {

SPC700::SPC700(SNESMemory& memory) noexcept
    : memory_(memory) {}

void SPC700::Reset() noexcept {
    pc_ = 0xFFC0;
    a_ = 0;
    x_ = 0;
    y_ = 0;
    p_ = 0;
    sp_ = 0xFF;
}

int SPC700::Step() {
    const uint8_t opcode = memory_.SPCRead8(pc_++);

    switch (opcode) {
        case 0x00: // NOP
            return 2;
        default:
            return 2;
    }
}

void SPC700::SaveState(AIO::Emulator::Common::SaveStateWriter& w) const {
    w.WriteU16(pc_);
    w.WriteU8(a_);
    w.WriteU8(x_);
    w.WriteU8(y_);
    w.WriteU8(p_);
    w.WriteU8(sp_);
}

void SPC700::LoadState(AIO::Emulator::Common::SaveStateReader& r) {
    pc_ = r.ReadU16();
    a_ = r.ReadU8();
    x_ = r.ReadU8();
    y_ = r.ReadU8();
    p_ = r.ReadU8();
    sp_ = r.ReadU8();
}

} // namespace AIO::Emulator::SNES
