#pragma once

#include "ISchedulable.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace AIO::Emulator::Common {

/// Deterministic, cycle-accurate event scheduler.
///
/// The Scheduler drives the entire emulated system forward in lock-step.
/// Every call to RunUntil() fires queued events in chronological order,
/// then advances registered ISchedulable subsystems through the remaining
/// cycles. The same input sequence will always produce the same output.
///
/// Typical usage per console:
///   scheduler.Register(cpu, cpuCycleRatio);    // e.g. 1:1
///   scheduler.Register(ppu, ppuCycleRatio);    // e.g. 3:1 (NTSC NES)
///   scheduler.Register(apu, apuCycleRatio);    // e.g. 1:1
///   scheduler.Schedule(341*3, [&]{ ppu.GenerateNMI(); });
///   scheduler.RunUntil(frameEndCycle);
///
/// Thread safety: none. The Scheduler is owned by the console class and
/// all calls originate from the emulation thread.
class Scheduler {
public:
    Scheduler();
    ~Scheduler() = default;

    Scheduler(const Scheduler&)            = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    // ── Subsystem registration ────────────────────────────────────────────

    /// Register @p subsystem to be ticked by the master clock.
    ///
    /// @param subsystem     Non-owning pointer; must outlive the Scheduler.
    /// @param cycleRatio    Number of master-clock ticks per subsystem tick.
    ///                      E.g. 3 means the PPU ticks once per 3 CPU cycles.
    ///                      Must be >= 1.
    void Register(ISchedulable* subsystem, uint32_t cycleRatio);

    // ── Event scheduling ─────────────────────────────────────────────────

    /// Schedule @p callback to fire at master-clock cycle (currentCycle + cyclesFromNow).
    ///
    /// @param cyclesFromNow  Delay from now. Must be > 0.
    /// @param callback       Zero-arg callable. Must not be null.
    /// @return               Absolute cycle at which the event is scheduled;
    ///                       useful for debugging or repeating events.
    uint64_t Schedule(uint64_t cyclesFromNow, std::function<void()> callback);

    // ── Execution ────────────────────────────────────────────────────────

    /// Advance the system to @p targetCycle, firing all queued events and
    /// ticking all registered subsystems on the way.
    ///
    /// @param targetCycle  Absolute master cycle. Must be >= currentCycle_.
    void RunUntil(uint64_t targetCycle);

    /// Step exactly one quantum (the smallest registered cycle ratio).
    /// Useful for tight single-step debugging.
    void StepOnce();

    // ── Clock queries ────────────────────────────────────────────────────

    /// Current master clock cycle count since last Reset().
    [[nodiscard]] uint64_t CurrentCycle() const noexcept { return currentCycle_; }

    /// Reset cycle counter and clear all pending events and subsystems.
    void Reset();

private:
    struct ScheduledEvent {
        uint64_t            fireCycle;
        uint64_t            insertionOrder; // tie-break for equal fireCycle
        std::function<void()> callback;

        bool operator>(const ScheduledEvent& rhs) const;
    };

    struct RegisteredSubsystem {
        ISchedulable* subsystem;
        uint32_t      cycleRatio;
        uint64_t      accumulator; // fractional cycles owed to this subsystem
    };

    uint64_t currentCycle_    = 0;
    uint64_t insertionCounter_ = 0;

    // Min-heap ordered by fireCycle (smallest cycle at top).
    std::vector<ScheduledEvent>    eventHeap_;
    std::vector<RegisteredSubsystem> subsystems_;

    void FirePendingEvents(uint64_t upToCycle);
    void AdvanceSubsystems(uint64_t deltaCycles);
};

} // namespace AIO::Emulator::Common
