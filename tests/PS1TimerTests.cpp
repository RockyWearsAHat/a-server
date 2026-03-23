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

// ─── Sync Modes (NOCASH PSX §Timers) ─────────────────────────────────────

TEST_F(PS1TimerTest, Timer2_SyncMode0_StopsCounter) {
  // Sync mode 0+3 for T2 = stop counter permanently.
  // mode = syncEnable(bit0)=1, syncMode(bits1-2)=0 → 0x0001
  timers->Write32(0x1F801124, 0x0001); // T2 mode: sync enabled, mode 0
  timers->Tick(1000);
  EXPECT_EQ(timers->Read32(0x1F801120), 0u)
      << "T2 sync mode 0 must stop counter";
}

TEST_F(PS1TimerTest, Timer2_SyncMode3_StopsCounter) {
  // mode = syncEnable=1, syncMode=3 → bits: 1 | (3<<1) = 0x0007
  timers->Write32(0x1F801124, 0x0007); // T2 mode: sync enabled, mode 3
  timers->Tick(1000);
  EXPECT_EQ(timers->Read32(0x1F801120), 0u)
      << "T2 sync mode 3 must stop counter";
}

TEST_F(PS1TimerTest, Timer0_SyncMode0_PausesDuringHBlank) {
  // mode = syncEnable=1, syncMode=0 → 0x0001
  timers->Write32(0x1F801104, 0x0001); // T0 mode: sync enabled, mode 0
  // Simulate HBlank active
  timers->TickHBlank();
  // Tick within the HBlank window; T0 should NOT advance
  timers->Tick(100);
  EXPECT_EQ(timers->Read32(0x1F801100), 0u)
      << "T0 sync mode 0 must pause during HBlank";
}

TEST_F(PS1TimerTest, Timer0_SyncMode0_RunsOutsideHBlank) {
  timers->Write32(0x1F801104, 0x0001); // T0 sync mode 0
  // No TickHBlank — hblankActive = false, so counter must advance
  timers->Tick(100);
  EXPECT_GT(timers->Read32(0x1F801100), 0u)
      << "T0 sync mode 0 must count outside HBlank";
}

TEST_F(PS1TimerTest, Timer1_SyncMode1_ResetsAtVBlank) {
  // mode = syncEnable=1, syncMode=1 → bits: 1 | (1<<1) = 0x0003
  timers->Write32(0x1F801114, 0x0003); // T1 mode: sync enabled, mode 1
  // Advance T1
  timers->Tick(200);
  EXPECT_GT(timers->Read32(0x1F801110), 0u); // sanity: counter advanced
  // Trigger VBlank — mode 1 must reset counter to 0
  timers->SetVBlankState(true);
  EXPECT_EQ(timers->Read32(0x1F801110), 0u)
      << "T1 sync mode 1 must reset counter at VBlank start";
}

TEST_F(PS1TimerTest, Timer1_SyncMode3_BlockedUntilFirstVBlank) {
  // mode = syncEnable=1, syncMode=3 → 1 | (3<<1) = 0x0007
  timers->Write32(0x1F801114, 0x0007);
  // Before first VBlank T1 must not count
  timers->Tick(500);
  EXPECT_EQ(timers->Read32(0x1F801110), 0u)
      << "T1 sync mode 3 must be stopped before first VBlank";
  // After first VBlank T1 must free-run
  timers->SetVBlankState(true);
  timers->SetVBlankState(false);
  timers->Tick(100);
  EXPECT_GT(timers->Read32(0x1F801110), 0u)
      << "T1 sync mode 3 must free-run after first VBlank";
}
