// Scheduler.cpp — deterministic cycle-accurate event scheduler.
//
// Design note: subsystem advancement uses a Bresenham-style per-subsystem
// accumulator so that fractional clock ratios never accumulate rounding
// error across a session.

#include "emulator/common/Scheduler.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace AIO::Emulator::Common {

// ── ScheduledEvent comparison ──────────────────────────────────────────────

bool Scheduler::ScheduledEvent::operator>(const ScheduledEvent& rhs) const {
    if (fireCycle != rhs.fireCycle)
        return fireCycle > rhs.fireCycle;
    return insertionOrder > rhs.insertionOrder; // older events fire first on tie
}

// ── Public API ────────────────────────────────────────────────────────────────

Scheduler::Scheduler() {
    // Reserve common capacity to avoid repeated heap reallocations.
    eventHeap_.reserve(64);
    subsystems_.reserve(8);
}

void Scheduler::Register(ISchedulable* subsystem, uint32_t cycleRatio) {
    if (subsystem == nullptr)
        throw std::invalid_argument("Scheduler::Register: subsystem must not be null");
    if (cycleRatio == 0)
        throw std::invalid_argument("Scheduler::Register: cycleRatio must be >= 1");

    subsystems_.push_back({ subsystem, cycleRatio, 0 });
}

uint64_t Scheduler::Schedule(uint64_t cyclesFromNow, std::function<void()> callback) {
    if (cyclesFromNow == 0)
        throw std::invalid_argument("Scheduler::Schedule: cyclesFromNow must be > 0");
    if (!callback)
        throw std::invalid_argument("Scheduler::Schedule: callback must not be null");

    uint64_t fireCycle = currentCycle_ + cyclesFromNow;

    eventHeap_.push_back({ fireCycle, insertionCounter_++, std::move(callback) });

    // Maintain min-heap property (smallest fireCycle at front).
    std::push_heap(eventHeap_.begin(), eventHeap_.end(), std::greater<ScheduledEvent>{});

    return fireCycle;
}

void Scheduler::RunUntil(uint64_t targetCycle) {
    if (targetCycle < currentCycle_)
        throw std::invalid_argument("Scheduler::RunUntil: targetCycle < currentCycle");

    while (currentCycle_ < targetCycle) {
        // Determine how far we can advance before the next event fires.
        uint64_t nextEventCycle = targetCycle;
        if (!eventHeap_.empty())
            nextEventCycle = std::min(targetCycle, eventHeap_.front().fireCycle);

        uint64_t delta = nextEventCycle - currentCycle_;
        if (delta > 0) {
            AdvanceSubsystems(delta);
            currentCycle_ = nextEventCycle;
        }

        // Fire all events that are due at the current cycle.
        FirePendingEvents(currentCycle_);
    }
}

void Scheduler::StepOnce() {
    if (subsystems_.empty())
        return;

    // Find the smallest cycle quantum (gcd of all ratios), or just use 1.
    // For simplicity, advance by 1 master cycle.
    RunUntil(currentCycle_ + 1);
}

void Scheduler::Reset() {
    currentCycle_     = 0;
    insertionCounter_ = 0;
    eventHeap_.clear();
    for (auto& s : subsystems_)
        s.accumulator = 0;
}

// ── Private helpers ───────────────────────────────────────────────────────────

void Scheduler::FirePendingEvents(uint64_t upToCycle) {
    while (!eventHeap_.empty() && eventHeap_.front().fireCycle <= upToCycle) {
        std::pop_heap(eventHeap_.begin(), eventHeap_.end(), std::greater<ScheduledEvent>{});
        ScheduledEvent ev = std::move(eventHeap_.back());
        eventHeap_.pop_back();
        ev.callback(); // Fire — the callback may Schedule() new events, which is safe.
    }
}

void Scheduler::AdvanceSubsystems(uint64_t deltaCycles) {
    // Bresenham accumulator: credit each subsystem deltaCycles * (1 / ratio)
    // ticks without floating-point arithmetic.
    for (auto& s : subsystems_) {
        s.accumulator += deltaCycles;
        uint64_t ticks = s.accumulator / s.cycleRatio;
        s.accumulator %= s.cycleRatio;

        if (ticks > 0) {
            [[maybe_unused]] uint64_t consumed = s.subsystem->Tick(ticks);
        }
    }
}

} // namespace AIO::Emulator::Common
