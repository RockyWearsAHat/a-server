// SchedulerTests.cpp — unit tests for the deterministic Scheduler,
// ClockDivider, and BusMap infrastructure.

#include "emulator/common/BusMap.h"
#include "emulator/common/Scheduler.h"
#include "emulator/common/TraceRecorder.h"
#include <gtest/gtest.h>
#include <string_view>
#include <vector>

using namespace AIO::Emulator::Common;

// ─────────────────────────────────────────────────────────────────────────────
// Scheduler Tests
// ─────────────────────────────────────────────────────────────────────────────

class MockSubsystem : public ISchedulable {
public:
    explicit MockSubsystem(const char* name) : name_(name) {}

    uint64_t Tick(uint64_t cycles) override {
        totalTicks += cycles;
        ++callCount;
        return cycles;
    }
    const char* SubsystemName() const override { return name_; }

    uint64_t totalTicks = 0;
    int      callCount  = 0;

private:
    const char* name_;
};

// ── Scheduler_Register ────────────────────────────────────────────────────────

TEST(Scheduler_Register, NullSubsystemThrows) {
    Scheduler sch;
    EXPECT_THROW(sch.Register(nullptr, 1), std::invalid_argument);
}

TEST(Scheduler_Register, ZeroRatioThrows) {
    Scheduler sch;
    MockSubsystem cpu("CPU");
    EXPECT_THROW(sch.Register(&cpu, 0), std::invalid_argument);
}

// ── Scheduler_RunUntil ────────────────────────────────────────────────────────

TEST(Scheduler_RunUntil, SingleSubsystem1to1Ratio) {
    Scheduler     sch;
    MockSubsystem cpu("CPU");

    sch.Register(&cpu, 1);
    sch.RunUntil(100);

    EXPECT_EQ(cpu.totalTicks, 100u);
    EXPECT_EQ(sch.CurrentCycle(), 100u);
}

TEST(Scheduler_RunUntil, DividedRatioFloorSemantics) {
    // PPU ticks at 1/3 the rate of the master clock (like NES NTSC).
    Scheduler     sch;
    MockSubsystem ppu("PPU");

    sch.Register(&ppu, 3);
    sch.RunUntil(9);

    EXPECT_EQ(ppu.totalTicks, 3u);
}

TEST(Scheduler_RunUntil, TargetLessThanCurrentThrows) {
    Scheduler     sch;
    MockSubsystem cpu("CPU");
    sch.Register(&cpu, 1);
    sch.RunUntil(50);

    EXPECT_THROW(sch.RunUntil(10), std::invalid_argument);
}

// ── Scheduler_Schedule ────────────────────────────────────────────────────────

TEST(Scheduler_Schedule, EventFiresAtCorrectCycle) {
    Scheduler sch;

    bool fired = false;
    sch.Schedule(10, [&] { fired = true; });

    sch.RunUntil(9);
    EXPECT_FALSE(fired);

    sch.RunUntil(10);
    EXPECT_TRUE(fired);
}

