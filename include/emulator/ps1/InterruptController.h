#pragma once

#include "PS1Constants.h"
#include "emulator/common/IInterruptController.h"
#include "emulator/common/Loggable.h"
#include <cstdint>
#include <ostream>
#include <string>

namespace AIO::Emulator::PS1 {

class InterruptController : public Common::Loggable,
                            public Common::IInterruptController {
public:
  InterruptController();
  ~InterruptController() = default;

  void Reset() override;

  // ─── Register Interface ─────────────────────────────────────────────
  uint32_t ReadStat() const { return iStat; }
  uint32_t ReadMask() const { return iMask; }
  void WriteStat(uint32_t value);
  void WriteMask(uint32_t value);

  // ─── IRQ Trigger ────────────────────────────────────────────────────
  void RequestIRQ(uint32_t irqBit) override;
  void ClearIRQ(uint32_t irqBit) override { iStat &= ~irqBit; }

  [[nodiscard]] uint32_t PendingBits() const override { return iStat; }
  [[nodiscard]] uint32_t MaskBits() const override { return iMask; }

  // ─── Polling ────────────────────────────────────────────────────────
  bool HasPendingIRQ() const { return Common::IInterruptController::HasPendingIRQ(); }

  // ─── Debug ──────────────────────────────────────────────────────────
  void DumpState(std::ostream &os) const;
  std::string GetDebugSummary() const;

private:
  uint32_t iStat = 0;
  uint32_t iMask = 0;
};

} // namespace AIO::Emulator::PS1
