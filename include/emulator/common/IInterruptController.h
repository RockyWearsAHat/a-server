#pragma once

#include <cstdint>

namespace AIO::Emulator::Common {

/// Common interrupt controller contract shared by emulator cores.
///
/// Implementations own hardware-specific register behavior, while this
/// interface gives shared code a stable way to route and poll IRQ state.
class IInterruptController {
public:
  virtual ~IInterruptController() = default;

  virtual void Reset() = 0;

  /// Raise one or more interrupt source bits.
  virtual void RequestIRQ(uint32_t irqBits) = 0;

  /// Clear one or more interrupt source bits.
  virtual void ClearIRQ(uint32_t irqBits) = 0;

  /// Raw pending-source register bits (controller status register).
  [[nodiscard]] virtual uint32_t PendingBits() const = 0;

  /// Raw enable/mask register bits.
  [[nodiscard]] virtual uint32_t MaskBits() const = 0;

  /// Generic pending predicate used by shared scheduler/runner utilities.
  [[nodiscard]] bool HasPendingIRQ() const {
    return (PendingBits() & MaskBits()) != 0;
  }
};

} // namespace AIO::Emulator::Common