TEST(Scheduler_Schedule, MultipleEventsFireInOrder) {
    Scheduler sch;

    std::vector<int> order;
    sch.Schedule(5,  [&] { order.push_back(1); });
    sch.Schedule(10, [&] { order.push_back(2); });
    sch.Schedule(15, [&] { order.push_back(3); });

    sch.RunUntil(15);

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(Scheduler_Schedule, ZeroCyclesFromNowThrows) {
    Scheduler sch;
    EXPECT_THROW(sch.Schedule(0, [] {}), std::invalid_argument);
}

TEST(Scheduler_Schedule, NullCallbackThrows) {
    Scheduler sch;
    EXPECT_THROW(sch.Schedule(10, nullptr), std::invalid_argument);
}

TEST(Scheduler_Schedule, EventCanScheduleNestedEvent) {
    Scheduler sch;

    bool innerFired = false;
    sch.Schedule(5, [&] {
        sch.Schedule(5, [&] { innerFired = true; });
    });

    sch.RunUntil(10);
    EXPECT_TRUE(innerFired);
}

TEST(Scheduler_Reset, ClearsPendingEventsAndCycles) {
    Scheduler     sch;
    MockSubsystem cpu("CPU");
    sch.Register(&cpu, 1);
    sch.RunUntil(100);

    sch.Reset();
    EXPECT_EQ(sch.CurrentCycle(), 0u);
    EXPECT_EQ(cpu.totalTicks, 100u); // ticks before reset are not undone

    sch.RunUntil(50); // should work from zero
    EXPECT_EQ(sch.CurrentCycle(), 50u);
}

// ── Scheduler_Determinism ────────────────────────────────────────────────────

TEST(Scheduler_Determinism, SameInputProducesSameOutput) {
    auto RunScenario = [](uint64_t endCycle) {
        Scheduler     sch;
        MockSubsystem cpu("CPU");
        MockSubsystem ppu("PPU");
        sch.Register(&cpu, 1);
        sch.Register(&ppu, 3);

        std::vector<uint64_t> eventsFired;
        for (uint64_t t = 100; t <= endCycle; t += 100)
            sch.Schedule(t, [t, &eventsFired] { eventsFired.push_back(t); });

        sch.RunUntil(endCycle);
        return eventsFired;
    };

    auto a = RunScenario(1000);
    auto b = RunScenario(1000);

    EXPECT_EQ(a, b);
}

// ─────────────────────────────────────────────────────────────────────────────
// BusMap Tests
// ─────────────────────────────────────────────────────────────────────────────

/// Minimal device backed by a flat array.
class FlatRam final : public IBusDevice {
public:
    explicit FlatRam(std::string_view name, uint32_t size)
        : name_(name), data_(size, 0) {}

    uint8_t Read8(uint32_t address) override {
        return data_[address & (data_.size() - 1)];
    }
    void Write8(uint32_t address, uint8_t value) override {
        data_[address & (data_.size() - 1)] = value;
    }
    std::string_view DeviceName() const override { return name_; }

private:
    std::string      name_;
    std::vector<uint8_t> data_;
};

TEST(BusMap_AddRegion, NullDeviceThrows) {
    BusMap<uint16_t> bus;
    EXPECT_THROW(bus.AddRegion(0x0000, 0x1000, nullptr), std::invalid_argument);
}

TEST(BusMap_AddRegion, ZeroSizeThrows) {
    BusMap<uint16_t> bus;
    FlatRam ram("RAM", 0x1000);
    EXPECT_THROW(bus.AddRegion(0x0000, 0, &ram), std::invalid_argument);
}

TEST(BusMap_AddRegion, OverlapThrows) {
    BusMap<uint16_t> bus;
    FlatRam ram1("RAM1", 0x1000);
    FlatRam ram2("RAM2", 0x1000);

    bus.AddRegion(0x0000, 0x1000, &ram1);
    // Overlapping region [0x0800, 0x1800)
    EXPECT_THROW(bus.AddRegion(0x0800, 0x1000, &ram2), std::logic_error);
}

TEST(BusMap_ReadWrite, RoundTrip8Bit) {
    BusMap<uint16_t> bus;
    FlatRam ram("WRAM", 0x800);

    bus.AddRegion(0x0000, 0x0800, &ram);
    bus.Write8(0x0042, 0xAB);
    EXPECT_EQ(bus.Read8(0x0042), 0xABu);
}

TEST(BusMap_ReadWrite, UnmappedReadReturnsOpenBus) {
    BusMap<uint16_t> bus;
    // No devices registered.
    EXPECT_EQ(bus.Read8(0x1234), 0xFFu);
}

TEST(BusMap_ReadWrite, UnmappedWriteIsNoOp) {
    BusMap<uint16_t> bus;
    EXPECT_NO_THROW(bus.Write8(0x1234, 0x55));
}

TEST(BusMap_ReadWrite, LittleEndian16BitAssembly) {
    BusMap<uint16_t, true> bus;
    FlatRam                ram("WRAM", 0x100);

    bus.AddRegion(0x0000, 0x0100, &ram);
    bus.Write16(0x0010, 0xBEEF);

    EXPECT_EQ(bus.Read8(0x0010), 0xEFu); // low byte
    EXPECT_EQ(bus.Read8(0x0011), 0xBEu); // high byte
    EXPECT_EQ(bus.Read16(0x0010), 0xBEEFu);
}

TEST(BusMap_ReadWrite, BigEndian16BitAssembly) {
    BusMap<uint16_t, false> bus;
    FlatRam                 ram("WRAM", 0x100);

    bus.AddRegion(0x0000, 0x0100, &ram);
    bus.Write16(0x0010, 0xBEEF);

    EXPECT_EQ(bus.Read8(0x0010), 0xBEu); // high byte first on big-endian bus
    EXPECT_EQ(bus.Read8(0x0011), 0xEFu);
    EXPECT_EQ(bus.Read16(0x0010), 0xBEEFu);
}

TEST(BusMap_ReadWrite, MultipleRegionsRouteCorrectly) {
    BusMap<uint16_t> bus;
    FlatRam          rom("ROM",  0x4000);
    FlatRam          wram("WRAM", 0x2000);

    bus.AddRegion(0x0000, 0x4000, &rom);
    bus.AddRegion(0x4000, 0x2000, &wram);

    bus.Write8(0x0100, 0xAA); // into ROM region
    bus.Write8(0x4100, 0xBB); // into WRAM region

    EXPECT_EQ(bus.Read8(0x0100), 0xAAu);
    EXPECT_EQ(bus.Read8(0x4100), 0xBBu);
    EXPECT_STREQ(std::string(bus.DeviceAt(0x0100)).c_str(), "ROM");
    EXPECT_STREQ(std::string(bus.DeviceAt(0x4100)).c_str(), "WRAM");
}

// ─────────────────────────────────────────────────────────────────────────────
// ClockDivider Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(ClockDivider_Constructor, ZeroRatioThrows) {
    EXPECT_THROW(ClockDivider(0), std::invalid_argument);
}

