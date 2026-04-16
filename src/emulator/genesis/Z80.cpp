// Z80.cpp — minimal Z80 execution scaffold for Genesis sound CPU bring-up.

#include "emulator/genesis/Z80.h"
#include "emulator/common/SaveState.h"
#include "emulator/genesis/GenesisMemory.h"

namespace AIO::Emulator::Genesis {

Z80::Z80(GenesisMemory& mem) noexcept
    : mem_(mem) {
    SetReset(true);
}

void Z80::SetReset(bool reset) noexcept {
    const bool rising = (!reset_ && reset);
    reset_ = reset;
    if (rising) {
        a_ = f_ = 0xFF;
        b_ = c_ = d_ = e_ = h_ = l_ = 0;
        ixh_ = ixl_ = iyh_ = iyl_ = 0;
        i_ = r_ = 0;
        sp_ = 0xFFFF;
        pc_ = 0x0000;
        halted_ = false;
        iff1_ = iff2_ = false;
        im_ = 0;
        nmiPending_ = false;
        irqPending_ = false;
        bankAddr_ = 0;
    }
}

void Z80::SetBankWindow(uint8_t highBit) noexcept {
    bankAddr_ = ((bankAddr_ >> 1) | (static_cast<uint32_t>(highBit & 1) << 23)) & 0xFF8000u;
    mem_.SetZ80BankWindow(bankAddr_);
}

uint8_t Z80::Read8(uint16_t addr) {
    return mem_.Z80Read8(addr);
}

void Z80::Write8(uint16_t addr, uint8_t v) {
    mem_.Z80Write8(addr, v);
}

uint8_t Z80::In8(uint8_t port) {
    return mem_.Z80In8(port);
}

void Z80::Out8(uint8_t port, uint8_t v) {
    mem_.Z80Out8(port, v);
}

void Z80::Push(uint16_t v) {
    --sp_;
    Write8(sp_, static_cast<uint8_t>(v >> 8));
    --sp_;
    Write8(sp_, static_cast<uint8_t>(v));
}

uint16_t Z80::Pop() {
    const uint8_t lo = Read8(sp_++);
    const uint8_t hi = Read8(sp_++);
    return (static_cast<uint16_t>(hi) << 8) | lo;
}

int Z80::ServiceNMI() {
    nmiPending_ = false;
    iff2_ = iff1_;
    iff1_ = false;
    Push(pc_);
    pc_ = 0x0066;
    return 11;
}

int Z80::ServiceIRQ() {
    if (!iff1_) {
        return 0;
    }
    irqPending_ = false;
    iff1_ = false;
    iff2_ = false;
    Push(pc_);
    if (im_ == 0 || im_ == 1) {
        pc_ = 0x0038;
    } else {
        const uint16_t vector = static_cast<uint16_t>((static_cast<uint16_t>(i_) << 8) | 0xFF);
        const uint8_t lo = Read8(vector);
        const uint8_t hi = Read8(static_cast<uint16_t>(vector + 1));
        pc_ = static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8) | lo);
    }
    return 13;
}

int Z80::Dispatch() {
    const uint8_t op = Read8(pc_++);

    // Minimal bring-up opcode set.
    switch (op) {
        case 0x00: return 4; // NOP
        case 0x76: halted_ = true; return 4; // HALT
        case 0x3E: a_ = Read8(pc_++); return 7; // LD A,n
        case 0x32: { // LD (nn),A
            const uint8_t lo = Read8(pc_++);
            const uint8_t hi = Read8(pc_++);
            const uint16_t nn = (static_cast<uint16_t>(hi) << 8) | lo;
            Write8(nn, a_);
            return 13;
        }
        case 0x3A: { // LD A,(nn)
            const uint8_t lo = Read8(pc_++);
            const uint8_t hi = Read8(pc_++);
            const uint16_t nn = (static_cast<uint16_t>(hi) << 8) | lo;
            a_ = Read8(nn);
            return 13;
        }
        case 0xC3: { // JP nn
            const uint8_t lo = Read8(pc_++);
            const uint8_t hi = Read8(pc_++);
            pc_ = static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8) | lo);
            return 10;
        }
        case 0xCD: { // CALL nn
            const uint8_t lo = Read8(pc_++);
            const uint8_t hi = Read8(pc_++);
            const uint16_t nn = static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8) | lo);
            Push(pc_);
            pc_ = nn;
            return 17;
        }
        case 0xC9: // RET
            pc_ = Pop();
            return 10;
        case 0xF3: // DI
            iff1_ = false;
            iff2_ = false;
            return 4;
        case 0xFB: // EI
            iff1_ = true;
            iff2_ = true;
            return 4;
        case 0xD3: { // OUT (n),A
            const uint8_t p = Read8(pc_++);
            Out8(p, a_);
            return 11;
        }
        case 0xDB: { // IN A,(n)
            const uint8_t p = Read8(pc_++);
            a_ = In8(p);
            return 11;
        }
        case 0xCB: return DispatchCB();
        case 0xDD: return DispatchDD();
        case 0xED: return DispatchED();
        case 0xFD: return DispatchFD();
        default:
            // Treat unsupported opcodes as NOP for deterministic bring-up.
            return 4;
    }
}

