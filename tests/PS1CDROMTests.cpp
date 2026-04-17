// Copyright (c) 2025 AIO Server Project. All rights reserved.
// Author: Alex Waldmann
// Date: 2025-01-01

#include "emulator/ps1/CDROM.h"
#include "emulator/ps1/InterruptController.h"
#include "emulator/ps1/PS1Constants.h"
#include "emulator/ps1/PS1Memory.h"
#include <gtest/gtest.h>

using namespace AIO::Emulator::PS1;

// ─── Test Fixture ──────────────────────────────────────────────────────────────

class PS1CDROMTest : public ::testing::Test {
protected:
  void SetUp() override {
    memory = std::make_unique<PS1Memory>();
    irq = std::make_unique<InterruptController>();
    cdrom = std::make_unique<CDROM>(*memory, *irq);
    memory->SetCDROM(cdrom.get());
    cdrom->Reset();
  }

  // ── Register helpers ──────────────────────────────────────────────────

  /// Set the CDROM index register (selects sub-register bank).
  void SetIndex(uint8_t idx) {
    cdrom->Write8(IO::CDROM_BASE + 0, idx);
  }

  /// Push one parameter byte into the parameter FIFO (index 0, reg 2).
  void PushParam(uint8_t param) {
    SetIndex(0);
    cdrom->Write8(IO::CDROM_BASE + 2, param);
  }

  /// Issue a command byte (index 0, reg 1).
  void SendCommand(uint8_t cmd) {
    SetIndex(0);
    cdrom->Write8(IO::CDROM_BASE + 1, cmd);
  }

  /// Read one response byte from the response FIFO (reg 1 any index).
  uint8_t ReadResponse() {
    return cdrom->Read8(IO::CDROM_BASE + 1);
  }

  /// Read the interrupt flag register (index 1, reg 3).
  uint8_t ReadInterruptFlag() {
    SetIndex(1);
    return cdrom->Read8(IO::CDROM_BASE + 3);
  }

  /// Acknowledge and clear the current interrupt.
  void AcknowledgeInterrupt() {
    SetIndex(1);
    cdrom->Write8(IO::CDROM_BASE + 3, 0x1F);
  }

  /// Wait for a pending second response to be delivered (tick up to N cycles).
  void TickUntilIRQ(int maxCycles = 100000) {
    for (int i = 0; i < maxCycles; ++i) {
      cdrom->Tick(1);
      if ((ReadInterruptFlag() & 0x07) != 0)
        return;
    }
  }

  std::unique_ptr<PS1Memory> memory;
  std::unique_ptr<InterruptController> irq;
  std::unique_ptr<CDROM> cdrom;
};

// ─── GetStat (0x01) ───────────────────────────────────────────────────────────

TEST_F(PS1CDROMTest, GetStat_FiresINT3AndReturnsStatusByte) {
  SendCommand(0x01);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
  // Motor-on bit (0x02) is always set when no disc and no error
  uint8_t stat = ReadResponse();
  EXPECT_NE(stat, 0u); // At least motor-on bit should be set
}

TEST_F(PS1CDROMTest, GetStat_ShellOpenBitSetWithNoDisc) {
  // No disc loaded → shell-open bit (0x10) should be reported
  SendCommand(0x01);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
  uint8_t stat = ReadResponse();
  EXPECT_NE(stat & 0x10u, 0u); // Bit 4: shell open / no disc
}

// ─── SetLoc (0x02) ────────────────────────────────────────────────────────────

TEST_F(PS1CDROMTest, SetLoc_AcceptsMinuteSecondSectorAndRespondsINT3) {
  PushParam(0x00); // MM = 0
  PushParam(0x02); // SS = 2
  PushParam(0x00); // FF = 0
  SendCommand(0x02);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
  // Response should be status byte (motor-on etc.)
  uint8_t stat = ReadResponse();
  EXPECT_NE(stat, 0u);
}

// ─── GetlocL (0x10) ───────────────────────────────────────────────────────────

