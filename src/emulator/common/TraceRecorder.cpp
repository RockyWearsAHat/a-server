// TraceRecorder.cpp — rolling ring-buffer trace recorder and ClockDivider.

#include "emulator/common/TraceRecorder.h"

#include <stdexcept>

namespace AIO::Emulator::Common {

// ── TraceRecorder ─────────────────────────────────────────────────────────────

TraceRecorder::TraceRecorder(size_t capacity)
    : capacity_(capacity) {
    if (capacity == 0)
        throw std::invalid_argument("TraceRecorder: capacity must be > 0");
    ring_.resize(capacity);
}

void TraceRecorder::StartRecording() noexcept { recording_ = true; }
void TraceRecorder::StopRecording()  noexcept { recording_ = false; }

void TraceRecorder::Clear() noexcept {
    head_  = 0;
    count_ = 0;
}

void TraceRecorder::Capture(uint64_t cycle, uint32_t pc,
                            uint32_t instruction, uint32_t result) noexcept {
    if (!recording_)
        return;

    ring_[head_] = TraceEntry{ cycle, pc, instruction, result };
    head_ = (head_ + 1) % capacity_;
    if (count_ < capacity_)
        ++count_;
    // When count_ == capacity_ the ring is full; head_ now points to the
    // oldest entry and will overwrite it on the next capture. That is the
    // correct ring-buffer semantics.
}

size_t TraceRecorder::Count() const noexcept { return count_; }

size_t TraceRecorder::PhysicalIndex(size_t logicalIndex) const noexcept {
    // Oldest entry lives at:
    //   (head_ - count_ + capacity_) % capacity_   when the buffer is full
    //   logicalIndex                                when not yet full(head_ == count_)
    size_t oldest = (head_ + capacity_ - count_) % capacity_;
    return (oldest + logicalIndex) % capacity_;
}

const TraceEntry& TraceRecorder::At(size_t i) const {
    if (i >= count_)
        throw std::out_of_range("TraceRecorder::At: index out of range");
    return ring_[PhysicalIndex(i)];
}

void TraceRecorder::ForEach(const std::function<void(const TraceEntry&)>& visitor) const {
    for (size_t i = 0; i < count_; ++i)
        visitor(ring_[PhysicalIndex(i)]);
}

TraceRecorder::DivergenceReport TraceRecorder::CompareWith(const TraceRecorder& other) const {
    size_t compareCount = std::min(count_, other.count_);
    for (size_t i = 0; i < compareCount; ++i) {
        const TraceEntry& a = At(i);
        const TraceEntry& b = other.At(i);
        if (a.masterCycle  != b.masterCycle  ||
            a.pc           != b.pc           ||
            a.instruction  != b.instruction  ||
            a.result       != b.result)
        {
            return { true, i, a, b };
        }
    }
    // Equal up to the shorter trace — no divergence detected.
    return {};
}

// ── ClockDivider ──────────────────────────────────────────────────────────────

ClockDivider::ClockDivider(uint32_t ratio)
    : ratio_(ratio) {
    if (ratio == 0)
        throw std::invalid_argument("ClockDivider: ratio must be >= 1");
}

uint64_t ClockDivider::Tick(uint64_t masterTicks) noexcept {
    accumulator_ += masterTicks;
    uint64_t ticks = accumulator_ / ratio_;
    accumulator_  %= ratio_;
    return ticks;
}

void ClockDivider::Reset() noexcept {
    accumulator_ = 0;
}

} // namespace AIO::Emulator::Common
