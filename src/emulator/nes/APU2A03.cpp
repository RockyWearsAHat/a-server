#include "emulator/nes/APU2A03.h"
#include "emulator/common/SaveState.h"
#include <cstring>

namespace AIO::Emulator::NES {

// ── Static lookup tables ──────────────────────────────────────────────────

const uint16_t APU2A03::kNoiseTimerTable[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

const uint16_t APU2A03::kDmcRateTable[16] = {
    428, 380, 340, 320, 286, 254, 226, 214,
    190, 160, 142, 128, 106, 84, 72, 54
};

const uint8_t APU2A03::kLengthTable[32] = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

const uint8_t APU2A03::kDutyTable[4][8] = {
    {0,1,0,0,0,0,0,0},
    {0,1,1,0,0,0,0,0},
    {0,1,1,1,1,0,0,0},
    {1,0,0,1,1,1,1,1}
};

const uint8_t APU2A03::kTriangleSequence[32] = {
    15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,
    0, 1, 2, 3, 4, 5,6,7,8,9,10,11,12,13,14,15
};

// ── PulseChannel ──────────────────────────────────────────────────────────

float APU2A03::PulseChannel::Output() const noexcept {
    if (!enabled || lengthCounter == 0 || timer < 8) return 0.0f;
    const uint8_t v = constVolume ? volume : envelopeDecay;
    return kDutyTable[duty][dutyPos] ? static_cast<float>(v) / 15.0f : 0.0f;
}

void APU2A03::PulseChannel::TickTimer() {
    if (timer == 0) {
        timer = (timerLow | ((volume & 0x7) << 8)); // reloaded below properly
        dutyPos = (dutyPos + 1) & 0x07;
    } else {
        --timer;
    }
}

void APU2A03::PulseChannel::TickEnvelope() {
    if (envelopeStart) {
        envelopeStart = false;
        envelopeDecay = 15;
        envelopeCounter = volume;
    } else {
        if (envelopeCounter > 0) {
            --envelopeCounter;
        } else {
            envelopeCounter = volume;
            if (envelopeDecay > 0) --envelopeDecay;
            else if (lengthHalt) envelopeDecay = 15;
        }
    }
}

void APU2A03::PulseChannel::TickSweep(bool isPulse2) {
    if (sweepReload) {
        sweepCounter = sweepPeriod;
        sweepReload  = false;
    } else if (sweepCounter > 0) {
        --sweepCounter;
    } else {
        sweepCounter = sweepPeriod;
        if (sweepEnable && sweepShift > 0) {
            const uint16_t period = timerLow | (static_cast<uint16_t>(volume & 0x7) << 8);
            const uint16_t delta  = period >> sweepShift;
            const uint16_t newPeriod = sweepNegate
                ? (isPulse2 ? period - delta : period - delta - 1)
                : period + delta;
            if (newPeriod < 0x800) {
                timerLow = newPeriod & 0xFF;
                // Upper bits stored in volume/length register (simplified)
            }
        }
    }
}

void APU2A03::PulseChannel::TickLength() {
    if (!lengthHalt && lengthCounter > 0) --lengthCounter;
}

// ── TriangleChannel ───────────────────────────────────────────────────────

float APU2A03::TriangleChannel::Output() const noexcept {
    if (!enabled || lengthCounter == 0 || linearCounter == 0) return 0.0f;
    return static_cast<float>(kTriangleSequence[sequencePos]) / 15.0f;
}

void APU2A03::TriangleChannel::TickTimer() {
    if (timer == 0) {
        timer = timerPeriod;
        if (lengthCounter > 0 && linearCounter > 0) {
            sequencePos = (sequencePos + 1) & 0x1F;
        }
    } else {
        --timer;
    }
}

void APU2A03::TriangleChannel::TickLinear() {
    if (linearReload) {
        linearCounter = linearCounterLoad;
    } else if (linearCounter > 0) {
        --linearCounter;
    }
    if (!controlFlag) linearReload = false;
}

void APU2A03::TriangleChannel::TickLength() {
    if (!controlFlag && lengthCounter > 0) --lengthCounter;
}

// ── NoiseChannel ──────────────────────────────────────────────────────────

float APU2A03::NoiseChannel::Output() const noexcept {
    if (!enabled || lengthCounter == 0 || (lfsr & 0x0001)) return 0.0f;
    const uint8_t v = constVolume ? volume : envelopeDecay;
    return static_cast<float>(v) / 15.0f;
}

void APU2A03::NoiseChannel::TickTimer() {
    if (timer == 0) {
        timer = timerPeriod;
        const uint8_t feedback = mode
            ? ((lfsr >> 6) & 0x01) ^ (lfsr & 0x01)
            : ((lfsr >> 1) & 0x01) ^ (lfsr & 0x01);
        lfsr >>= 1;
        lfsr   = (lfsr & 0x3FFF) | (static_cast<uint16_t>(feedback) << 14);
    } else {
        --timer;
    }
}

void APU2A03::NoiseChannel::TickEnvelope() {
    if (envelopeStart) {
        envelopeStart = false;
        envelopeDecay = 15;
        envelopeCounter = volume;
    } else if (envelopeCounter > 0) {
        --envelopeCounter;
    } else {
        envelopeCounter = volume;
        if (envelopeDecay > 0) --envelopeDecay;
        else if (lengthHalt) envelopeDecay = 15;
    }
}

void APU2A03::NoiseChannel::TickLength() {
    if (!lengthHalt && lengthCounter > 0) --lengthCounter;
}

// ── DmcChannel ────────────────────────────────────────────────────────────

float APU2A03::DmcChannel::Output() const noexcept {
    return static_cast<float>(outputLevel) / 127.0f;
}

// ── APU2A03 ───────────────────────────────────────────────────────────────

APU2A03::APU2A03() {
    sampleBuffer_.fill(0.0f);
}

void APU2A03::Reset() {
    pulse1_   = PulseChannel{};
    pulse2_   = PulseChannel{};
    triangle_ = TriangleChannel{};
    noise_    = NoiseChannel{};
    dmc_      = DmcChannel{};
    frameCounterMode_ = 0;
    frameIrqInhibit_  = false;
    irqPending_       = false;
    frameClock_       = 0;
    frameStep_        = 0;
    sampleCount_      = 0;
    sampleTimer_      = 0;
    sampleBuffer_.fill(0.0f);
    noise_.lfsr = 1;
}

void APU2A03::SetIrqCallback(IrqCallback cb)          { onIrq_     = std::move(cb); }
void APU2A03::SetAudioReadyCallback(AudioReadyCallback cb) { audioReady_ = std::move(cb); }
void APU2A03::SetDmcReadCallback(DmcReadCallback cb)       { dmcRead_   = std::move(cb); }

// ── Register I/O ──────────────────────────────────────────────────────────

uint8_t APU2A03::ReadRegister(uint16_t addr) const {
    if (addr == 0x4015) {
        uint8_t status =
            (pulse1_.lengthCounter   > 0 ? 0x01 : 0) |
            (pulse2_.lengthCounter   > 0 ? 0x02 : 0) |
            (triangle_.lengthCounter > 0 ? 0x04 : 0) |
            (noise_.lengthCounter    > 0 ? 0x08 : 0) |
            (dmc_.bytesRemaining     > 0 ? 0x10 : 0) |
            (irqPending_ ? 0x40 : 0);
        irqPending_ = false;
        return status;
    }
    return 0xFF;
}

void APU2A03::WriteRegister(uint16_t addr, uint8_t value) {
    switch (addr) {
        // ── Pulse 1 ──────────────────────────────────────────────────────
        case 0x4000:
            pulse1_.duty       = (value >> 6) & 0x03;
            pulse1_.lengthHalt = (value & 0x20) != 0;
            pulse1_.constVolume= (value & 0x10) != 0;
            pulse1_.volume     = value & 0x0F;
            break;
        case 0x4001:
            pulse1_.sweepEnable = (value & 0x80) >> 7;
            pulse1_.sweepPeriod = (value >> 4) & 0x07;
            pulse1_.sweepNegate = (value & 0x08) != 0;
            pulse1_.sweepShift  = value & 0x07;
            pulse1_.sweepReload = true;
            break;
        case 0x4002:
            pulse1_.timerLow = value;
            break;
        case 0x4003:
            pulse1_.lengthCounter = kLengthTable[value >> 3];
            pulse1_.envelopeStart = true;
            pulse1_.dutyPos       = 0;
            break;
        // ── Pulse 2 ──────────────────────────────────────────────────────
        case 0x4004:
            pulse2_.duty       = (value >> 6) & 0x03;
            pulse2_.lengthHalt = (value & 0x20) != 0;
            pulse2_.constVolume= (value & 0x10) != 0;
            pulse2_.volume     = value & 0x0F;
            break;
        case 0x4005:
            pulse2_.sweepEnable = (value & 0x80) >> 7;
            pulse2_.sweepPeriod = (value >> 4) & 0x07;
            pulse2_.sweepNegate = (value & 0x08) != 0;
            pulse2_.sweepShift  = value & 0x07;
            pulse2_.sweepReload = true;
            break;
        case 0x4006:
            pulse2_.timerLow = value;
            break;
        case 0x4007:
            pulse2_.lengthCounter = kLengthTable[value >> 3];
            pulse2_.envelopeStart = true;
            pulse2_.dutyPos       = 0;
            break;
        // ── Triangle ─────────────────────────────────────────────────────
        case 0x4008:
            triangle_.controlFlag       = (value & 0x80) != 0;
            triangle_.linearCounterLoad = value & 0x7F;
            break;
        case 0x400A:
            triangle_.timerPeriod = (triangle_.timerPeriod & 0xFF00) | value;
            break;
        case 0x400B:
            triangle_.timerPeriod     = (triangle_.timerPeriod & 0x00FF) |
                                        (static_cast<uint16_t>(value & 0x07) << 8);
            triangle_.lengthCounter   = kLengthTable[value >> 3];
            triangle_.linearReload    = true;
            break;
        // ── Noise ─────────────────────────────────────────────────────────
        case 0x400C:
            noise_.lengthHalt  = (value & 0x20) != 0;
            noise_.constVolume = (value & 0x10) != 0;
            noise_.volume      = value & 0x0F;
            break;
        case 0x400E:
            noise_.mode        = (value & 0x80) != 0;
            noise_.timerPeriod = kNoiseTimerTable[value & 0x0F];
            break;
        case 0x400F:
            noise_.lengthCounter = kLengthTable[value >> 3];
            noise_.envelopeStart = true;
            break;
        // ── DMC ──────────────────────────────────────────────────────────
        case 0x4010:
            dmc_.irqEnable = (value & 0x80) != 0;
            dmc_.loop      = (value & 0x40) != 0;
            dmc_.rate      = kDmcRateTable[value & 0x0F];
            break;
        case 0x4011:
            dmc_.outputLevel = value & 0x7F;
            break;
        case 0x4012:
            dmc_.sampleAddress = 0xC000 + (value << 6);
            dmc_.currentAddr   = dmc_.sampleAddress;
            break;
        case 0x4013:
            dmc_.sampleLength   = (value << 4) + 1;
            dmc_.bytesRemaining = dmc_.sampleLength;
            break;
        // ── APU status ────────────────────────────────────────────────────
        case 0x4015:
            pulse1_.enabled    = (value & 0x01) != 0;
            pulse2_.enabled    = (value & 0x02) != 0;
            triangle_.enabled  = (value & 0x04) != 0;
            noise_.enabled     = (value & 0x08) != 0;
            dmc_.enabled       = (value & 0x10) != 0;
            if (!pulse1_.enabled)   pulse1_.lengthCounter  = 0;
            if (!pulse2_.enabled)   pulse2_.lengthCounter  = 0;
            if (!triangle_.enabled) triangle_.lengthCounter= 0;
            if (!noise_.enabled)    noise_.lengthCounter   = 0;
            if (!dmc_.enabled)      dmc_.bytesRemaining    = 0;
            break;
        // ── Frame counter ─────────────────────────────────────────────────
        case 0x4017:
            frameCounterMode_ = (value & 0x80) ? 1 : 0;
            frameIrqInhibit_  = (value & 0x40) != 0;
            frameClock_       = 0;
            frameStep_        = 0;
            if (frameCounterMode_ == 1) {
                TickHalfFrame();
                TickQuarterFrame();
            }
            if (frameIrqInhibit_) irqPending_ = false;
            break;
        default:
            break;
    }
}

// ── Tick ──────────────────────────────────────────────────────────────────

void APU2A03::Tick(uint32_t cpuCycles) {
    for (uint32_t c = 0; c < cpuCycles; ++c) {
        // Timer ticks every CPU cycle for triangle; every 2 for pulse/noise.
        triangle_.TickTimer();
        if ((frameClock_ & 1) == 0) {
            pulse1_.TickTimer();
            pulse2_.TickTimer();
            noise_.TickTimer();
        }
        // DMC: refill sample buffer (DMA-stalls CPU) and clock shift register.
        if (dmc_.enabled) {
            if (dmc_.sampleBufferEmpty && dmc_.bytesRemaining > 0 && dmcRead_) {
                dmc_.sampleBuffer      = dmcRead_(dmc_.currentAddr);
                dmc_.currentAddr       = (dmc_.currentAddr == 0xFFFF)
                                       ? uint16_t{0x8000}
                                       : static_cast<uint16_t>(dmc_.currentAddr + 1);
                --dmc_.bytesRemaining;
                dmc_.sampleBufferEmpty = false;
                // 4-cycle CPU stall per DMA read (Mesen approximation).
                dmcStallCycles_ += 4;
                if (dmc_.bytesRemaining == 0) {
                    if (dmc_.loop) {
                        dmc_.currentAddr    = dmc_.sampleAddress;
                        dmc_.bytesRemaining = dmc_.sampleLength;
                    } else if (dmc_.irqEnable) {
                        irqPending_ = true;
                        if (onIrq_) onIrq_();
                    }
                }
            }
            if (dmc_.timer == 0) {
                dmc_.timer = dmc_.rate > 0 ? static_cast<uint16_t>(dmc_.rate - 1u) : uint16_t{0};
                if (!dmc_.sampleBufferEmpty) {
                    dmc_.silence           = false;
                    dmc_.shiftReg          = dmc_.sampleBuffer;
                    dmc_.bitsRemaining     = 8;
                    dmc_.sampleBufferEmpty = true;
                } else {
                    dmc_.silence = true;
                }
            } else {
                --dmc_.timer;
            }
            if (!dmc_.silence && dmc_.bitsRemaining > 0) {
                if (dmc_.shiftReg & 0x01) {
                    if (dmc_.outputLevel <= 125) dmc_.outputLevel += 2u;
                } else {
                    if (dmc_.outputLevel >= 2)   dmc_.outputLevel -= 2u;
                }
                dmc_.shiftReg >>= 1;
                --dmc_.bitsRemaining;
            }
        }
        TickFrameCounter();
        MixSample();
    }
}

void APU2A03::TickFrameCounter() {
    ++frameClock_;

    static constexpr uint32_t kStep4[4] = { 7457, 14913, 22371, 29829 };
    static constexpr uint32_t kStep5[5] = { 7457, 14913, 22371, 29829, 37281 };

    if (frameCounterMode_ == 0) {
        for (int s = 0; s < 4; ++s) {
            if (frameClock_ == kStep4[s]) {
                TickQuarterFrame();
                if (s == 1 || s == 3) TickHalfFrame();
                if (s == 3) {
                    frameClock_ = 0;
                    if (!frameIrqInhibit_) {
                        irqPending_ = true;
                        if (onIrq_) onIrq_();
                    }
                }
                break;
            }
        }
    } else {
        for (int s = 0; s < 5; ++s) {
            if (frameClock_ == kStep5[s]) {
                TickQuarterFrame();
                if (s == 1 || s == 3) TickHalfFrame();
                if (s == 4) frameClock_ = 0;
                break;
            }
        }
    }
}

void APU2A03::TickQuarterFrame() {
    pulse1_.TickEnvelope();
    pulse2_.TickEnvelope();
    triangle_.TickLinear();
    noise_.TickEnvelope();
}

void APU2A03::TickHalfFrame() {
    pulse1_.TickLength();
    pulse2_.TickLength();
    triangle_.TickLength();
    noise_.TickLength();
    pulse1_.TickSweep(false);
    pulse2_.TickSweep(true);
}

void APU2A03::MixSample() {
    sampleTimer_ += kSampleRate;
    if (sampleTimer_ < kCpuFreqHz) return;
    sampleTimer_ -= kCpuFreqHz;

    const float p1    = pulse1_.Output();
    const float p2    = pulse2_.Output();
    const float tri   = triangle_.Output();
    const float noise = noise_.Output();
    const float dmc   = dmc_.Output();

    // NES mixing formula (approximate linear approximation)
    const float pulseOut = (p1 + p2) > 0.0f
        ? 95.88f / (8128.0f / (p1 + p2) + 100.0f)
        : 0.0f;
    const float tndOut = (tri + noise + dmc) > 0.0f
        ? 159.79f / (1.0f / (tri / 8227.0f + noise / 12241.0f + dmc / 22638.0f) + 100.0f)
        : 0.0f;

    if (sampleCount_ < sampleBuffer_.size()) {
        sampleBuffer_[sampleCount_++] = pulseOut + tndOut;
    }

    if (sampleCount_ >= sampleBuffer_.size()) {
        FlushAudio();
    }
}

void APU2A03::FlushAudio() {
    if (sampleCount_ > 0 && audioReady_) {
        audioReady_(sampleBuffer_.data(), sampleCount_);
    }
    sampleCount_ = 0;
}

// ── Save state ────────────────────────────────────────────────────────────

void APU2A03::SaveState(Common::SaveStateWriter& w) const {
    w.WriteU8(frameCounterMode_);
    w.WriteBool(frameIrqInhibit_);
    w.WriteBool(irqPending_);
    w.WriteU32(frameClock_);
}

void APU2A03::LoadState(Common::SaveStateReader& r) {
    frameCounterMode_ = r.ReadU8();
    frameIrqInhibit_  = r.ReadBool();
    irqPending_       = r.ReadBool();
    frameClock_       = r.ReadU32();
}

} // namespace AIO::Emulator::NES
