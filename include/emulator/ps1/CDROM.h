#pragma once

#include "PS1Constants.h"
#include "emulator/common/Loggable.h"
#include <array>
#include <cstdint>
#include <ostream>
#include <queue>
#include <string>
#include <vector>

namespace AIO::Emulator::PS1 {

class PS1Memory;
class InterruptController;

class CDROM : public Common::Loggable {
public:
  CDROM(PS1Memory &memory, InterruptController &interrupts);
  ~CDROM() = default;

  void Reset();

  // ─── Register Interface ─────────────────────────────────────────────
  uint8_t Read8(uint32_t addr);
  void Write8(uint32_t addr, uint8_t value);

  // ─── Disc Image ─────────────────────────────────────────────────────
  bool LoadDisc(const std::string &path);
  bool HasDisc() const { return discLoaded; }

  // ─── Timing ─────────────────────────────────────────────────────────
  void Tick(uint32_t cpuCycles);

  // ─── DMA Interface ──────────────────────────────────────────────────
  uint8_t DMARead();
  bool HasDataToRead() const;

  // ─── Debug ──────────────────────────────────────────────────────────
  void DumpState(std::ostream &os) const;
  std::string GetDebugSummary() const;

private:
  PS1Memory &memory;
  InterruptController &interrupts;

  // ─── Status ─────────────────────────────────────────────────────────
  uint8_t index = 0; // Index/Status register (bits 0-1)
  bool discLoaded = false;

  // ─── FIFOs ──────────────────────────────────────────────────────────
  std::queue<uint8_t> parameterFIFO;
  std::queue<uint8_t> responseFIFO;
  std::vector<uint8_t> dataBuf;
  uint32_t dataReadPos = 0;

  // ─── Interrupt ──────────────────────────────────────────────────────
  uint8_t interruptEnable = 0;
  uint8_t interruptFlag = 0;

  // ─── Command Processing ─────────────────────────────────────────────
  bool commandPending = false;
  uint8_t pendingCommand = 0;
  uint32_t commandDelay = 0; // Cycles until command completes

  // ─── Seek/Read State ────────────────────────────────────────────────
  uint8_t seekMinutes = 0;
  uint8_t seekSeconds = 0;
  uint8_t seekSector = 0;
  bool reading = false;
  bool seeking = false;
  uint32_t readDelay = 0;
  uint8_t mode = 0;

  // ─── Disc Data ──────────────────────────────────────────────────────
  std::vector<uint8_t> discData;

  // ─── Command Handlers ───────────────────────────────────────────────
  void ExecuteCommand(uint8_t cmd);
  void CmdGetStat();
  void CmdSetLoc();
  void CmdReadN();
  void CmdPause();
  void CmdInit();
  void CmdSetMode();
  void CmdSeekL();
  void CmdGetID();
  void CmdReadS();
  void CmdTest();
  void CmdReadTOC();

  void PushResponse(uint8_t value);
  void SetInterrupt(uint8_t type);
  uint32_t GetSectorOffset(uint8_t mm, uint8_t ss, uint8_t ff) const;
  static uint8_t BCDToDecimal(uint8_t bcd);
  static uint8_t DecimalToBCD(uint8_t dec);

  uint8_t GetStatusByte() const;
};

} // namespace AIO::Emulator::PS1
