#pragma once
#include "emulator/common/ISaveStateable.h"
#include <cstdint>
#include <functional>

namespace AIO::Emulator::Genesis {

/// @brief Texas Instruments SN76489 PSG (Programmable Sound Generator).
///
/// Provides three tone channels and one noise channel.
/// Also present in the Sega Master System; the Genesis integrates one inside the VDP chip.
/// Clocked at master / 15 ≈ 3.58 MHz; output sample rate is the chip clock / 16.
///
/// The M68K writes to the PSG via port 0xC00011 (byte write, even or odd).
/// The Z80 writes via port 0x7F.
///
/// Reference: SN76489 data sheet; SMS Power documentation (Tier-1).
///
/// @code
///   SN76489 psg;
///   psg.Write(0x9F); // channel 0 volume = 0 (maximum)
///   psg.Tick(15);    // advance 15 master cycles
/// @endcode
class SN76489 : public AIO::Emulator::Common::ISaveStateable {
public:
    using AudioCallback = std::function<void(float sample)>;

    SN76489()  = default;
    ~SN76489() override = default;

    SN76489(const SN76489&)            = delete;
    SN76489& operator=(const SN76489&) = delete;

    /// Advance by the given number of master-clock cycles.
    /// Calls the audio callback once per output sample period (16 chip cycles).
    void Tick(uint32_t masterCycles);

    /// Write a byte to the PSG data port.
    void Write(uint8_t value);

    /// Set the audio callback invoked per output sample.
    void SetAudioCallback(AudioCallback cb) noexcept { audioCb_ = std::move(cb); }

    // ISaveStateable
    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    // ── Channel state ──────────────────────────────────────────────────────
    struct ToneChannel {
        uint16_t counter {0};    ///< Down-counter (divisor)
        uint16_t period  {0x3FF};///< Tone period register (10 bits)
        uint8_t  volume  {0xF};  ///< Attenuation: 0 = max, 0xF = silent
        bool     output  {false};
    };

    ToneChannel tone_[3];

    // ── Noise channel ──────────────────────────────────────────────────────
    uint16_t noiseCounter_  {0};
    uint16_t noisePeriod_   {0x10};
    uint8_t  noiseCtrl_     {0};   ///< Bits: type (1=white), shift-rate (0-2)
    uint8_t  noiseVol_      {0xF};
    uint16_t noiseLfsr_     {0x8000};
    bool     noiseOutput_   {false};

    // ── Write latch ────────────────────────────────────────────────────────
    uint8_t  latchedCh_     {0};   ///< Last latched channel (0–3)
    bool     latchedType_   {false};///< false=tone, true=volume

    // ── Timing ─────────────────────────────────────────────────────────────
    uint32_t cycleAcc_      {0};   ///< Accumulated master cycles (not yet consumed)
    int      chipCycleAcc_  {0};   ///< Accumulated chip cycles for sample clock

    AudioCallback audioCb_;

    // ── Helpers ────────────────────────────────────────────────────────────
    void       ClockChip();
    [[nodiscard]] float MixOutput() const noexcept;
    static constexpr float kVolumeTable[16] = {
        1.0f, 0.794f, 0.631f, 0.501f,
        0.398f, 0.316f, 0.251f, 0.200f,
        0.158f, 0.126f, 0.100f, 0.079f,
        0.063f, 0.050f, 0.040f, 0.0f
    };
};

} // namespace AIO::Emulator::Genesis
