#include "emulator/ps1/PS1Timer.h"
#include "emulator/ps1/InterruptController.h"
#include <iomanip>
#include <sstream>

namespace AIO::Emulator::PS1 {

PS1Timer::PS1Timer(InterruptController &interrupts)
    : Loggable("PS1.TMR"), interrupts(interrupts) {}

void PS1Timer::Reset() {
  for (auto &ch : channels) {
    ch.counter = 0;
    ch.target = 0;
    ch.mode = 0;
    ch.irqFlag = true;
  }
  timer2Div8Accum = 0;
}

uint32_t PS1Timer::Read32(uint32_t addr) const {
  uint32_t timerIndex = (addr - IO::TIMER_BASE) / IO::TIMER_CHANNEL_SIZE;
  uint32_t reg = (addr - IO::TIMER_BASE) % IO::TIMER_CHANNEL_SIZE;

  if (timerIndex >= Timer::NUM_TIMERS)
    return 0;

  const auto &ch = channels[timerIndex];

  switch (reg) {
  case 0x00:
    return ch.counter;
  case 0x04: {
    // Reading mode clears reached flags (bits 11-12) after returning current
    // value
    uint32_t value = ch.mode;
    const_cast<TimerChannel &>(ch).clearReachedFlags();
    return value;
  }
  case 0x08:
    return ch.target;
  default:
    return 0;
  }
}

void PS1Timer::Write32(uint32_t addr, uint32_t value) {
  uint32_t timerIndex = (addr - IO::TIMER_BASE) / IO::TIMER_CHANNEL_SIZE;
  uint32_t reg = (addr - IO::TIMER_BASE) % IO::TIMER_CHANNEL_SIZE;

  if (timerIndex >= Timer::NUM_TIMERS)
    return;

  auto &ch = channels[timerIndex];

  switch (reg) {
  case 0x00:
    ch.counter = static_cast<uint16_t>(value);
    break;
  case 0x04:
    // Writing mode resets counter and clears reached flags
    ch.mode = static_cast<uint16_t>(value & 0x03FF); // Bits 0-9 are writable
    ch.counter = 0;
    ch.irqFlag = true; // Reset IRQ flag (not requesting)
    ch.clearReachedFlags();
    if constexpr (Trace::TIMER_TRACE) {
      LogDebug("Timer%u mode=%04X (sync=%d syncMode=%u resetOnTarget=%d "
               "irqTarget=%d irqOvf=%d repeat=%d toggle=%d clkSrc=%u)",
               timerIndex, ch.mode, ch.syncEnable(), ch.syncMode(),
               ch.resetOnTarget(), ch.irqOnTarget(), ch.irqOnOverflow(),
               ch.irqRepeat(), ch.irqToggle(), ch.clockSource());
    }
    break;
  case 0x08:
    ch.target = static_cast<uint16_t>(value);
    break;
  }
}

void PS1Timer::Tick(uint32_t cpuCycles) {
  // Timer 2 always uses system clock (or system/8)
  uint32_t timer2Source = channels[2].clockSource();
  if (timer2Source == 0 || timer2Source == 1) {
    TickChannel(2, cpuCycles);
  } else {
    // System clock / 8 — accumulate sub-cycles to avoid integer truncation
    timer2Div8Accum += cpuCycles;
    uint32_t ticks = timer2Div8Accum / 8;
    timer2Div8Accum %= 8;
    if (ticks > 0) {
      TickChannel(2, ticks);
    }
  }

  // Timer 0 and Timer 1 use system clock unless alternative source is selected
  // Timer 0 alternate = dot clock (handled by TickDotClock)
  // Timer 1 alternate = hblank (handled by TickHBlank)
  if (channels[0].clockSource() == 0 || channels[0].clockSource() == 2) {
    TickChannel(0, cpuCycles);
  }
  if (channels[1].clockSource() == 0 || channels[1].clockSource() == 2) {
    TickChannel(1, cpuCycles);
  }
}

void PS1Timer::TickHBlank() {
  // Timer 1 can be clocked by hblank
  if (channels[1].clockSource() == 1 || channels[1].clockSource() == 3) {
    TickChannel(1, 1);
  }
}

void PS1Timer::TickDotClock(uint32_t dots) {
  // Timer 0 can be clocked by dot clock
  if (channels[0].clockSource() == 1 || channels[0].clockSource() == 3) {
    TickChannel(0, dots);
  }
}

void PS1Timer::TickChannel(uint32_t index, uint32_t ticks) {
  auto &ch = channels[index];

  for (uint32_t i = 0; i < ticks; i++) {
    uint16_t prevCounter = ch.counter;
    ch.counter++;

    // Check target reached
    if (ch.counter == ch.target) {
      ch.setReachedTarget();
      if (ch.resetOnTarget()) {
        ch.counter = 0;
      }
      if (ch.irqOnTarget()) {
        CheckIRQ(index);
      }
    }

    // Check overflow (0xFFFF → 0x0000)
    if (prevCounter == 0xFFFF && ch.counter == 0) {
      ch.setReachedOverflow();
      if (ch.irqOnOverflow()) {
        CheckIRQ(index);
      }
    }
  }
}

void PS1Timer::CheckIRQ(uint32_t index) {
  auto &ch = channels[index];

  bool shouldFire = false;

  if (ch.irqToggle()) {
    // Toggle mode: flip IRQ flag each time
    ch.irqFlag = !ch.irqFlag;
    shouldFire = !ch.irqFlag; // Fire when flag goes low
  } else {
    // Pulse mode: briefly set flag low
    ch.irqFlag = false;
    shouldFire = true;
  }

  if (shouldFire) {
    // Timer 0 = IRQ::TIMER0, Timer 1 = IRQ::TIMER1, Timer 2 = IRQ::TIMER2
    uint32_t irqBit = IRQ::TIMER0 << index;
    interrupts.RequestIRQ(irqBit);

    if constexpr (Trace::TIMER_TRACE) {
      LogDebug("Timer%u IRQ fired (counter=%04X target=%04X)", index,
               ch.counter, ch.target);
    }

    // Non-repeat mode: disable further IRQs
    if (!ch.irqRepeat()) {
      // Don't fire again (pulse once)
      ch.irqFlag = true;
    }
  }
}

void PS1Timer::DumpState(std::ostream &os) const {
  os << "=== PS1 Timers ===" << std::endl;
  for (uint32_t i = 0; i < Timer::NUM_TIMERS; i++) {
    const auto &ch = channels[i];
    os << "Timer" << i << " counter=" << std::hex << ch.counter
       << " target=" << ch.target << " mode=" << ch.mode << std::endl;
  }
}

std::string PS1Timer::GetDebugSummary() const {
  std::ostringstream os;
  for (uint32_t i = 0; i < Timer::NUM_TIMERS; i++) {
    os << "T" << i << "=" << std::hex << channels[i].counter << " ";
  }
  return os.str();
}

} // namespace AIO::Emulator::PS1
