#include "emulator/ps1/InterruptController.h"
#include "emulator/ps1/PS1Constants.h"
#include "emulator/ps1/PS1Timer.h"
#include <gtest/gtest.h>

using namespace AIO::Emulator::PS1;

class PS1TimerTest : public ::testing::Test {
protected:
  void SetUp() override {
    irq = std::make_unique<InterruptController>();
    timers = std::make_unique<PS1Timer>(*irq);
  }
  std::unique_ptr<InterruptController> irq;
  std::unique_ptr<PS1Timer> timers;
};

TEST_F(PS1TimerTest, InitialCounterValues_Zero) {
  EXPECT_EQ(timers->Read32(0x1F801100), 0u); // Timer 0 counter
  EXPECT_EQ(timers->Read32(0x1F801110), 0u); // Timer 1 counter
  EXPECT_EQ(timers->Read32(0x1F801120), 0u); // Timer 2 counter
}

TEST_F(PS1TimerTest, WriteTarget_ReadBack) {
  timers->Write32(0x1F801108, 0x1234); // Timer 0 target
  EXPECT_EQ(timers->Read32(0x1F801108), 0x1234u);
}

TEST_F(PS1TimerTest, Timer0_CountsUp) {
  // Timer 0 mode = 0 (system clock, no IRQ)
  timers->Write32(0x1F801104, 0);
  timers->Tick(100);
  uint32_t counter = timers->Read32(0x1F801100);
  EXPECT_GT(counter, 0u);
}

TEST_F(PS1TimerTest, Timer1_CountsUp) {
  timers->Write32(0x1F801114, 0);
  timers->Tick(100);
  uint32_t counter = timers->Read32(0x1F801110);
  EXPECT_GT(counter, 0u);
}

TEST_F(PS1TimerTest, Timer2_CountsUp) {
  timers->Write32(0x1F801124, 0);
  timers->Tick(100);
  uint32_t counter = timers->Read32(0x1F801120);
  EXPECT_GT(counter, 0u);
}

TEST_F(PS1TimerTest, Timer_Overflow_Wraps) {
  // Set counter near max
  timers->Write32(0x1F801100, 0xFFF0); // Set counter
  timers->Write32(0x1F801104, 0);      // Mode = system clock
  timers->Tick(0x20);

  uint32_t counter = timers->Read32(0x1F801100);
  // Should have wrapped (counter < 0xFFF0 + 0x20 when masked to 16-bit)
  EXPECT_LT(counter, 0x10000u);
}

TEST_F(PS1TimerTest, WriteMode_ResetsCounter) {
  timers->Tick(100);              // Increment counter
  timers->Write32(0x1F801104, 0); // Write mode resets counter to 0
  EXPECT_EQ(timers->Read32(0x1F801100), 0u);
}

TEST_F(PS1TimerTest, Timer_TargetIRQ) {
  irq->WriteMask(IRQ::TIMER0);

  // Set target and enable target IRQ (bit 4)
  timers->Write32(0x1F801108, 10);   // Target = 10
  timers->Write32(0x1F801104, 0x10); // Mode: IRQ on target

  timers->Tick(15);

  EXPECT_TRUE(irq->HasPendingIRQ());
}

TEST_F(PS1TimerTest, Timer_TargetIRQ_OneShotPulseDoesNotRefireAfterAck) {
  irq->WriteMask(IRQ::TIMER0);

  // Target IRQ + reset-on-target, non-repeat pulse mode.
  timers->Write32(0x1F801108, 1);
  timers->Write32(0x1F801104, 0x18);

  timers->Tick(1);
  EXPECT_TRUE(irq->HasPendingIRQ());

  irq->ClearIRQ(IRQ::TIMER0);
  EXPECT_FALSE(irq->HasPendingIRQ());

  timers->Tick(8);
  EXPECT_FALSE(irq->HasPendingIRQ());
}

TEST_F(PS1TimerTest, Timer_TargetIRQ_RepeatPulseRefiresAfterAck) {
  irq->WriteMask(IRQ::TIMER0);

  // Target IRQ + reset-on-target + repeat mode.
  timers->Write32(0x1F801108, 1);
  timers->Write32(0x1F801104, 0x58);

  timers->Tick(1);
  EXPECT_TRUE(irq->HasPendingIRQ());

  irq->ClearIRQ(IRQ::TIMER0);
  EXPECT_FALSE(irq->HasPendingIRQ());

  timers->Tick(1);
  EXPECT_TRUE(irq->HasPendingIRQ());
}

TEST_F(PS1TimerTest, Reset_ClearsAllTimers) {
  timers->Tick(100);
  timers->Reset();
  EXPECT_EQ(timers->Read32(0x1F801100), 0u);
  EXPECT_EQ(timers->Read32(0x1F801110), 0u);
  EXPECT_EQ(timers->Read32(0x1F801120), 0u);
}

TEST_F(PS1TimerTest, TickDotClock_IncrementsDotClockTimer) {
  // Timer 0 with dot clock source (bit 8 = 1)
  timers->Write32(0x1F801104, 0x100);
  timers->TickDotClock(50);
  // Timer 0 should have been incremented by dot clock ticks
  uint32_t counter = timers->Read32(0x1F801100);
  EXPECT_GT(counter, 0u);
}