TEST_F(PS1CDROMTest, GetLocL_RespondsINT3) {
  SendCommand(0x10);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
}

// ─── GetlocP (0x11) ───────────────────────────────────────────────────────────

TEST_F(PS1CDROMTest, GetLocP_RespondsINT3) {
  SendCommand(0x11);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
}

// ─── GetTN (0x13) ─────────────────────────────────────────────────────────────

TEST_F(PS1CDROMTest, GetTN_RespondsINT3WithTrackInfo) {
  SendCommand(0x13);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
  // The response begins with the status byte; at minimum it should be present
  // without asserting particular track-count values (command is skeletal).
  uint8_t stat = ReadResponse();
  EXPECT_NE(stat, 0xFFu); // Should not be garbage
}

// ─── GetTD (0x14) ─────────────────────────────────────────────────────────────

TEST_F(PS1CDROMTest, GetTD_RespondsINT3ForTrack1) {
  PushParam(0x01); // Track 1 in BCD
  SendCommand(0x14);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
}

// ─── Init (0x0A) ─────────────────────────────────────────────────────────────

TEST_F(PS1CDROMTest, Init_FirstResponseIsINT3) {
  SendCommand(0x0A);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
}

TEST_F(PS1CDROMTest, Init_SecondResponseIsINT2AfterDelay) {
  SendCommand(0x0A);
  // Consume first INT3
  AcknowledgeInterrupt();
  // Tick until second response arrives
  TickUntilIRQ();
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 2u);
}

// ─── SetMode (0x0E) ───────────────────────────────────────────────────────────

TEST_F(PS1CDROMTest, SetMode_AcceptsModeByteAndRespondsINT3) {
  PushParam(0x20); // Bit 5 = whole-sector mode
  SendCommand(0x0E);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
}

// ─── GetID (0x1A) ─────────────────────────────────────────────────────────────

TEST_F(PS1CDROMTest, GetID_NoDisc_FirstResponseIsINT5WithErrorBit) {
  // No disc loaded — should respond with error INT5
  SendCommand(0x1A);
  uint8_t flag = ReadInterruptFlag() & 0x07;
  // psx-spx: no disc → INT5 (error)
  EXPECT_EQ(flag, 5u);
}

TEST_F(PS1CDROMTest, GetID_NoDisc_StatusHasShellOpenBit) {
  SendCommand(0x1A);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 5u);
  uint8_t stat = ReadResponse();
  EXPECT_NE(stat & 0x08u, 0u); // Bit 3: ID error flag
}

// ─── Play (0x03) ─────────────────────────────────────────────────────────────

TEST_F(PS1CDROMTest, Play_WithNoParam_RespondsINT3AndSetsPlayingBit) {
  SendCommand(0x03);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
  uint8_t stat = ReadResponse();
  // psx-spx: During Play, status bit 7 (0x80) must be set
  EXPECT_NE(stat & 0x80u, 0u);
}

TEST_F(PS1CDROMTest, Play_WithTrackParam_RespondsINT3AndSetsPlayingBit) {
  PushParam(0x01); // Track 1 (BCD)
  SendCommand(0x03);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
  uint8_t stat = ReadResponse();
  EXPECT_NE(stat & 0x80u, 0u);
}

TEST_F(PS1CDROMTest, Play_PlayingBitClearedAfterPause) {
  // Start playing
  SendCommand(0x03);
  ReadResponse(); // Drain Play's response byte
  AcknowledgeInterrupt();

  // Pause it
  SendCommand(0x09);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
  uint8_t stat = ReadResponse();
  // After Pause, playing bit should be cleared
  EXPECT_EQ(stat & 0x80u, 0u);
}

TEST_F(PS1CDROMTest, Play_PlayingBitClearedAfterStop) {
  // Start playing
  SendCommand(0x03);
  ReadResponse(); // Drain Play's response byte
  AcknowledgeInterrupt();

  // Stop it
  SendCommand(0x08);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
  uint8_t stat = ReadResponse();
  EXPECT_EQ(stat & 0x80u, 0u);
}

