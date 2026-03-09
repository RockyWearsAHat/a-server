#pragma once

#include "PS1Constants.h"
#include "emulator/common/Loggable.h"
#include <array>
#include <cstdint>
#include <ostream>
#include <string>

namespace AIO::Emulator::PS1 {

class InterruptController;

struct TimerChannel {
  uint16_t counter = 0;
  uint16_t target = 0;
  uint16_t mode = 0;

  bool syncEnable() const { return mode & 1; }
  uint32_t syncMode() const { return (mode >> 1) & 3; }
  bool resetOnTarget() const { return (mode >> 3) & 1; }
  bool irqOnTarget() const { return (mode >> 4) & 1; }
  bool irqOnOverflow() const { return (mode >> 5) & 1; }
  bool irqRepeat() const { return (mode >> 6) & 1; }
  bool irqToggle() const { return (mode >> 7) & 1; }
  uint32_t clockSource() const { return (mode >> 8) & 3; }
  bool reachedTarget() const { return (mode >> 11) & 1; }
  bool reachedOverflow() const { return (mode >> 12) & 1; }

  void setReachedTarget() { mode |= (1 << 11); }
  void setReachedOverflow() { mode |= (1 << 12); }
  void clearReachedFlags() { mode &= ~(3 << 11); }

  // Internal
  bool irqFlag = true; // Starts high (not requesting), toggled/pulsed
  bool oneShotFired = false;
};

class PS1Timer : public Common::Loggable {
public:
  explicit PS1Timer(InterruptController &interrupts);
  ~PS1Timer() = default;

  void Reset();

  // ─── Register Interface ─────────────────────────────────────────────
  uint32_t Read32(uint32_t addr) const;
  void Write32(uint32_t addr, uint32_t value);

  // ─── Timing ─────────────────────────────────────────────────────────
  void Tick(uint32_t cpuCycles);
  void TickHBlank();                // Called per scanline for timer 1
  void TickDotClock(uint32_t dots); // Called per dot clock for timer 0

  // ─── Debug ──────────────────────────────────────────────────────────
  void DumpState(std::ostream &os) const;
  std::string GetDebugSummary() const;
  const TimerChannel &GetChannel(uint32_t index) const {
    return channels[index];
  }

private:
  InterruptController &interrupts;
  std::array<TimerChannel, Timer::NUM_TIMERS> channels{};

  uint32_t timer2Div8Accum = 0;

  void TickChannel(uint32_t index, uint32_t ticks);
  void CheckIRQ(uint32_t index);
};

} // namespace AIO::Emulator::PS1
