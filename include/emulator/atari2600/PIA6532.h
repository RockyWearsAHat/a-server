#pragma once
// PIA 6532 (RIOT) — Peripheral Interface Adapter used in the Atari 2600.
// Provides: 128 bytes RAM, two 8-bit I/O ports (A for joystick, B for console switches),
// and a programmable interval timer.

#include <cstdint>
#include <array>

namespace Atari2600 {

class PIA6532 {
public:
    PIA6532() noexcept;
    ~PIA6532() = default;

    PIA6532(const PIA6532&)            = delete;
    PIA6532& operator=(const PIA6532&) = delete;

    void    Reset()  noexcept;
    void    Tick()   noexcept;  // Called once per CPU cycle

    // Memory-mapped I/O: addr matches bits 7..0 of RIOT address space
    // RAM: 0x80–0xFF (128 bytes, mirrors at 0x00–0x7F within RIOT window)
    // Registers: 0x00–0x1F (chip select decoding handled by Atari2600Memory)
    uint8_t Read(uint16_t addr)             noexcept;
    void    Write(uint16_t addr, uint8_t v) noexcept;

    // Port inputs — set by the console/cartridge logic
    void SetPortA(uint8_t val) noexcept { portAIn_ = val; }  // Joystick directions
    void SetPortB(uint8_t val) noexcept { portBIn_ = val; }  // Console switches

private:
    std::array<uint8_t, 128> ram_{};

    uint8_t ddra_     = 0;     // Data direction register A (1 = output)
    uint8_t ddrb_     = 0;     // Data direction register B
    uint8_t portAOut_ = 0;     // Output register A
    uint8_t portBOut_ = 0;     // Output register B
    uint8_t portAIn_  = 0xFF;  // External input A
    uint8_t portBIn_  = 0xFF;  // External input B

    // Timer
    uint32_t timerInterval_  = 1;    // Divider: 1, 8, 64, or 1024
    uint32_t timerCounter_   = 0xFF * 1; // Current countdown (interval * initial)
    uint8_t  timerValue_     = 0xFF; // Last programmed value
    bool     timerIRQEnabled_ = false;
    bool     timerIRQFlag_    = false;
    uint32_t clockCount_     = 0;
};

} // namespace Atari2600
