#include "emulator/atari2600/PIA6532.h"

namespace Atari2600 {

PIA6532::PIA6532() noexcept {
    Reset();
}

void PIA6532::Reset() noexcept {
    ram_.fill(0);
    ddra_      = 0;
    ddrb_      = 0;
    portAOut_  = 0;
    portBOut_  = 0;
    portAIn_   = 0xFF;
    portBIn_   = 0xFF;
    timerInterval_   = 1;
    timerValue_      = 0xFF;
    timerCounter_    = 0xFF;
    timerIRQEnabled_ = false;
    timerIRQFlag_    = false;
    clockCount_      = 0;
}

void PIA6532::Tick() noexcept {
    clockCount_++;
    if (timerCounter_ > 0) {
        timerCounter_--;
    } else {
        // Timer underflowed — decrement by 1 each cycle (wrap mode)
        timerIRQFlag_ = true;
    }
}

uint8_t PIA6532::Read(uint16_t addr) noexcept {
    const uint16_t a = addr & 0x07FF;

    // RAM: chip-select bit 9 = 0, bit 7 = 1 selects RAM (0x80–0xFF)
    // We key off the two decoded chip-select signals from Atari2600Memory.
    // When addr bit 9=1, addr bit 7=0 → registers; bit 7=1 → RAM
    if (a & 0x0200) {
        // Register space
        if (a & 0x80) {
            // RAM read via register space (rare)
            return ram_[a & 0x7F];
        }
        // I/O and timer registers
        switch (a & 0x07) {
            case 0x00: // SWCHA — Port A (joystick)
                return (portAIn_ & ~ddra_) | (portAOut_ & ddra_);
            case 0x01: // SWACNT — Port A DDR
                return ddra_;
            case 0x02: // SWCHB — Port B (console switches)
                return (portBIn_ & ~ddrb_) | (portBOut_ & ddrb_);
            case 0x03: // SWBCNT — Port B DDR
                return ddrb_;
            case 0x04: case 0x06: { // INTIM — timer read
                timerIRQFlag_ = false;
                const uint8_t v = static_cast<uint8_t>(timerCounter_ / timerInterval_);
                return v;
            }
            case 0x05: case 0x07: { // TIMINT — timer + interrupt status
                const uint8_t v = static_cast<uint8_t>(timerCounter_ / timerInterval_);
                return (timerIRQFlag_ ? 0x80u : 0u) | v;
            }
            default: return 0;
        }
    } else {
        // RAM read (addr bit 7 high = 0x80–0xFF in 2600 address space)
        return ram_[a & 0x7F];
    }
}

void PIA6532::Write(uint16_t addr, uint8_t v) noexcept {
    const uint16_t a = addr & 0x07FF;

    if (a & 0x0200) {
        if (a & 0x80) {
            ram_[a & 0x7F] = v;
            return;
        }
        // Register writes
        // Timer writes: addr bits 0-1 select interval; bit 3 selects IRQ enable
        const bool isTimer = (a & 0x14) == 0x14; // bit 4=1, bit 2=1
        if (isTimer) {
            timerIRQEnabled_ = (a & 0x08) != 0;
            timerValue_      = v;
            switch (a & 0x03) {
                case 0: timerInterval_ = 1;    break;
                case 1: timerInterval_ = 8;    break;
                case 2: timerInterval_ = 64;   break;
                case 3: timerInterval_ = 1024; break;
            }
            timerCounter_ = static_cast<uint32_t>(v) * timerInterval_;
            timerIRQFlag_ = false;
            return;
        }
        switch (a & 0x07) {
            case 0x00: portAOut_ = v; break;
            case 0x01: ddra_     = v; break;
            case 0x02: portBOut_ = v; break;
            case 0x03: ddrb_     = v; break;
            default: break;
        }
    } else {
        ram_[a & 0x7F] = v;
    }
}

} // namespace Atari2600
