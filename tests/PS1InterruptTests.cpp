#include "emulator/ps1/InterruptController.h"
#include "emulator/ps1/PS1Constants.h"
#include <gtest/gtest.h>

using namespace AIO::Emulator::PS1;

class PS1InterruptTest : public ::testing::Test {
protected:
  void SetUp() override { irq = std::make_unique<InterruptController>(); }
  std::unique_ptr<InterruptController> irq;
};

TEST_F(PS1InterruptTest, InitialState_NoPending) {
  EXPECT_FALSE(irq->HasPendingIRQ());
  EXPECT_EQ(irq->ReadStat(), 0u);
  EXPECT_EQ(irq->ReadMask(), 0u);
}

TEST_F(PS1InterruptTest, RequestIRQ_SetsStatBit) {
  irq->RequestIRQ(IRQ::VBLANK);
  EXPECT_EQ(irq->ReadStat() & IRQ::VBLANK, IRQ::VBLANK);
}

TEST_F(PS1InterruptTest, RequestIRQ_NoPendingIfMasked) {
  irq->RequestIRQ(IRQ::VBLANK);
  // Mask is 0, so no pending IRQ
  EXPECT_FALSE(irq->HasPendingIRQ());
}

TEST_F(PS1InterruptTest, HasPendingIRQ_WhenStatAndMaskOverlap) {
  irq->WriteMask(IRQ::VBLANK);
  irq->RequestIRQ(IRQ::VBLANK);
  EXPECT_TRUE(irq->HasPendingIRQ());
}

TEST_F(PS1InterruptTest, WriteStat_AcknowledgesClearsBit) {
  irq->RequestIRQ(IRQ::VBLANK);
  // Writing 0 to a stat bit clears it (acknowledge semantics)
  irq->WriteStat(~IRQ::VBLANK);
  EXPECT_EQ(irq->ReadStat() & IRQ::VBLANK, 0u);
}

TEST_F(PS1InterruptTest, MultipleIRQSources) {
  irq->RequestIRQ(IRQ::VBLANK);
  irq->RequestIRQ(IRQ::TIMER0);
  EXPECT_EQ(irq->ReadStat() & IRQ::VBLANK, IRQ::VBLANK);
  EXPECT_EQ(irq->ReadStat() & IRQ::TIMER0, IRQ::TIMER0);
}

TEST_F(PS1InterruptTest, WriteMask_OnlyAllows11Bits) {
  irq->WriteMask(0xFFFFFFFF);
  EXPECT_EQ(irq->ReadMask(), 0x7FFu);
}

TEST_F(PS1InterruptTest, Reset_ClearsEverything) {
  irq->RequestIRQ(IRQ::VBLANK);
  irq->WriteMask(0x7FF);
  irq->Reset();
  EXPECT_EQ(irq->ReadStat(), 0u);
  EXPECT_EQ(irq->ReadMask(), 0u);
  EXPECT_FALSE(irq->HasPendingIRQ());
}

TEST_F(PS1InterruptTest, GetDebugSummary_NotEmpty) {
  EXPECT_FALSE(irq->GetDebugSummary().empty());
}

TEST_F(PS1InterruptTest, PendingIRQ_RequiresBothStatAndMask) {
  // Set mask for TIMER0
  irq->WriteMask(IRQ::TIMER0);
  // Request VBLANK (not in mask)
  irq->RequestIRQ(IRQ::VBLANK);
  EXPECT_FALSE(irq->HasPendingIRQ());

  // Now request TIMER0 (in mask)
  irq->RequestIRQ(IRQ::TIMER0);
  EXPECT_TRUE(irq->HasPendingIRQ());
}
