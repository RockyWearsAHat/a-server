#pragma once

#include "emulator/common/ISaveStateable.h"
#include <array>
#include <cstdint>
#include <functional>

namespace AIO::Emulator::NES {

/// NES APU (RP2A03 subsection).
///
/// Five audio channels:
///   - Pulse 1 and Pulse 2 (square wave, duty cycle configurable)
///   - Triangle (linear counter + length counter gating)
///   - Noise (LFSR-based random noise)
///   - DMC (Delta Modulation Channel — sample playback)
///
/// Register map ($4000–$4017, subset):
///   $4000–$4003  Pulse 1
///   $4004–$4007  Pulse 2
///   $4008–$400B  Triangle
///   $400C–$400F  Noise
///   $4010–$4013  DMC
///   $4015         APU status (channel enable / DMC IRQ)
///   $4017         Frame counter (sequencer mode + IRQ inhibit)
///
/// Output: 44100 Hz mono float samples are accumulated per CPU step and
/// delivered via AudioReadyCallback when a host-controlled buffer is full.
class APU2A03 : public AIO::Emulator::Common::ISaveStateable {
public:
    APU2A03();
    ~APU2A03() override = default;

    using AudioReadyCallback = std::function<void(const float* samples, size_t count)>;

    // ── Lifecycle ─────────────────────────────────────────────────────────

    void Reset();

    /// Tick by @p cpuCycles CPU clocks. Generates audio samples internally.
    void Tick(uint32_t cpuCycles);

    // ── Register I/O (mirrors NESMemory $4000–$4017) ─────────────────────

    [[nodiscard]] uint8_t  ReadRegister (uint16_t addr) const;
    void                   WriteRegister(uint16_t addr, uint8_t value);

    // ── IRQ ───────────────────────────────────────────────────────────────

    using IrqCallback = std::function<void()>;
    void SetIrqCallback(IrqCallback cb);

    [[nodiscard]] bool IrqPending() const noexcept { return irqPending_; }

    // ── Audio output ──────────────────────────────────────────────────────

    void SetAudioReadyCallback(AudioReadyCallback cb);

    // ── ISaveStateable ────────────────────────────────────────────────────

    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    // ── Pulse channel ─────────────────────────────────────────────────────
    struct PulseChannel {
        uint8_t  duty          = 0;
        bool     lengthHalt    = false;
        bool     constVolume   = false;
        uint8_t  volume        = 0;
        uint8_t  sweepEnable   = 0;
        uint8_t  sweepPeriod   = 0;
        bool     sweepNegate   = false;
        uint8_t  sweepShift    = 0;
        uint16_t timerLow      = 0;
        uint8_t  lengthCounter = 0;
        uint8_t  envelopeCounter = 0;
        uint8_t  envelopeDecay   = 0;
        bool     envelopeStart   = false;
        uint16_t timer           = 0;
        uint8_t  dutyPos         = 0;
        bool     enabled         = false;
        bool     sweepReload     = false;
        uint8_t  sweepCounter    = 0;

        [[nodiscard]] float Output() const noexcept;
        void TickTimer();
        void TickEnvelope();
        void TickSweep(bool isPulse2);
        void TickLength();
    };

    // ── Triangle channel ──────────────────────────────────────────────────
    struct TriangleChannel {
        uint8_t  linearCounter     = 0;
        uint8_t  linearCounterLoad = 0;
        bool     controlFlag       = false;
        bool     linearReload      = false;
        uint16_t timerPeriod       = 0;
        uint16_t timer             = 0;
        uint8_t  sequencePos       = 0;
        uint8_t  lengthCounter     = 0;
        bool     enabled           = false;

        [[nodiscard]] float Output() const noexcept;
        void TickTimer();
        void TickLinear();
        void TickLength();
    };

    // ── Noise channel ────────────────────────────────────────────────────
    struct NoiseChannel {
        bool     mode          = false; // LFSR mode (short/long)
        uint16_t timerPeriod   = 0;
        uint16_t timer         = 0;
        uint16_t lfsr          = 1;
        uint8_t  lengthCounter = 0;
        uint8_t  volume        = 0;
        bool     enabled       = false;
        bool     constVolume   = false;
        uint8_t  envelopeCounter = 0;
        uint8_t  envelopeDecay   = 0;
        bool     envelopeStart   = false;
        bool     lengthHalt      = false;

        [[nodiscard]] float Output() const noexcept;
        void TickTimer();
        void TickEnvelope();
        void TickLength();
    };

    // ── DMC channel ──────────────────────────────────────────────────────
    struct DmcChannel {
        bool     irqEnable     = false;
        bool     loop          = false;
        uint8_t  rate          = 0;       // index into period table
        uint8_t  outputLevel   = 0;
        uint16_t sampleAddress = 0;
        uint16_t sampleLength  = 0;
        uint16_t currentAddr   = 0;
        uint16_t bytesRemaining= 0;
        uint16_t timer         = 0;
        uint8_t  shiftReg      = 0;
        uint8_t  bitsRemaining = 0;
        uint8_t  sampleBuffer  = 0;
        bool     sampleBufferEmpty = true;
        bool     enabled       = false;
        bool     silence       = true;

        [[nodiscard]] float Output() const noexcept;
    };

    PulseChannel    pulse1_;
    PulseChannel    pulse2_;
    TriangleChannel triangle_;
    NoiseChannel    noise_;
    DmcChannel      dmc_;

    // ── Frame counter (sequencer) ─────────────────────────────────────────
    uint8_t  frameCounterMode_ = 0; // 0=4-step, 1=5-step
    bool     frameIrqInhibit_  = false;
    mutable bool irqPending_  = false;
    uint32_t frameClock_       = 0;
    int      frameStep_        = 0;

    // ── Audio output ──────────────────────────────────────────────────────
    std::array<float, 4096> sampleBuffer_{};
    size_t       sampleCount_ = 0;
    uint32_t     sampleTimer_ = 0;
    static constexpr uint32_t kSampleRate    = 44100;
    static constexpr uint32_t kCpuFreqHz     = 1789773;

    AudioReadyCallback audioReady_;
    IrqCallback        onIrq_;

    void TickFrameCounter();
    void TickHalfFrame();
    void TickQuarterFrame();
    void MixSample();
    void FlushAudio();

    static const uint16_t kNoiseTimerTable[16];
    static const uint16_t kDmcRateTable[16];
    static const uint8_t  kLengthTable[32];
    static const uint8_t  kDutyTable[4][8];
    static const uint8_t  kTriangleSequence[32];
};

} // namespace AIO::Emulator::NES
