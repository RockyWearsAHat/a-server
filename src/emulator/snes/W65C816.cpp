#include "emulator/snes/W65C816.h"

#include "emulator/common/SaveState.h"
#include "emulator/snes/SNESMemory.h"

namespace AIO::Emulator::SNES {

W65C816::W65C816(SNESMemory& memory) noexcept
    : memory_(memory) {}

uint8_t W65C816::Read8(uint32_t addr) {
    return memory_.Read8(addr);
}

uint16_t W65C816::Read16(uint32_t addr) {
    return memory_.Read16(addr);
}

void W65C816::Push8(uint8_t value) {
    const uint32_t stackAddr = 0x000000u | static_cast<uint32_t>(sp_ & 0xFFFFu);
    memory_.Write8(stackAddr, value);
    sp_ = static_cast<uint16_t>(sp_ - 1u);
}

uint8_t W65C816::Pop8() {
    sp_ = static_cast<uint16_t>(sp_ + 1u);
    const uint32_t stackAddr = 0x000000u | static_cast<uint32_t>(sp_ & 0xFFFFu);
    return memory_.Read8(stackAddr);
}

void W65C816::Reset() {
    emulationMode_ = true;
    p_ = 0x34;
    pbr_ = 0;
    dbr_ = 0;
    sp_ = 0x01FF;
    pc_ = Read16(0x00FFFCu);
}

int W65C816::Step() {
    const uint32_t fetchAddr = (static_cast<uint32_t>(pbr_) << 16) | pc_;
    const uint8_t opcode = Read8(fetchAddr);
    pc_ = static_cast<uint16_t>(pc_ + 1u);

    switch (opcode) {
        case 0xEA: // NOP
            return 2;
        case 0x4C: { // JMP abs
            const uint16_t target = Read16((static_cast<uint32_t>(pbr_) << 16) | pc_);
            pc_ = target;
            return 3;
        }
        case 0x00: { // BRK
            const uint16_t returnPc = static_cast<uint16_t>(pc_ + 1u);
            Push8(static_cast<uint8_t>(returnPc >> 8));
            Push8(static_cast<uint8_t>(returnPc & 0xFF));
            Push8(p_);
            p_ |= 0x04; // IRQ disable
            pc_ = Read16(0x00FFE6u);
            return 8;
        }
        default:
            return 2;
    }
}

void W65C816::SaveState(AIO::Emulator::Common::SaveStateWriter& w) const {
    w.WriteU16(pc_);
    w.WriteU16(sp_);
    w.WriteU16(a_);
    w.WriteU16(x_);
    w.WriteU16(y_);
    w.WriteU8(p_);
    w.WriteU8(dbr_);
    w.WriteU8(pbr_);
    w.WriteBool(emulationMode_);
}

void W65C816::LoadState(AIO::Emulator::Common::SaveStateReader& r) {
    pc_ = r.ReadU16();
    sp_ = r.ReadU16();
    a_ = r.ReadU16();
    x_ = r.ReadU16();
    y_ = r.ReadU16();
    p_ = r.ReadU8();
    dbr_ = r.ReadU8();
    pbr_ = r.ReadU8();
    emulationMode_ = r.ReadBool();
}

} // namespace AIO::Emulator::SNES
