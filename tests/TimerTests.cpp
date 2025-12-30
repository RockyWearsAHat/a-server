#include <gtest/gtest.h>

#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/IORegs.h"

using namespace AIO::Emulator::GBA;

namespace {
uint16_t ReadTimerCounter(GBAMemory &mem, int timerIdx) {
  const uint32_t addr = IORegs::BASE + IORegs::TM0CNT_L +
                        (uint32_t)timerIdx * IORegs::TIMER_CHANNEL_SIZE;
  return mem.Read16(addr);
}

uint32_t TimerCntLAddr(int timerIdx) {
  return IORegs::BASE + IORegs::TM0CNT_L +
         (uint32_t)timerIdx * IORegs::TIMER_CHANNEL_SIZE;
}

uint32_t TimerCntHAddr(int timerIdx) {
  return IORegs::BASE + IORegs::TM0CNT_H +
         (uint32_t)timerIdx * IORegs::TIMER_CHANNEL_SIZE;
}
} // namespace

TEST(TimerTest, Prescaler1_IncrementsEveryCycle) {
  GBAMemory mem;
  mem.Reset();

  // Reload=0, enable, prescaler=1.
  mem.Write16(TimerCntLAddr(0), 0x0000u);
  mem.Write16(TimerCntHAddr(0),
              (uint16_t)(TimerControl::ENABLE | TimerControl::PRESCALER_1));

  EXPECT_EQ(ReadTimerCounter(mem, 0), 0x0000u);

  mem.UpdateTimers(1);
  EXPECT_EQ(ReadTimerCounter(mem, 0), 0x0001u);

  mem.UpdateTimers(3);
  EXPECT_EQ(ReadTimerCounter(mem, 0), 0x0004u);
}

TEST(TimerTest, Prescaler64_IncrementsEvery64Cycles) {
  GBAMemory mem;
  mem.Reset();

  mem.Write16(TimerCntLAddr(0), 0x0000u);
  mem.Write16(TimerCntHAddr(0),
              (uint16_t)(TimerControl::ENABLE | TimerControl::PRESCALER_64));

  mem.UpdateTimers(63);
  EXPECT_EQ(ReadTimerCounter(mem, 0), 0x0000u);

  mem.UpdateTimers(1);
  EXPECT_EQ(ReadTimerCounter(mem, 0), 0x0001u);
}

TEST(TimerTest, OverflowReloadsAndSetsIFWhenIRQEnabled) {
  GBAMemory mem;
  mem.Reset();

  // Reload near overflow so we can trigger quickly.
  mem.Write16(TimerCntLAddr(0), 0xFFFEu);
  mem.Write16(TimerCntHAddr(0),
              (uint16_t)(TimerControl::ENABLE | TimerControl::IRQ_ENABLE |
                         TimerControl::PRESCALER_1));

  // 2 cycles => one overflow => counter reloads to 0xFFFE.
  mem.UpdateTimers(2);
  EXPECT_EQ(ReadTimerCounter(mem, 0), 0xFFFEu);

  const uint16_t ifReg = mem.Read16(IORegs::REG_IF);
  EXPECT_NE((ifReg & InterruptFlags::TIMER0), 0u);
}

TEST(TimerTest, CascadeTimerIncrementsOnPreviousOverflow) {
  GBAMemory mem;
  mem.Reset();

  // Timer0 overflows every 2 cycles.
  mem.Write16(TimerCntLAddr(0), 0xFFFEu);
  mem.Write16(TimerCntHAddr(0),
              (uint16_t)(TimerControl::ENABLE | TimerControl::PRESCALER_1));

  // Timer1 count-up (cascade), enabled, reload=0.
  mem.Write16(TimerCntLAddr(1), 0x0000u);
  mem.Write16(TimerCntHAddr(1),
              (uint16_t)(TimerControl::ENABLE | TimerControl::COUNT_UP));

  mem.UpdateTimers(1);
  EXPECT_EQ(ReadTimerCounter(mem, 0), 0xFFFFu);
  EXPECT_EQ(ReadTimerCounter(mem, 1), 0x0000u);

  mem.UpdateTimers(1); // overflow Timer0 once.
  EXPECT_EQ(ReadTimerCounter(mem, 0), 0xFFFEu);
  EXPECT_EQ(ReadTimerCounter(mem, 1), 0x0001u);

  mem.UpdateTimers(2); // overflow Timer0 once more.
  EXPECT_EQ(ReadTimerCounter(mem, 1), 0x0002u);
}

TEST(TimerTest, CascadeAccountsForMultipleOverflowsInSingleUpdate) {
  GBAMemory mem;
  mem.Reset();

  // Timer0 overflows every 2 cycles.
  mem.Write16(TimerCntLAddr(0), 0xFFFEu);
  mem.Write16(TimerCntHAddr(0),
              (uint16_t)(TimerControl::ENABLE | TimerControl::PRESCALER_1));

  // Timer1 count-up enabled.
  mem.Write16(TimerCntLAddr(1), 0x0000u);
  mem.Write16(TimerCntHAddr(1),
              (uint16_t)(TimerControl::ENABLE | TimerControl::COUNT_UP));

  // 6 cycles => 3 overflows on Timer0 => Timer1 increments by 3.
  mem.UpdateTimers(6);
  EXPECT_EQ(ReadTimerCounter(mem, 0), 0xFFFEu);
  EXPECT_EQ(ReadTimerCounter(mem, 1), 0x0003u);
}

TEST(TimerTest,
     WritingReloadWhileEnabledImmediatelyReloadsCounterAndResetsPrescaler) {
  GBAMemory mem;
  mem.Reset();

  // Start timer0 with prescaler 64 and reload=0.
  mem.Write16(TimerCntLAddr(0), 0x0000u);
  mem.Write16(TimerCntHAddr(0),
              (uint16_t)(TimerControl::ENABLE | TimerControl::PRESCALER_64));

  // Accumulate prescaler partial state.
  mem.UpdateTimers(63);
  EXPECT_EQ(ReadTimerCounter(mem, 0), 0x0000u);

  // Write new reload while enabled. Hardware reloads immediately (our GBAMemory
  // contract).
  mem.Write16(TimerCntLAddr(0), 0xFF00u);
  EXPECT_EQ(ReadTimerCounter(mem, 0), 0xFF00u);

  // Ensure prescaler counter was reset: 63 more cycles should still not
  // increment.
  mem.UpdateTimers(63);
  EXPECT_EQ(ReadTimerCounter(mem, 0), 0xFF00u);

  mem.UpdateTimers(1);
  EXPECT_EQ(ReadTimerCounter(mem, 0), 0xFF01u);
}
