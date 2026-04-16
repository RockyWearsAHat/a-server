#pragma once
#include "emulator/common/ISaveStateable.h"
#include <array>
#include <cstdint>
#include <functional>

namespace AIO::Emulator::Genesis {

/// @brief Yamaha YM2612 / OPN2 6-channel FM synthesizer.
///
/// Produces 44,100 Hz stereo PCM audio via FM synthesis.
/// The Z80 addresses the YM2612 through two register-set pairs:
///   - Part 1: address 0x4000, data 0x4001 (channels 1–3)
///   - Part 2: address 0x4002, data 0x4003 (channels 4–6)
///
/// The M68K can also access the YM2612 directly.
///
/// Reference: Yamaha OPN2 application manual + Nemesis's YM2612 notes (Tier-1/2).
///
/// @code
///   YM2612 ym;
///   ym.Write(0x00, 0x28); // key-on register
///   ym.Write(0x01, 0xF0); // key-on all operators, channel 0
///   ym.Tick(7);            // advance 7 master cycles
/// @endcode
class YM2612 : public AIO::Emulator::Common::ISaveStateable {
public:
    /// Audio ready callback — delivers one stereo sample pair [L, R] in [-1.0, 1.0].
    using AudioCallback = std::function<void(float left, float right)>;

    YM2612()  = default;
    ~YM2612() override = default;

    YM2612(const YM2612&)            = delete;
    YM2612& operator=(const YM2612&) = delete;

    /// Advance by the given number of master-clock cycles.
    /// Calls the audio callback once per output sample period (~162 master cycles).
    void Tick(uint32_t masterCycles);

    /// Read the YM2612 status register.
    [[nodiscard]] uint8_t Read() const noexcept;

    /// Write a byte to the YM2612.
    /// @param port 0 or 2 selects the address register; 1 or 3 selects the data register.
    /// @param value 8-bit value to write.
    void Write(uint8_t port, uint8_t value);

    /// Set the audio callback invoked per output sample.
    void SetAudioCallback(AudioCallback cb) noexcept { audioCb_ = std::move(cb); }

    // ISaveStateable
    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    // ── FM operator state ─────────────────────────────────────────────────
    struct Operator {
        uint8_t  mul  {0};   ///< Frequency multiplier
        uint8_t  dt   {0};   ///< Detune
        uint8_t  tl   {0};   ///< Total level (attenuation)
        uint8_t  ks   {0};   ///< Key scaling rate
        uint8_t  ar   {0};   ///< Attack rate
        uint8_t  dr   {0};   ///< Decay rate
        uint8_t  sr   {0};   ///< Sustain rate
        uint8_t  rr   {0};   ///< Release rate
        uint8_t  sl   {0};   ///< Sustain level
        uint8_t  ssg  {0};   ///< SSG-EG envelope type
        uint32_t phase{0};   ///< Phase accumulator
        int32_t  env  {0};   ///< Envelope value (in dB units × 8)
        uint8_t  envState{0};///< 0=attack, 1=decay, 2=sustain, 3=release, 4=off
    };

    struct Channel {
        Operator ops[4];
        uint16_t fnum   {0};
        uint8_t  block  {0};
        uint8_t  algo   {0};
        uint8_t  fb     {0};  ///< Feedback level for OP1
        uint8_t  ams    {0};
        uint8_t  pms    {0};
        uint8_t  lrpan  {0};  ///< L/R pan + AMS/PMS
        bool     keyOn  {false};
        int32_t  op1Fb  {0};  ///< OP1 feedback accumulator
    };

    std::array<Channel, 6> channels_ {};

    uint8_t  addrLatch_[2] {}; ///< Pending address for part 1 and part 2
    uint8_t  statusReg_   {0};
    uint32_t cycleAcc_    {0};
    int      lfoCounter_  {0};
    uint8_t  lfoEnable_   {0};
    uint8_t  lfoRate_     {0};

    AudioCallback audioCb_;

    // ── Helpers ────────────────────────────────────────────────────────────
    void WriteReg(uint8_t part, uint8_t reg, uint8_t val);
    void ClockFM();
    [[nodiscard]] int32_t SynthChannel(int ch);
    void KeyOn (int ch, uint8_t slotMask);
    void KeyOff(int ch, uint8_t slotMask);
    [[nodiscard]] int32_t CalcOp(int ch, int op, int32_t modInput) const;
    void AdvanceEnvelope(Channel& ch, int op);
};

} // namespace AIO::Emulator::Genesis
