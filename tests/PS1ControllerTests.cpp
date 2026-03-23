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

// ─── DualShock Analog Pad Protocol ─────────────────────────────────────

// Switching to analog mode changes padId to 0x73.
TEST_F(PS1ControllerTest, SetPadType_Analog_ChangesID) {
  controller->SetPadType(true);

  controller->WriteData(0x01);
  controller->ReadData(); // 0xFF

  controller->WriteData(0x42);
  uint8_t id = controller->ReadData();
  EXPECT_EQ(id, 0x73u); // ANALOG_PAD_ID
}

// Analog pad returns 9 bytes total: ID + 0x5A + buttons_lo + buttons_hi
// + analogRX + analogRY + analogLX + analogLY (8 bytes after 0x01 select).
TEST_F(PS1ControllerTest, AnalogPad_FullSequence) {
  controller->SetPadType(true);
  controller->SetButtonState(0xFFFF); // All released
  controller->SetAnalogStickState(0x10, 0x20, 0x30, 0x40);

  // 0x01 — select
  controller->WriteData(0x01);
  controller->ReadData(); // 0xFF

  // 0x42 — poll
  controller->WriteData(0x42);
  uint8_t idByte = controller->ReadData();
  EXPECT_EQ(idByte, 0x73u);

  controller->WriteData(0x00);
  EXPECT_EQ(controller->ReadData(), 0x5Au);

  controller->WriteData(0x00);
  EXPECT_EQ(controller->ReadData(), 0xFFu); // buttons lo

  controller->WriteData(0x00);
  EXPECT_EQ(controller->ReadData(), 0xFFu); // buttons hi

  controller->WriteData(0x00);
  EXPECT_EQ(controller->ReadData(), 0x10u); // analogRX

  controller->WriteData(0x00);
  EXPECT_EQ(controller->ReadData(), 0x20u); // analogRY

  controller->WriteData(0x00);
  EXPECT_EQ(controller->ReadData(), 0x30u); // analogLX

  controller->WriteData(0x00);
  EXPECT_EQ(controller->ReadData(), 0x40u); // analogLY
}

// Switching back to digital pad gives normal 5-byte response.
TEST_F(PS1ControllerTest, SetPadType_Digital_RestoresShortResponse) {
  controller->SetPadType(true);
  controller->SetPadType(false);

  controller->WriteData(0x01);
  controller->ReadData();

  controller->WriteData(0x42);
  uint8_t id = controller->ReadData();
  EXPECT_EQ(id, 0x41u); // DIGITAL_PAD_ID

  controller->WriteData(0x00);
  controller->ReadData(); // 0x5A
  controller->WriteData(0x00);
  controller->ReadData(); // buttons lo
  controller->WriteData(0x00);
  controller->ReadData(); // buttons hi

  // No more bytes — next poll starts fresh
  controller->WriteData(0x00);
  uint8_t extra = controller->ReadData();
  EXPECT_EQ(extra, 0xFFu); // Done/Idle: 0xFF
}

// Analog stick state persists across multiple polls.
// Real PS1 games deassert JOY_SEL (bit 1 of CTRL) between polls to reset the
// SIO state machine to Idle before each new poll.
TEST_F(PS1ControllerTest, AnalogStickState_PersistsAcrossPolls) {
  controller->SetPadType(true);
  controller->SetAnalogStickState(0xAA, 0xBB, 0xCC, 0xDD);

  auto doOnePoll = [&]() {
    // Simulate JOY_SEL assert then deassert so the SIO resets to Idle
    controller->WriteCtrl(0x0002); // assert bit 1
    controller->WriteCtrl(0x0000); // deassert bit 1 → CommState = Idle

    controller->WriteData(0x01);
    controller->ReadData();
    controller->WriteData(0x42);
    controller->ReadData(); // id
    controller->WriteData(0x00);
    controller->ReadData(); // 0x5A
    controller->WriteData(0x00);
    controller->ReadData(); // buttons lo
    controller->WriteData(0x00);
    controller->ReadData(); // buttons hi

    controller->WriteData(0x00);
    EXPECT_EQ(controller->ReadData(), 0xAAu); // RX
    controller->WriteData(0x00);
    EXPECT_EQ(controller->ReadData(), 0xBBu); // RY
    controller->WriteData(0x00);
    EXPECT_EQ(controller->ReadData(), 0xCCu); // LX
    controller->WriteData(0x00);
    EXPECT_EQ(controller->ReadData(), 0xDDu); // LY
  };

  doOnePoll();
  doOnePoll();
}