TEST(ClockDivider_Tick, ExactMultiple) {
    ClockDivider div(3);
    EXPECT_EQ(div.Tick(9), 3u);
    EXPECT_EQ(div.Tick(3), 1u);
}

TEST(ClockDivider_Tick, AccumulatesRemainder) {
    ClockDivider div(3);
    EXPECT_EQ(div.Tick(1), 0u);
    EXPECT_EQ(div.Tick(1), 0u);
    EXPECT_EQ(div.Tick(1), 1u); // 3 master ticks accumulated → 1 subsystem tick
}

TEST(ClockDivider_Tick, NoAccumulatedError) {
    // 1000 master ticks ÷ 3 should give exactly 333, with 1 leftover.
    ClockDivider div(3);
    uint64_t total = 0;
    for (int i = 0; i < 1000; ++i)
        total += div.Tick(1);
    EXPECT_EQ(total, 333u);
}

TEST(ClockDivider_Reset, ClearsAccumulator) {
    ClockDivider div(3);
    div.Tick(2); // 2 remainder
    div.Reset();
    EXPECT_EQ(div.Tick(1), 0u); // accumulator cleared; 1 < 3
}

// ─────────────────────────────────────────────────────────────────────────────
// TraceRecorder Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(TraceRecorder_Constructor, ZeroCapacityThrows) {
    EXPECT_THROW(TraceRecorder(0), std::invalid_argument);
}

TEST(TraceRecorder_Capture, NoCapturWhenNotRecording) {
    TraceRecorder rec;
    rec.Capture(1, 0x100, 0xEA, 0);
    EXPECT_EQ(rec.Count(), 0u);
}

TEST(TraceRecorder_Capture, CapturesWhenRecording) {
    TraceRecorder rec;
    rec.StartRecording();
    rec.Capture(10, 0x200, 0xABCD, 42);
    EXPECT_EQ(rec.Count(), 1u);

    const TraceEntry& e = rec.At(0);
    EXPECT_EQ(e.masterCycle,  10u);
    EXPECT_EQ(e.pc,           0x200u);
    EXPECT_EQ(e.instruction,  0xABCDu);
    EXPECT_EQ(e.result,       42u);
}

TEST(TraceRecorder_Capture, RingOverwritesOldEntries) {
    TraceRecorder rec(4);
    rec.StartRecording();

    for (uint32_t i = 0; i < 6; ++i)
        rec.Capture(i, i * 4, i, 0);

    // Buffer holds the 4 newest entries (i = 2..5).
    EXPECT_EQ(rec.Count(), 4u);
    EXPECT_EQ(rec.At(0).pc, 8u);  // i=2
    EXPECT_EQ(rec.At(3).pc, 20u); // i=5
}

TEST(TraceRecorder_At, OutOfRangeThrows) {
    TraceRecorder rec;
    rec.StartRecording();
    rec.Capture(0, 0, 0);
    EXPECT_THROW([[maybe_unused]] auto _ = rec.At(1), std::out_of_range);
}

TEST(TraceRecorder_Compare, IdenticalTracesNoDivergence) {
    TraceRecorder a, b;
    a.StartRecording();
    b.StartRecording();
    for (int i = 0; i < 10; ++i) {
        a.Capture(i, i * 4, 0xEA, 0);
        b.Capture(i, i * 4, 0xEA, 0);
    }

    auto report = a.CompareWith(b);
    EXPECT_FALSE(report.diverged);
}

TEST(TraceRecorder_Compare, DetectsDivergenceAtCorrectIndex) {
    TraceRecorder a, b;
    a.StartRecording();
    b.StartRecording();

    for (int i = 0; i < 5; ++i) {
        a.Capture(i, i * 4, 0xEA, 0);
        b.Capture(i, i * 4, 0xEA, 0);
    }
    // Diverge at index 5.
    a.Capture(5, 20, 0xEA,   0);
    b.Capture(5, 20, 0xBEEF, 0); // different instruction

    auto report = a.CompareWith(b);
    EXPECT_TRUE(report.diverged);
    EXPECT_EQ(report.firstDiffIndex, 5u);
    EXPECT_EQ(report.lhs.instruction, 0xEAu);
    EXPECT_EQ(report.rhs.instruction, 0xBEEFu);
}

TEST(TraceRecorder_Clear, ResetsCount) {
    TraceRecorder rec;
    rec.StartRecording();
    rec.Capture(0, 0, 0);
    rec.Clear();
    EXPECT_EQ(rec.Count(), 0u);
}
