#pragma once

#include "PS1Constants.h"
#include "emulator/common/Loggable.h"
#include <cstdint>
#include <ostream>
#include <string>

namespace AIO::Emulator::PS1 {

class InterruptController : public Common::Loggable {
public:
  InterruptController();
  ~InterruptController() = default;

  void Reset();

  // ─── Register Interface ─────────────────────────────────────────────
  uint32_t ReadStat() const { return iStat; }
  uint32_t ReadMask() const { return iMask; }
  void WriteStat(uint32_t value);
  void WriteMask(uint32_t value);

  // ─── IRQ Trigger ────────────────────────────────────────────────────
  void RequestIRQ(uint32_t irqBit);
  void ClearIRQ(uint32_t irqBit) { iStat &= ~irqBit; }

  // ─── Polling ────────────────────────────────────────────────────────
  bool HasPendingIRQ() const { return (iStat & iMask) != 0; }

  // ─── Debug ──────────────────────────────────────────────────────────
  void DumpState(std::ostream &os) const;
  std::string GetDebugSummary() const;

private:
  uint32_t iStat = 0;
  uint32_t iMask = 0;
};

} // namespace AIO::Emulator::PS1