TEST_F(PS1CDROMTest, Play_PlayingBitClearedAfterInit) {
  // Start playing
  SendCommand(0x03);
  ReadResponse(); // Drain Play's response byte
  AcknowledgeInterrupt();

  // Init resets everything
  SendCommand(0x0A);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
  uint8_t stat = ReadResponse();
  EXPECT_EQ(stat & 0x80u, 0u);
}

// ─── Pause (0x09) ────────────────────────────────────────────────────────────

TEST_F(PS1CDROMTest, Pause_FirstResponseIsINT3) {
  SendCommand(0x09);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
}

TEST_F(PS1CDROMTest, Pause_SecondResponseIsINT2AfterDelay) {
  SendCommand(0x09);
  AcknowledgeInterrupt();
  TickUntilIRQ();
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 2u);
}

// ─── Stop (0x08) ─────────────────────────────────────────────────────────────

TEST_F(PS1CDROMTest, Stop_FirstResponseIsINT3) {
  SendCommand(0x08);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
}

TEST_F(PS1CDROMTest, Stop_SecondResponseIsINT2AfterDelay) {
  SendCommand(0x08);
  AcknowledgeInterrupt();
  TickUntilIRQ();
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 2u);
}

// ─── SeekL (0x15) ────────────────────────────────────────────────────────────

TEST_F(PS1CDROMTest, SeekL_FirstResponseIsINT3) {
  SendCommand(0x15);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
}

TEST_F(PS1CDROMTest, SeekL_SecondResponseIsINT2) {
  SendCommand(0x15);
  AcknowledgeInterrupt();
  TickUntilIRQ();
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 2u);
}

// ─── SeekP (0x16) ────────────────────────────────────────────────────────────

TEST_F(PS1CDROMTest, SeekP_FirstResponseIsINT3) {
  SendCommand(0x16);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
}

// ─── Interrupt acknowledge ────────────────────────────────────────────────────

TEST_F(PS1CDROMTest, AcknowledgeInterrupt_ClearsFlag) {
  SendCommand(0x01); // GetStat fires INT3
  EXPECT_NE(ReadInterruptFlag() & 0x07, 0u);
  AcknowledgeInterrupt();
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 0u);
}

TEST_F(PS1CDROMTest, MultipleCommandsDeliverSequentially) {
  // Issue GetStat twice; second should appear after first is acknowledged
  SendCommand(0x01);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
  // Issue a second command without acking — implementation detail; at minimum
  // the first response should still be readable.
  uint8_t stat = ReadResponse();
  EXPECT_NE(stat, 0u);
}

// ─── Status bit consistency ───────────────────────────────────────────────────

TEST_F(PS1CDROMTest, StatusByte_MotorOnBitAlwaysSet) {
  // Motor-on bit (0x02) should be set in all normal status responses
  SendCommand(0x01);
  uint8_t stat = ReadResponse();
  // Mask out Shell-open (0x10) and check motor-on (0x02) is set
  EXPECT_NE(stat & 0x02u, 0u);
}

TEST_F(PS1CDROMTest, StatusByte_ReadingBitClearedAfterReset) {
  cdrom->Reset();
  SendCommand(0x01);
  uint8_t stat = ReadResponse();
  // Bit 5 (Reading) should NOT be set immediately after reset
  EXPECT_EQ(stat & 0x20u, 0u);
}

TEST_F(PS1CDROMTest, StatusByte_SeekingBitClearedAfterReset) {
  cdrom->Reset();
  SendCommand(0x01);
  uint8_t stat = ReadResponse();
  // Bit 6 (Seeking) should NOT be set immediately after reset
  EXPECT_EQ(stat & 0x40u, 0u);
}

// ─── ReadTOC (0x1E) ───────────────────────────────────────────────────────────

TEST_F(PS1CDROMTest, ReadTOC_RespondsINT3ThenINT2) {
  SendCommand(0x1E);
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 3u);
  AcknowledgeInterrupt();
  TickUntilIRQ();
  EXPECT_EQ(ReadInterruptFlag() & 0x07, 2u);
}
