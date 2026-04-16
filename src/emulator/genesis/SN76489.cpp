// SN76489.cpp — PSG synthesis scaffold for Genesis.

#include "emulator/genesis/SN76489.h"
#include "emulator/common/SaveState.h"

namespace AIO::Emulator::Genesis {

void SN76489::Write(uint8_t value) {
    if ((value & 0x80) != 0) {
        // Latch/data byte
        latchedCh_ = static_cast<uint8_t>((value >> 5) & 0x03);
        latchedType_ = ((value & 0x10) != 0); // 1=volume, 0=tone/noise
        const uint8_t data = value & 0x0F;

        if (latchedCh_ < 3) {
            if (latchedType_) {
                tone_[latchedCh_].volume = data;
            } else {
                tone_[latchedCh_].period = static_cast<uint16_t>((tone_[latchedCh_].period & 0x3F0u) | data);
                if (tone_[latchedCh_].period == 0) { tone_[latchedCh_].period = 1; }
            }
        } else {
            if (latchedType_) {
                noiseVol_ = data;
            } else {
                noiseCtrl_ = data;
                const uint8_t rate = static_cast<uint8_t>(noiseCtrl_ & 0x03);
                switch (rate) {
                    case 0: noisePeriod_ = 0x10; break;
                    case 1: noisePeriod_ = 0x20; break;
                    case 2: noisePeriod_ = 0x40; break;
                    default: noisePeriod_ = tone_[2].period; break;
                }
            }
        }
        return;
    }

    // Data byte (continues prior latch)
    const uint8_t data = value & 0x3F;
    if (latchedCh_ < 3 && !latchedType_) {
        uint16_t p = tone_[latchedCh_].period;
        p = static_cast<uint16_t>((p & 0x000Fu) | (static_cast<uint16_t>(data) << 4));
        tone_[latchedCh_].period = (p == 0) ? 1 : p;
    }
}

void SN76489::ClockChip() {
    for (int i = 0; i < 3; ++i) {
        ToneChannel& ch = tone_[i];
        if (ch.counter == 0) {
            ch.counter = ch.period;
            ch.output = !ch.output;
        } else {
            --ch.counter;
        }
    }

    if (noiseCounter_ == 0) {
        noiseCounter_ = noisePeriod_;
        const bool white = (noiseCtrl_ & 0x4) != 0;
        const uint16_t feedback = white
            ? static_cast<uint16_t>((noiseLfsr_ ^ (noiseLfsr_ >> 1)) & 1u)
            : static_cast<uint16_t>(noiseLfsr_ & 1u);
        noiseLfsr_ = static_cast<uint16_t>((noiseLfsr_ >> 1) | (feedback << 15));
        noiseOutput_ = (noiseLfsr_ & 1u) != 0;
    } else {
        --noiseCounter_;
    }
}

float SN76489::MixOutput() const noexcept {
    float mix = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float amp = kVolumeTable[tone_[i].volume & 0x0F];
        mix += tone_[i].output ? amp : -amp;
    }
    {
        const float amp = kVolumeTable[noiseVol_ & 0x0F];
        mix += noiseOutput_ ? amp : -amp;
    }

    // Normalize roughly to [-1, 1].
    return mix * 0.25f;
}

void SN76489::Tick(uint32_t masterCycles) {
    // Chip runs at master/15.
    cycleAcc_ += masterCycles;
    while (cycleAcc_ >= 15u) {
        cycleAcc_ -= 15u;
        ++chipCycleAcc_;

        ClockChip();

        // Output sample every 16 chip cycles.
        if (chipCycleAcc_ >= 16) {
            chipCycleAcc_ -= 16;
            if (audioCb_) {
                audioCb_(MixOutput());
            }
        }
    }
}

void SN76489::SaveState(AIO::Emulator::Common::SaveStateWriter& w) const {
    for (const ToneChannel& ch : tone_) {
        w.WriteU16(ch.counter);
        w.WriteU16(ch.period);
        w.WriteU8(ch.volume);
        w.WriteBool(ch.output);
    }

    w.WriteU16(noiseCounter_);
    w.WriteU16(noisePeriod_);
    w.WriteU8(noiseCtrl_);
    w.WriteU8(noiseVol_);
    w.WriteU16(noiseLfsr_);
    w.WriteBool(noiseOutput_);

    w.WriteU8(latchedCh_);
    w.WriteBool(latchedType_);
    w.WriteU32(cycleAcc_);
    w.WriteU32(static_cast<uint32_t>(chipCycleAcc_));
}

void SN76489::LoadState(AIO::Emulator::Common::SaveStateReader& r) {
    for (ToneChannel& ch : tone_) {
        ch.counter = r.ReadU16();
        ch.period = r.ReadU16();
        ch.volume = r.ReadU8();
        ch.output = r.ReadBool();
    }

    noiseCounter_ = r.ReadU16();
    noisePeriod_ = r.ReadU16();
    noiseCtrl_ = r.ReadU8();
    noiseVol_ = r.ReadU8();
    noiseLfsr_ = r.ReadU16();
    noiseOutput_ = r.ReadBool();

    latchedCh_ = r.ReadU8();
    latchedType_ = r.ReadBool();
    cycleAcc_ = r.ReadU32();
    chipCycleAcc_ = static_cast<int>(r.ReadU32());
}

} // namespace AIO::Emulator::Genesis
