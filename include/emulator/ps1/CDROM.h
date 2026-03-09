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
  const uint8_t *GetDiscDataPointer() const { return discData.data(); }
  size_t GetDiscDataSize() const { return discData.size(); }

  // ─── Timing ─────────────────────────────────────────────────────────
  void Tick(uint32_t cpuCycles);

  // ─── Sector-Level Access (for HLE BIOS EXE loading) ─────────────────
  static constexpr uint32_t RAW_SECTOR_SIZE = 2352;
  static constexpr uint32_t SECTOR_DATA_OFFSET = 24;
  static constexpr uint32_t SECTOR_DATA_SIZE = 2048;
  bool ReadSectorData(uint32_t sectorNum, uint8_t *out,
                      uint32_t size = SECTOR_DATA_SIZE) const;

  // ─── Interrupt State (for HLE BIOS event delivery) ──────────────────
  uint8_t GetInterruptFlag() const { return interruptFlag & 0x07; }
  // HLE BIOS calls this after reading the interrupt type so the CDROM
  // controller can deliver queued interrupts for subsequent commands.
  void AcknowledgeInterrupt();

  // ─── Read State (for HLE BIOS Timer2 suppression) ──────────────────
  bool IsReading() const { return reading; }
  bool IsCDBusy() const { return reading || readCooldown > 0; }

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

  // Queued interrupts: on real hardware new interrupts are held back until
  // the current one is acknowledged by the game.
  struct QueuedIRQ {
    uint8_t type;
    std::vector<uint8_t> response;
  };
  std::queue<QueuedIRQ> pendingIRQs;
  bool queuedDeliveryPending = false;
  uint32_t queuedDeliveryDelay = 0;

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
  uint32_t readCooldown = 0;
  uint8_t mode = 0;
  bool sectorBufferReady = false;

  // ─── XA-ADPCM Filter ───────────────────────────────────────────────
  uint8_t xaFilterFile = 0;
  uint8_t xaFilterChannel = 0;

  // ─── Delayed Second Response (for multi-response commands) ──────────
  bool secondResponsePending = false;
  uint32_t secondResponseDelay = 0;
  uint8_t secondResponseType = 0;
  std::vector<uint8_t> secondResponseData;

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
  void CmdSeekP();
  void CmdGetID();
  void CmdReadS();
  void CmdTest();
  void CmdReadTOC();
  void CmdMute();
  void CmdDemute();
  void CmdSetFilter();
  void CmdStop();
  void CmdPlay();
  void CmdGetlocL();
  void CmdGetlocP();
  void CmdGetTN();
  void CmdGetTD();

  void PushResponse(uint8_t value);
  void SetInterrupt(uint8_t type);
  void QueueSecondResponse(uint8_t intType, const std::vector<uint8_t> &data,
                           uint32_t delayCycles);
  uint32_t GetSectorOffset(uint8_t mm, uint8_t ss, uint8_t ff) const;
  static uint8_t BCDToDecimal(uint8_t bcd);
  static uint8_t DecimalToBCD(uint8_t dec);

  uint8_t GetStatusByte() const;
};

} // namespace AIO::Emulator::PS1
