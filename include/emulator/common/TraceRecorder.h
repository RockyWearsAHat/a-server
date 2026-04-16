#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace AIO::Emulator::Common {

/// Entry captured during recording.
struct TraceEntry {
    uint64_t masterCycle; ///< Absolute master-clock cycle when this was captured.
    uint32_t pc;          ///< Program counter at the start of the instruction.
    uint32_t instruction; ///< Raw encoding of the instruction (32-bit or padded).
    uint32_t result;      ///< Optional: first destination register value after execution.
};

/// ── TraceRecorder ─────────────────────────────────────────────────────────────
///
/// Captures a rolling window of executed instructions for determinism testing,
/// debugging, and regression replay.
///
/// The recorder maintains a fixed-size ring buffer to bound memory usage.
/// Once full, it overwrites the oldest entry. The buffer size is configurable
/// at construction time; the default (65536) is large enough to cover several
/// frames on most 8/32-bit consoles.
///
/// Replay model:
///   1. Record sequence A during a known-good run.
///   2. Record sequence B during a suspect run.
///   3. Call CompareWith(B) to get the first divergence point.
///
/// Thread safety: none — all calls must originate from the emulation thread.
class TraceRecorder {
public:
    static constexpr size_t kDefaultCapacity = 65536;

    /// @param capacity  Ring buffer size (number of entries). Must be > 0.
    explicit TraceRecorder(size_t capacity = kDefaultCapacity);
    ~TraceRecorder() = default;

    TraceRecorder(const TraceRecorder&)            = delete;
    TraceRecorder& operator=(const TraceRecorder&) = delete;

    // ── Control ───────────────────────────────────────────────────────────

    void StartRecording() noexcept;
    void StopRecording()  noexcept;
    void Clear()          noexcept;

    [[nodiscard]] bool IsRecording() const noexcept { return recording_; }

    // ── Recording ─────────────────────────────────────────────────────────

    /// Capture one instruction. No-op when not recording.
    ///
    /// @param cycle       Master-clock cycle at instruction start.
    /// @param pc          Program counter.
    /// @param instruction Raw instruction word.
    /// @param result      Destination register value after execution (optional; 0 if unused).
    void Capture(uint64_t cycle, uint32_t pc, uint32_t instruction, uint32_t result = 0) noexcept;

    // ── Inspection ────────────────────────────────────────────────────────

    /// Number of entries currently stored (0 … capacity).
    [[nodiscard]] size_t Count() const noexcept;

    /// Read the entry at logical index @p i (0 = oldest, Count()-1 = newest).
    /// @throws std::out_of_range if i >= Count().
    [[nodiscard]] const TraceEntry& At(size_t i) const;

    /// Iterate all entries from oldest to newest.
    void ForEach(const std::function<void(const TraceEntry&)>& visitor) const;

    // ── Comparison ────────────────────────────────────────────────────────

    struct DivergenceReport {
        bool     diverged       = false;
        size_t   firstDiffIndex = 0;   ///< Logical index in the shorter trace.
        TraceEntry lhs;
        TraceEntry rhs;
    };

    /// Compare this recorder's trace with @p other, starting from the oldest
    /// entry in each. Returns information about the first point of divergence.
    ///
    /// Two entries are equal when all four fields match exactly.
    [[nodiscard]] DivergenceReport CompareWith(const TraceRecorder& other) const;

private:
    std::vector<TraceEntry> ring_;
    size_t capacity_;
    size_t head_      = 0; ///< Next write position (wraps around capacity_).
    size_t count_     = 0; ///< Number of valid entries (≤ capacity_).
    bool   recording_ = false;

    // Convert logical index to physical ring index.
    size_t PhysicalIndex(size_t logicalIndex) const noexcept;
};

/// ── ClockDivider ──────────────────────────────────────────────────────────────
///
/// Converts a master-clock tick count into subsystem ticks with
/// no accumulated rounding error (uses a Bresenham-style accumulator).
///
/// Example: a master clock at 21.477272 MHz driving a CPU at 1/12 rate.
///   ClockDivider div(12);            // 1 CPU tick per 12 master ticks
///   uint64_t cpuTicks = div.Tick(masterTicks);
class ClockDivider {
public:
    /// @param ratio  Master ticks per subsystem tick. Must be >= 1.
    explicit ClockDivider(uint32_t ratio);

    /// Feed @p masterTicks into the divider.
    /// @return Number of whole subsystem ticks that elapsed.
    uint64_t Tick(uint64_t masterTicks) noexcept;

    void Reset() noexcept;

    [[nodiscard]] uint32_t Ratio() const noexcept { return ratio_; }

private:
    uint32_t ratio_;
    uint64_t accumulator_ = 0; ///< Fractional master ticks carried forward.
};

} // namespace AIO::Emulator::Common
