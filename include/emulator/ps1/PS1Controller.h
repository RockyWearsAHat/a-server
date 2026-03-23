#pragma once

#include "PS1Constants.h"
#include "emulator/common/Loggable.h"
#include <array>
#include <cstdint>
#include <ostream>
#include <string>

namespace AIO::Emulator::PS1 {

class PS1Memory;
class InterruptController;

struct ControllerState {
  uint16_t buttons = 0xFFFF; // Active LOW
  uint8_t padId = Controller::DIGITAL_PAD_ID;
  bool connected = true;
  // DualShock analog sticks (0x80 = centre)
  uint8_t analogRX = 0x80; // Right stick X
  uint8_t analogRY = 0x80; // Right stick Y
  uint8_t analogLX = 0x80; // Left  stick X
  uint8_t analogLY = 0x80; // Left  stick Y
};

class PS1Controller : public Common::Loggable {
public:
  PS1Controller(InterruptController &interrupts);
  ~PS1Controller() = default;

  void Reset();

  // ─── Register Interface ─────────────────────────────────────────────
  uint8_t ReadData();
  uint32_t ReadStat() const;
  uint16_t ReadMode() const { return mode; }
  uint16_t ReadCtrl() const { return ctrl; }
  uint16_t ReadBaud() const { return baud; }

  void WriteData(uint8_t value);
  void WriteStat(uint32_t value);
  void WriteMode(uint16_t value);
  void WriteCtrl(uint16_t value);
  void WriteBaud(uint16_t value);

  // ─── Input State ────────────────────────────────────────────────────
  void SetButtonState(uint16_t buttons);
  void SetControllerConnected(bool connected);
  uint16_t GetButtonState() const {
    return pad.buttons;
  } // DualShock: set analog stick axes (0x00=full left/up, 0x80=centre,
    // 0xFF=full right/down)
  void SetAnalogStickState(uint8_t rx, uint8_t ry, uint8_t lx, uint8_t ly);
  // DualShock: switch between digital pad (0x41) and analog pad (0x73)
  void SetPadType(bool analog);
  // ─── Timing ─────────────────────────────────────────────────────────
  void Tick(uint32_t cpuCycles);

  // ─── Debug ──────────────────────────────────────────────────────────
  void DumpState(std::ostream &os) const;
  std::string GetDebugSummary() const;

private:
  InterruptController &interrupts;

  ControllerState pad;

  uint16_t mode = 0;
  uint16_t ctrl = 0;
  uint16_t baud = 0;
  uint32_t stat = 0;

  // SIO transfer state
  uint8_t txData = 0;
  uint8_t rxData = 0xFF;
  bool txReady = true;
  bool rxReady = false;

  // Communication state machine
  enum class CommState {
    Idle,
    SelectedPad,
    SendingID_Hi,
    SendingReady,
    SendingButtons_Lo,
    SendingButtons_Hi,
    // DualShock analog extension (additional bytes after digital buttons)
    SendingAnalogRX,
    SendingAnalogRY,
    SendingAnalogLX,
    SendingAnalogLY,
    Done
  };
  CommState commState = CommState::Idle;
  uint32_t transferDelay = 0;

  void ProcessByte(uint8_t value);
};

} // namespace AIO::Emulator::PS1
