// YM2612.cpp — simplified FM synthesis scaffold for Genesis bring-up.

#include "emulator/genesis/YM2612.h"
#include "emulator/common/SaveState.h"
#include <algorithm>

namespace AIO::Emulator::Genesis {

uint8_t YM2612::Read() const noexcept {
    return statusReg_;
}

void YM2612::Write(uint8_t port, uint8_t value) {
    const uint8_t part = static_cast<uint8_t>((port >> 1) & 1);
    const bool dataPort = (port & 1) != 0;

    if (!dataPort) {
        addrLatch_[part] = value;
        return;
    }
    WriteReg(part, addrLatch_[part], value);
}

void YM2612::WriteReg(uint8_t part, uint8_t reg, uint8_t val) {
    // Global regs
    if (part == 0) {
        if (reg == 0x22) { // LFO enable/rate
            lfoEnable_ = static_cast<uint8_t>((val >> 3) & 1);
            lfoRate_ = static_cast<uint8_t>(val & 0x07);
            return;
        }
        if (reg == 0x28) { // Key on/off
            const int ch = val & 0x03;
            if (ch == 3) { return; } // invalid
            const int trueCh = ch + ((val & 0x04) ? 3 : 0);
            const uint8_t mask = static_cast<uint8_t>((val >> 4) & 0x0F);
            if (mask != 0) { KeyOn(trueCh, mask); }
            else           { KeyOff(trueCh, 0x0F); }
            return;
        }
    }

    // Channel registers (A0-AF, B0-B6)
    if ((reg & 0xF0) == 0xA0) {
        int ch = (reg & 0x03) + (part ? 3 : 0);
        if ((reg & 0x0C) == 0x00) {
            channels_[ch].fnum = static_cast<uint16_t>((channels_[ch].fnum & 0x700u) | val);
            return;
        }
        if ((reg & 0x0C) == 0x04) {
            channels_[ch].fnum = static_cast<uint16_t>((channels_[ch].fnum & 0x0FFu) | ((val & 0x07) << 8));
            channels_[ch].block = static_cast<uint8_t>((val >> 3) & 0x07);
            return;
        }
    }

    if ((reg & 0xF0) == 0xB0) {
        int ch = (reg & 0x03) + (part ? 3 : 0);
        if ((reg & 0x0C) == 0x00) {
            channels_[ch].algo = static_cast<uint8_t>(val & 0x07);
            channels_[ch].fb   = static_cast<uint8_t>((val >> 3) & 0x07);
            return;
        }
        if ((reg & 0x0C) == 0x04) {
            channels_[ch].lrpan = val;
            channels_[ch].ams   = static_cast<uint8_t>((val >> 4) & 0x03);
            channels_[ch].pms   = static_cast<uint8_t>(val & 0x07);
            return;
        }
    }

    // Operator registers; slot layout follows YM2612 mapping.
    if ((reg & 0xF0) >= 0x30 && (reg & 0xF0) <= 0x90) {
        int ch = (reg & 0x03) + (part ? 3 : 0);
        int slot = (reg >> 2) & 0x03;
        Operator& op = channels_[ch].ops[slot];
        switch (reg & 0xF0) {
            case 0x30:
                op.dt = static_cast<uint8_t>((val >> 4) & 0x07);
                op.mul = static_cast<uint8_t>(val & 0x0F);
                break;
            case 0x40:
                op.tl = static_cast<uint8_t>(val & 0x7F);
                break;
            case 0x50:
                op.ks = static_cast<uint8_t>((val >> 6) & 0x03);
                op.ar = static_cast<uint8_t>(val & 0x1F);
                break;
            case 0x60:
                op.dr = static_cast<uint8_t>(val & 0x1F);
                break;
            case 0x70:
                op.sr = static_cast<uint8_t>(val & 0x1F);
                break;
            case 0x80:
                op.sl = static_cast<uint8_t>((val >> 4) & 0x0F);
                op.rr = static_cast<uint8_t>(val & 0x0F);
                break;
            case 0x90:
                op.ssg = static_cast<uint8_t>(val & 0x0F);
                break;
            default:
                break;
        }
    }
}

void YM2612::KeyOn(int ch, uint8_t slotMask) {
    Channel& c = channels_[ch];
    c.keyOn = true;
    for (int i = 0; i < 4; ++i) {
        if ((slotMask & (1u << i)) != 0) {
            c.ops[i].envState = 0;
            c.ops[i].env = 0;
        }
    }
}

void YM2612::KeyOff(int ch, uint8_t slotMask) {
    Channel& c = channels_[ch];
    for (int i = 0; i < 4; ++i) {
        if ((slotMask & (1u << i)) != 0) {
            c.ops[i].envState = 3;
        }
    }
    c.keyOn = false;
}

void YM2612::AdvanceEnvelope(Channel& ch, int op) {
    Operator& o = ch.ops[op];
    switch (o.envState) {
        case 0: // attack
            o.env -= 8 + o.ar;
            if (o.env <= 0) {
                o.env = 0;
                o.envState = 1;
            }
            break;
        case 1: // decay
            o.env += 2 + o.dr;
            if (o.env >= (static_cast<int>(o.sl) * 32)) {
                o.envState = 2;
            }
            break;
        case 2: // sustain
            o.env += o.sr > 0 ? (1 + o.sr / 4) : 0;
            o.env = std::min(o.env, 1023);
            break;
        case 3: // release
            o.env += 2 + o.rr;
            if (o.env >= 1023) {
                o.env = 1023;
                o.envState = 4;
            }
            break;
        default:
            break;
    }
}

int32_t YM2612::CalcOp(int ch, int op, int32_t modInput) const {
    const Channel& c = channels_[ch];
    const Operator& o = c.ops[op];

    const uint32_t phase = o.phase + static_cast<uint32_t>(modInput);
    const int32_t wave = static_cast<int32_t>((phase & 0x80000000u) ? -32767 : 32767);
    const int32_t envAtten = std::min(1023, o.env + static_cast<int>(o.tl) * 8);
    const int32_t out = (wave * (1023 - envAtten)) / 1023;
    return out;
}

int32_t YM2612::SynthChannel(int ch) {
    Channel& c = channels_[ch];

    for (int op = 0; op < 4; ++op) {
        Operator& o = c.ops[op];
        const uint32_t step = (static_cast<uint32_t>(c.fnum) << c.block) * (o.mul + 1u);
        o.phase += step;
        AdvanceEnvelope(c, op);
    }

    // Simplified algorithm routing for bring-up: cascade 1->2->3->4
    int32_t op1 = CalcOp(ch, 0, c.op1Fb >> (7 - c.fb));
    c.op1Fb = (c.op1Fb + op1) / 2;
    int32_t op2 = CalcOp(ch, 1, op1);
    int32_t op3 = CalcOp(ch, 2, op2);
    int32_t op4 = CalcOp(ch, 3, op3);
    return op4;
}

void YM2612::ClockFM() {
    int64_t mixL = 0;
    int64_t mixR = 0;

    for (int ch = 0; ch < 6; ++ch) {
        const int32_t s = SynthChannel(ch);
        const uint8_t pan = channels_[ch].lrpan;
        if ((pan & 0x80) != 0) { mixL += s; }
        if ((pan & 0x40) != 0) { mixR += s; }
    }

    if (audioCb_) {
        constexpr float kNorm = 1.0f / 32768.0f / 6.0f;
        audioCb_(static_cast<float>(mixL) * kNorm,
                 static_cast<float>(mixR) * kNorm);
    }
}

void YM2612::Tick(uint32_t masterCycles) {
    cycleAcc_ += masterCycles;

    // YM2612 clock ~ master/7 => sample time derived from FM clock.
    while (cycleAcc_ >= 7u) {
        cycleAcc_ -= 7u;

        ++lfoCounter_;
        if (lfoEnable_ && lfoRate_ > 0 && lfoCounter_ >= (32 >> std::min<int>(lfoRate_, 5))) {
            lfoCounter_ = 0;
        }

        ClockFM();
    }
}

void YM2612::SaveState(AIO::Emulator::Common::SaveStateWriter& w) const {
    for (const Channel& ch : channels_) {
        for (const Operator& op : ch.ops) {
            w.WriteU8(op.mul); w.WriteU8(op.dt);
            w.WriteU8(op.tl);  w.WriteU8(op.ks);
            w.WriteU8(op.ar);  w.WriteU8(op.dr);
            w.WriteU8(op.sr);  w.WriteU8(op.rr);
            w.WriteU8(op.sl);  w.WriteU8(op.ssg);
            w.WriteU32(op.phase);
            w.WriteU32(static_cast<uint32_t>(op.env));
            w.WriteU8(op.envState);
        }
        w.WriteU16(ch.fnum);
        w.WriteU8(ch.block);
        w.WriteU8(ch.algo);
        w.WriteU8(ch.fb);
        w.WriteU8(ch.ams);
        w.WriteU8(ch.pms);
        w.WriteU8(ch.lrpan);
        w.WriteBool(ch.keyOn);
        w.WriteU32(static_cast<uint32_t>(ch.op1Fb));
    }

    w.WriteU8(addrLatch_[0]);
    w.WriteU8(addrLatch_[1]);
    w.WriteU8(statusReg_);
    w.WriteU32(cycleAcc_);
    w.WriteU32(static_cast<uint32_t>(lfoCounter_));
    w.WriteU8(lfoEnable_);
    w.WriteU8(lfoRate_);
}

void YM2612::LoadState(AIO::Emulator::Common::SaveStateReader& r) {
    for (Channel& ch : channels_) {
        for (Operator& op : ch.ops) {
            op.mul = r.ReadU8(); op.dt = r.ReadU8();
            op.tl  = r.ReadU8(); op.ks = r.ReadU8();
            op.ar  = r.ReadU8(); op.dr = r.ReadU8();
            op.sr  = r.ReadU8(); op.rr = r.ReadU8();
            op.sl  = r.ReadU8(); op.ssg = r.ReadU8();
            op.phase = r.ReadU32();
            op.env = static_cast<int32_t>(r.ReadU32());
            op.envState = r.ReadU8();
        }
        ch.fnum = r.ReadU16();
        ch.block = r.ReadU8();
        ch.algo = r.ReadU8();
        ch.fb = r.ReadU8();
        ch.ams = r.ReadU8();
        ch.pms = r.ReadU8();
        ch.lrpan = r.ReadU8();
        ch.keyOn = r.ReadBool();
        ch.op1Fb = static_cast<int32_t>(r.ReadU32());
    }

    addrLatch_[0] = r.ReadU8();
    addrLatch_[1] = r.ReadU8();
    statusReg_ = r.ReadU8();
    cycleAcc_ = r.ReadU32();
    lfoCounter_ = static_cast<int>(r.ReadU32());
    lfoEnable_ = r.ReadU8();
    lfoRate_ = r.ReadU8();
}

} // namespace AIO::Emulator::Genesis
