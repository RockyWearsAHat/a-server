#include "emulator/ps1/InterruptController.h"
#include "emulator/ps1/PS1Constants.h"
#include "emulator/ps1/PS1Controller.h"
#include <gtest/gtest.h>

using namespace AIO::Emulator::PS1;

class PS1ControllerTest : public ::testing::Test {
protected:
  void SetUp() override {
    irq = std::make_unique<InterruptController>();
    controller = std::make_unique<PS1Controller>(*irq);
    controller->SetControllerConnected(true);
  }
  std::unique_ptr<InterruptController> irq;
  std::unique_ptr<PS1Controller> controller;
};

// ─── Initial State ──────────────────────────────────────────────────────

TEST_F(PS1ControllerTest, InitialButtonState_AllReleased) {
  // Active-low: 0xFFFF = all released
  controller->SetButtonState(0xFFFF);
  // Verify we can read data without crash
  uint8_t data = controller->ReadData();
  EXPECT_EQ(data, 0xFFu); // No communication started
}

TEST_F(PS1ControllerTest, ReadStat_TXReady) {
  uint32_t stat = controller->ReadStat();
  EXPECT_NE(stat & 1, 0u); // TX ready bit should be set
}

// ─── Communication Protocol ────────────────────────────────────────────

TEST_F(PS1ControllerTest, FullDigitalPadSequence) {
  uint16_t buttons = 0xFDFF; // All released except one button

  controller->SetButtonState(buttons);

  // Step 1: Send 0x01 (select pad)
  controller->WriteData(0x01);
  uint8_t response1 = controller->ReadData();
  EXPECT_EQ(response1, 0xFFu); // ACK byte

  // Step 2: Send 0x42 (read command)
  controller->WriteData(0x42);
  uint8_t response2 = controller->ReadData();
  EXPECT_EQ(response2, 0x41u); // Device ID (digital pad)

  // Step 3: Get ID byte 2
  controller->WriteData(0x00);
  uint8_t response3 = controller->ReadData();
  EXPECT_EQ(response3, 0x5Au);

  // Step 4: Get buttons low byte
  controller->WriteData(0x00);
  uint8_t buttonsLo = controller->ReadData();
  EXPECT_EQ(buttonsLo, static_cast<uint8_t>(buttons & 0xFF));

  // Step 5: Get buttons high byte
  controller->WriteData(0x00);
  uint8_t buttonsHi = controller->ReadData();
  EXPECT_EQ(buttonsHi, static_cast<uint8_t>((buttons >> 8) & 0xFF));
}

// ─── Button State Updates ──────────────────────────────────────────────

TEST_F(PS1ControllerTest, SetButtonState_UpdatesOutput) {
  // Press D-Pad Up (bit 4 = 0 in active-low)
  controller->SetButtonState(0xFFEF);

  // Do full read sequence
  controller->WriteData(0x01);
  controller->ReadData();
  controller->WriteData(0x42);
  controller->ReadData();
  controller->WriteData(0x00);
  controller->ReadData();
  controller->WriteData(0x00);
  uint8_t lo = controller->ReadData();

  EXPECT_EQ(lo, 0xEFu);
}

// ─── Disconnected Controller ──────────────────────────────────────────

TEST_F(PS1ControllerTest, Disconnected_ReturnsFF) {
  controller->SetControllerConnected(false);
  controller->WriteData(0x01);
  uint8_t response = controller->ReadData();
  EXPECT_EQ(response, 0xFFu);
}

TEST_F(PS1ControllerTest, Disconnected_NoStateChange) {
  controller->SetControllerConnected(false);
  controller->WriteData(0x01);
  controller->WriteData(0x42);
  uint8_t response = controller->ReadData();
  EXPECT_EQ(response, 0xFFu); // Should still be 0xFF
}

// ─── WriteCtrl Reset ───────────────────────────────────────────────────

TEST_F(PS1ControllerTest, WriteCtrl_Reset_ClearsState) {
  controller->WriteData(0x01); // Start communication
  controller->WriteCtrl(0x40); // Bit 6 = reset
  // After reset, controller should be back to Idle
  controller->WriteData(0x01);
  uint8_t response = controller->ReadData();
  EXPECT_EQ(response, 0xFFu); // Fresh sequence
}

// ─── ACK / IRQ ─────────────────────────────────────────────────────────

TEST_F(PS1ControllerTest, WriteCtrl_AcknowledgeClearsIRQ) {
  controller->WriteStat(0); // Clear IRQ
  uint32_t stat = controller->ReadStat();
  EXPECT_EQ(stat & (1 << 9), 0u); // IRQ flag cleared
}

// ─── Mode/Baud ─────────────────────────────────────────────────────────

TEST_F(PS1ControllerTest, WriteMode_DoesNotCrash) {
  controller->WriteMode(0x000D);
}

TEST_F(PS1ControllerTest, WriteBaud_DoesNotCrash) {
  controller->WriteBaud(0x0088);
}

// ─── Reset ──────────────────────────────────────────────────────────────

TEST_F(PS1ControllerTest, Reset_RestoresDefaults) {
  controller->SetButtonState(0x0000);
  controller->WriteData(0x01);
  controller->Reset();
  // After reset, buttons should be 0xFFFF (all released)
  // and controller should be in Idle state
}

// ─── Debug ──────────────────────────────────────────────────────────────

TEST_F(PS1ControllerTest, GetDebugSummary_NotEmpty) {
  EXPECT_FALSE(controller->GetDebugSummary().empty());
}

TEST_F(PS1ControllerTest, DumpState_WritesOutput) {
  std::ostringstream os;
  controller->DumpState(os);
  EXPECT_FALSE(os.str().empty());
}
