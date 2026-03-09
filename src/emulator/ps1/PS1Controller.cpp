#include "emulator/ps1/PS1Controller.h"
#include "emulator/ps1/InterruptController.h"
#include "emulator/ps1/PS1Constants.h"
#include <iomanip>
#include <sstream>

namespace AIO::Emulator::PS1 {

PS1Controller::PS1Controller(InterruptController &interrupts)
    : Loggable("PS1.Controller"), interrupts(interrupts) {
  Reset();
}

void PS1Controller::Reset() {
  pad.buttons = 0xFFFF;
  pad.connected = true;
  pad.padId = Controller::DIGITAL_PAD_ID;

  txData = 0;
  rxData = 0xFF;
  txReady = true;
  rxReady = false;

  stat = 0;
  mode = 0;
  ctrl = 0;
  baud = 0;

  commState = CommState::Idle;
  transferDelay = 0;
}

// ─── Register Interface ────────────────────────────────────────────────

uint8_t PS1Controller::ReadData() {
  if constexpr (Trace::CONTROLLER_TRACE) {
    LogDebug("Read JOY_DATA: %02X", rxData);
  }
  stat &= ~(1 << 1); // Clear RX-ready flag
  return rxData;
}

uint32_t PS1Controller::ReadStat() const {
  // Bit 0: TX ready
  // Bit 1: RX FIFO not empty
  // Bit 7: ACK input level
  uint32_t s = stat;
  if (txReady)
    s |= 1;
  if (rxReady)
    s |= (1 << 1);
  return s;
}

void PS1Controller::WriteData(uint8_t value) {
  if constexpr (Trace::CONTROLLER_TRACE) {
    LogDebug("Write JOY_DATA: %02X (state=%d)", value,
             static_cast<int>(commState));
  }
  txData = value;
  txReady = false;
  ProcessByte(value);
  txReady = true;
}

void PS1Controller::WriteStat(uint32_t value) {
  (void)value;
  stat &= ~(1 << 9); // Clear IRQ flag
}

void PS1Controller::WriteMode(uint16_t value) { mode = value; }

void PS1Controller::WriteCtrl(uint16_t value) {
  uint16_t prevCtrl = ctrl;
  ctrl = value;

  if (value & (1 << 4)) {
    stat &= ~(1 << 9); // ACK — clear IRQ flag
  }

  // Bit 6 = full reset
  if (value & (1 << 6)) {
    commState = CommState::Idle;
    stat = 0;
    mode = 0;
    baud = 0;
    txReady = true;
    rxReady = false;
  }

  // JOY Select (bit 1) deasserted → reset SIO state machine
  bool wasSelected = prevCtrl & (1 << 1);
  bool nowSelected = value & (1 << 1);
  if (wasSelected && !nowSelected) {
    commState = CommState::Idle;
    rxReady = false;
    txReady = true;
  }
}

void PS1Controller::WriteBaud(uint16_t value) { baud = value; }

// ─── Communication State Machine ───────────────────────────────────────

void PS1Controller::ProcessByte(uint8_t txByte) {
  if (!pad.connected) {
    rxData = 0xFF;
    return;
  }

  switch (commState) {
  case CommState::Idle:
    if (txByte == 0x01) {
      commState = CommState::SelectedPad;
      rxData = 0xFF;
      rxReady = true;
      stat |= (1 << 7);    // ACK
      transferDelay = 100; // Fire IRQ after short delay
    } else {
      rxData = 0xFF;
    }
    break;

  case CommState::SelectedPad:
    if (txByte == 0x42) {
      commState = CommState::SendingID_Hi;
      rxData = pad.padId;
      rxReady = true;
      stat |= (1 << 7);
      transferDelay = 100;
    } else {
      commState = CommState::Idle;
      rxData = 0xFF;
    }
    break;

  case CommState::SendingID_Hi:
    commState = CommState::SendingReady;
    rxData = 0x5A;
    rxReady = true;
    stat |= (1 << 7);
    transferDelay = 100;
    break;

  case CommState::SendingReady:
    commState = CommState::SendingButtons_Lo;
    rxData = static_cast<uint8_t>(pad.buttons & 0xFF);
    rxReady = true;
    stat |= (1 << 7);
    transferDelay = 100;
    break;

  case CommState::SendingButtons_Lo:
    commState = CommState::SendingButtons_Hi;
    rxData = static_cast<uint8_t>((pad.buttons >> 8) & 0xFF);
    rxReady = true;
    stat |= (1 << 7);
    transferDelay = 100;
    break;

  case CommState::SendingButtons_Hi:
    commState = CommState::Done;
    rxData = 0xFF;
    // No ACK on last byte
    break;

  case CommState::Done:
    commState = CommState::Idle;
    rxData = 0xFF;
    break;
  }
}

void PS1Controller::SetButtonState(uint16_t buttons) { pad.buttons = buttons; }

void PS1Controller::SetControllerConnected(bool connected) {
  pad.connected = connected;
}

void PS1Controller::Tick(uint32_t cycles) {
  if (transferDelay > 0) {
    if (cycles >= transferDelay) {
      transferDelay = 0;
      if (ctrl & (1 << 12)) {
        stat |= (1 << 9);
        interrupts.RequestIRQ(IRQ::SIO0);
      }
    } else {
      transferDelay -= cycles;
    }
  }
}

// ─── Debug ──────────────────────────────────────────────────────────────

void PS1Controller::DumpState(std::ostream &os) const {
  os << "=== PS1 Controller ===" << std::endl;
  os << "State: " << static_cast<int>(commState) << std::endl;
  os << "Buttons: " << std::hex << pad.buttons << std::endl;
  os << "STAT: " << stat << std::endl;
  os << "CTRL: " << ctrl << std::endl;
  os << "Connected: " << pad.connected << std::endl;
}

std::string PS1Controller::GetDebugSummary() const {
  std::ostringstream os;
  os << "Controller btns=" << std::hex << pad.buttons
     << " state=" << static_cast<int>(commState)
     << (pad.connected ? " [connected]" : " [disconnected]");
  return os.str();
}

} // namespace AIO::Emulator::PS1
