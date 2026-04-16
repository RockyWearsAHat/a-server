#pragma once

#include <cstdint>

namespace AIO::Emulator::Common {

/// Interface for any subsystem that participates in clock-driven execution.
///
/// The Scheduler advances the system by ticking each registered ISchedulable
/// by a quantum of master-clock cycles. Implementors must:
///   - Execute exactly tickCycles worth of cycles and return that value.
///   - Never read global mutable state that could vary across replay runs.
///   - Produce the same output for the same input sequence (determinism).
///
/// Example: a CPU implementation ticks through instructions and returns the
/// actual cycles consumed; a PPU ticks scanline rendering logic.
class ISchedulable {
public:
    virtual ~ISchedulable() = default;

    /// Advance this subsystem by tickCycles master-clock cycles.
    ///
    /// @param tickCycles  Number of master-clock cycles to execute. Must be > 0.
    /// @return            Number of cycles actually consumed. Must equal tickCycles.
    [[nodiscard]] virtual uint64_t Tick(uint64_t tickCycles) = 0;

    /// Human-readable subsystem identifier (e.g., "CPU", "PPU").
    /// Used by the Scheduler for diagnostic output.
    [[nodiscard]] virtual const char* SubsystemName() const = 0;
};

} // namespace AIO::Emulator::Common
