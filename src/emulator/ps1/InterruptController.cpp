#include "emulator/ps1/InterruptController.h"
#include <iomanip>
#include <sstream>

namespace AIO::Emulator::PS1 {

InterruptController::InterruptController() : Loggable("PS1.IRQ") {}

void InterruptController::Reset() {
  iStat = 0;
  iMask = 0;
}

void InterruptController::WriteStat(uint32_t value) {
  // Writing to I_STAT is an acknowledge: bits are cleared by writing 0
  // (write 0 to acknowledge, writing 1 keeps the bit)
  iStat &= value;

  if constexpr (Trace::IRQ_TRACE) {
    LogDebug("I_STAT write: %08X → stat=%08X", value, iStat);
  }
}

void InterruptController::WriteMask(uint32_t value) {
  iMask = value & 0x7FF; // Only 11 interrupt sources

  if constexpr (Trace::IRQ_TRACE) {
    LogDebug("I_MASK write: %08X", iMask);
  }
}

void InterruptController::RequestIRQ(uint32_t irqBit) {
  iStat |= irqBit;

  if constexpr (Trace::IRQ_TRACE) {
    LogDebug("IRQ requested: bit=%08X stat=%08X mask=%08X pending=%d", irqBit,
             iStat, iMask, HasPendingIRQ());
  }
}

void InterruptController::DumpState(std::ostream &os) const {
  os << "=== Interrupt Controller ===" << std::endl;
  os << "I_STAT: " << std::hex << std::setw(8) << std::setfill('0') << iStat
     << std::endl;
  os << "I_MASK: " << std::hex << std::setw(8) << std::setfill('0') << iMask
     << std::endl;
  os << "Pending: " << (HasPendingIRQ() ? "YES" : "NO") << std::endl;
}

std::string InterruptController::GetDebugSummary() const {
  std::ostringstream os;
  os << "IRQ stat=" << std::hex << iStat << " mask=" << iMask
     << " pending=" << HasPendingIRQ();
  return os.str();
}

} // namespace AIO::Emulator::PS1
