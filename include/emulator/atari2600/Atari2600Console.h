#pragma once
// Atari 2600 top-level console.
// Ties together MOS 6507, TIA, PIA 6532, and memory bus.
// ROM formats: .a26, .bin (both raw binary dumps, typically 2–32 KB).

#include <cstdint>
#include <string>
#include <memory>

namespace Atari2600 {

class MOS6507;
class TIA;
class PIA6532;
class Atari2600Memory;

class Atari2600Console {
public:
    Atari2600Console();
    ~Atari2600Console();

    Atari2600Console(const Atari2600Console&)            = delete;
    Atari2600Console& operator=(const Atari2600Console&) = delete;

    // Load a .a26 / .bin ROM image from disk
    bool LoadROM(const std::string& path);

    void Reset();

    // Run one CPU instruction (plus the corresponding TIA ticks at 3:1 ratio)
    void Step();

    // Run until TIA reports a complete frame
    void RunFrame();

    const uint32_t* GetFramebuffer() const noexcept;
    static constexpr int kWidth  = 160;
    static constexpr int kHeight = 192;

    // Controller input: 8-bit joystick bitmask (bits: 0=up,1=dn,2=lf,3=rt,4=fire)
    void SetJoystick(int port, uint8_t state) noexcept;

    bool IsROMLoaded() const noexcept { return romLoaded_; }

private:
    std::unique_ptr<TIA>             tia_;
    std::unique_ptr<PIA6532>         pia_;
    std::unique_ptr<Atari2600Memory> mem_;
    std::unique_ptr<MOS6507>         cpu_;

    bool romLoaded_ = false;
    uint8_t portA_  = 0xFF; // Current Port A state (joystick inputs, active-low)
};

} // namespace Atari2600