int Z80::Step() {
    if (reset_ || busReq_) {
        return 0;
    }

    if (nmiPending_) {
        return ServiceNMI();
    }
    if (irqPending_) {
        const int cy = ServiceIRQ();
        if (cy > 0) {
            return cy;
        }
    }

    if (halted_) {
        return 4;
    }

    return Dispatch();
}

int Z80::DispatchCB() { return 8; }
int Z80::DispatchDD() { return 8; }
int Z80::DispatchED() { return 8; }
int Z80::DispatchFD() { return 8; }
int Z80::DispatchDDCB(int8_t /*dis*/) { return 15; }
int Z80::DispatchFDCB(int8_t /*dis*/) { return 15; }

void Z80::SaveState(AIO::Emulator::Common::SaveStateWriter& w) const {
    w.WriteU8(a_); w.WriteU8(f_);
    w.WriteU8(b_); w.WriteU8(c_);
    w.WriteU8(d_); w.WriteU8(e_);
    w.WriteU8(h_); w.WriteU8(l_);
    w.WriteU8(ixh_); w.WriteU8(ixl_);
    w.WriteU8(iyh_); w.WriteU8(iyl_);
    w.WriteU8(i_); w.WriteU8(r_);
    w.WriteU16(sp_); w.WriteU16(pc_);

    w.WriteU8(a2_); w.WriteU8(f2_);
    w.WriteU8(b2_); w.WriteU8(c2_);
    w.WriteU8(d2_); w.WriteU8(e2_);
    w.WriteU8(h2_); w.WriteU8(l2_);

    w.WriteBool(busReq_);
    w.WriteBool(reset_);
    w.WriteBool(halted_);
    w.WriteBool(iff1_);
    w.WriteBool(iff2_);
    w.WriteU8(im_);
    w.WriteBool(nmiPending_);
    w.WriteBool(irqPending_);
    w.WriteU32(bankAddr_);
}

void Z80::LoadState(AIO::Emulator::Common::SaveStateReader& r) {
    a_ = r.ReadU8(); f_ = r.ReadU8();
    b_ = r.ReadU8(); c_ = r.ReadU8();
    d_ = r.ReadU8(); e_ = r.ReadU8();
    h_ = r.ReadU8(); l_ = r.ReadU8();
    ixh_ = r.ReadU8(); ixl_ = r.ReadU8();
    iyh_ = r.ReadU8(); iyl_ = r.ReadU8();
    i_ = r.ReadU8(); r_ = r.ReadU8();
    sp_ = r.ReadU16(); pc_ = r.ReadU16();

    a2_ = r.ReadU8(); f2_ = r.ReadU8();
    b2_ = r.ReadU8(); c2_ = r.ReadU8();
    d2_ = r.ReadU8(); e2_ = r.ReadU8();
    h2_ = r.ReadU8(); l2_ = r.ReadU8();

    busReq_ = r.ReadBool();
    reset_ = r.ReadBool();
    halted_ = r.ReadBool();
    iff1_ = r.ReadBool();
    iff2_ = r.ReadBool();
    im_ = r.ReadU8();
    nmiPending_ = r.ReadBool();
    irqPending_ = r.ReadBool();
    bankAddr_ = r.ReadU32();
    mem_.SetZ80BankWindow(bankAddr_);
}

} // namespace AIO::Emulator::Genesis
