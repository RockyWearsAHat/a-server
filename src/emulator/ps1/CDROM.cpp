#include "emulator/ps1/CDROM.h"
#include "emulator/ps1/InterruptController.h"
#include "emulator/ps1/PS1Memory.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace AIO::Emulator::PS1 {

CDROM::CDROM(PS1Memory &memory, InterruptController &interrupts)
    : Loggable("PS1.CDROM"), memory(memory), interrupts(interrupts) {}

void CDROM::Reset() {
  index = 0;
  while (!parameterFIFO.empty())
    parameterFIFO.pop();
  while (!responseFIFO.empty())
    responseFIFO.pop();
  dataBuf.clear();
  dataReadPos = 0;
  interruptEnable = 0;
  interruptFlag = 0;
  while (!pendingIRQs.empty())
    pendingIRQs.pop();
  queuedDeliveryPending = false;
  queuedDeliveryDelay = 0;
  commandPending = false;
  pendingCommand = 0;
  commandDelay = 0;
  seekMinutes = 0;
  seekSeconds = 0;
  seekSector = 0;
  reading = false;
  seeking = false;
  readDelay = 0;
  mode = 0;
  sectorBufferReady = false;
  secondResponsePending = false;
  secondResponseDelay = 0;
  secondResponseType = 0;
  secondResponseData.clear();
}

bool CDROM::LoadDisc(const std::string &path) {
  std::string binPath = path;

  // Parse CUE sheets to extract the referenced BIN file
  if (path.size() >= 4) {
    std::string ext = path.substr(path.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".cue") {
      std::ifstream cueFile(path);
      if (!cueFile.is_open()) {
        LogError("Failed to open CUE file: %s", path.c_str());
        return false;
      }
      std::string line;
      bool foundFile = false;
      while (std::getline(cueFile, line)) {
        // Look for: FILE "filename.bin" BINARY
        auto pos = line.find("FILE");
        if (pos == std::string::npos)
          continue;
        auto q1 = line.find('"', pos);
        if (q1 == std::string::npos)
          continue;
        auto q2 = line.find('"', q1 + 1);
        if (q2 == std::string::npos)
          continue;
        std::string filename = line.substr(q1 + 1, q2 - q1 - 1);
        // Resolve relative to the CUE file's directory
        std::filesystem::path cuePath(path);
        binPath = (cuePath.parent_path() / filename).string();
        foundFile = true;
        LogInfo("CUE parsed: BIN file = %s", binPath.c_str());
        break;
      }
      if (!foundFile) {
        LogError("No FILE directive found in CUE: %s", path.c_str());
        return false;
      }
    }
  }

  std::ifstream file(binPath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    LogError("Failed to open disc image: %s", binPath.c_str());
    return false;
  }

  auto size = file.tellg();
  file.seekg(0);
  discData.resize(static_cast<size_t>(size));
  file.read(reinterpret_cast<char *>(discData.data()), size);

  discLoaded = true;
  LogInfo("Disc loaded: %s (%zu bytes)", binPath.c_str(), discData.size());
  return true;
}

// ─── Register Interface ─────────────────────────────────────────────────

uint8_t CDROM::Read8(uint32_t addr) {
  uint32_t reg = addr - IO::CDROM_BASE;

  switch (reg) {
  case 0: {
    // Status register
    uint8_t stat = index & 3;
    stat |= 0x18; // Parameter FIFO empty (bit 3), param FIFO not full (bit 4)
    if (!responseFIFO.empty())
      stat |= 0x20; // Response FIFO not empty
    if (!dataBuf.empty() && dataReadPos < dataBuf.size())
      stat |= 0x40; // Data FIFO not empty
    if (commandPending)
      stat |= 0x80; // Busy
    LogInfo("CDROM-R reg0 status=0x%02X idx=%d respEmpty=%d busy=%d", stat,
            index, responseFIFO.empty() ? 1 : 0, commandPending ? 1 : 0);
    return stat;
  }
  case 1: {
    // Response FIFO
    if (responseFIFO.empty()) {
      LogInfo("CDROM-R reg1 response=0x00 (empty)");
      return 0;
    }
    uint8_t val = responseFIFO.front();
    responseFIFO.pop();
    LogInfo("CDROM-R reg1 response=0x%02X remaining=%zu", val,
            responseFIFO.size());
    return val;
  }
  case 2: {
    // Data FIFO
    if (dataReadPos < dataBuf.size()) {
      return dataBuf[dataReadPos++];
    }
    return 0;
  }
  case 3: {
    if (index & 1) {
      // Interrupt flag (with index bit)
      uint8_t val = interruptFlag | 0xE0;
      LogInfo("CDROM-R reg3 intFlag=0x%02X (raw=0x%02X)", val, interruptFlag);
      return val;
    }
    LogInfo("CDROM-R reg3 intEnable=0x%02X", interruptEnable | 0xE0);
    return interruptEnable | 0xE0;
  }
  default:
    return 0;
  }
}

void CDROM::Write8(uint32_t addr, uint8_t value) {
  uint32_t reg = addr - IO::CDROM_BASE;

  LogInfo("CDROM-W reg%d.idx%d val=0x%02X", reg, index, value);

  switch (reg) {
  case 0:
    // Index register
    index = value & 3;
    break;
  case 1:
    switch (index) {
    case 0:
      // Command register
      commandPending = true;
      pendingCommand = value;
      commandDelay =
          5000; // ~150μs — close to real hardware first-response timing
      if constexpr (Trace::CDROM_TRACE) {
        LogDebug("CD command %02X queued", value);
      }
      break;
    case 1:
      break; // Sound Map Data Out
    case 2:
      break; // Sound Map Coding Info
    case 3:
      break; // Audio Volume Right to Right
    }
    break;
  case 2:
    switch (index) {
    case 0:
      // Parameter FIFO
      parameterFIFO.push(value);
      break;
    case 1:
      // Interrupt enable
      interruptEnable = value & 0x1F;
      break;
    case 2:
      break; // Audio Volume Left to Left
    case 3:
      break; // Audio Volume Right to Left
    }
    break;
  case 3:
    switch (index) {
    case 0:
      // Request register
      if (value & 0x80) {
        dataReadPos = 0;
        sectorBufferReady = false;
        // If still reading, deliver the next sector quickly after DMA
        // completes. Hardware delivers at CD speed (225k cycles double-speed),
        // but HLE BIOS polling is ~100x faster so the game's timeout counter
        // expires first. Deliver after a short delay so the IRQ handler chain
        // can complete.
        if (reading && readDelay > 500) {
          readDelay = 500;
        }
        LogInfo("CDROM request data: dataBuf.size=%zu dataReadPos=0 "
                "reading=%s readDelay=%u",
                dataBuf.size(), reading ? "true" : "false", readDelay);
      } else {
        LogInfo("CDROM clear data: dataBuf had %zu bytes", dataBuf.size());
        dataBuf.clear();
        dataReadPos = 0;
        sectorBufferReady = false;
      }
      break;
    case 1:
      // Interrupt flag (write to acknowledge)
      {
        uint8_t oldFlag = interruptFlag;
        interruptFlag &= ~(value & 0x1F);
        LogInfo("CDROM INT ack: val=0x%02X old=0x%02X new=0x%02X pending=%zu "
                "reading=%s sectorBufferReady=%s readDelay=%u",
                value, oldFlag, interruptFlag, pendingIRQs.size(),
                reading ? "true" : "false",
                sectorBufferReady ? "true" : "false", readDelay);
      }
      // De-assert IRQ line when all interrupt flags are cleared
      if ((interruptFlag & 0x07) == 0) {
        interrupts.ClearIRQ(IRQ::CDROM);

        // Schedule queued interrupt delivery via a tiny delay so the game
        // returns from the current exception before the next IRQ fires.
        if (!pendingIRQs.empty() && !queuedDeliveryPending) {
          queuedDeliveryPending = true;
          queuedDeliveryDelay = 4000; // ~120μs — enough for game to return and
                                      // set up its wait loop
        }
      }
      if (value & 0x40) {
        // Clear parameter FIFO
        while (!parameterFIFO.empty())
          parameterFIFO.pop();
      }
      break;
    case 2:
      break; // Audio Volume Left to Right
    case 3:
      break; // Audio Volume Apply
    }
    break;
  }
}

// ─── Timing ─────────────────────────────────────────────────────────────

void CDROM::Tick(uint32_t cpuCycles) {
  // Deliver queued interrupts after a short delay so the game has time
  // to return from the previous exception handler before the next fires.
  if (queuedDeliveryPending) {
    if (queuedDeliveryDelay <= cpuCycles) {
      queuedDeliveryPending = false;
      queuedDeliveryDelay = 0;

      if (!pendingIRQs.empty() && (interruptFlag & 0x07) == 0) {
        auto next = std::move(pendingIRQs.front());
        pendingIRQs.pop();

        while (!responseFIFO.empty())
          responseFIFO.pop();
        for (uint8_t b : next.response) {
          responseFIFO.push(b);
        }

        interruptFlag = next.type;
        LogInfo(
            "CDROM DeliverQueued INT%d intEnable=0x%02X flag=0x%02X willIRQ=%d",
            next.type, interruptEnable, interruptFlag,
            (interruptEnable & interruptFlag) ? 1 : 0);
        if (interruptEnable & interruptFlag) {
          interrupts.RequestIRQ(IRQ::CDROM);
        }
      }
    } else {
      queuedDeliveryDelay -= cpuCycles;
    }
  }

  if (commandPending) {
    if (commandDelay <= cpuCycles) {
      commandDelay = 0;
      commandPending = false;
      ExecuteCommand(pendingCommand);
    } else {
      commandDelay -= cpuCycles;
    }
  }

  // Handle delayed second response
  if (secondResponsePending) {
    if (secondResponseDelay <= cpuCycles) {
      secondResponseDelay = 0;
      secondResponsePending = false;

      // Clear response FIFO and push second response data
      while (!responseFIFO.empty())
        responseFIFO.pop();
      for (uint8_t byte : secondResponseData) {
        responseFIFO.push(byte);
      }
      secondResponseData.clear();
      SetInterrupt(secondResponseType);
    } else {
      secondResponseDelay -= cpuCycles;
    }
  }

  // Handle ongoing reads.
  // Don't deliver next sector while previous INT1 is unacknowledged —
  // the game must ACK, DMA the data, then clear the buffer before we
  // overwrite dataBuf with the next sector. This prevents the race
  // where a queued INT1 causes the game to DMA stale/empty data.
  if (reading && readDelay > 0 && !sectorBufferReady) {
    if (readDelay <= cpuCycles) {
      readDelay = 0;
      uint32_t sectorOffset =
          GetSectorOffset(seekMinutes, seekSeconds, seekSector);
      uint32_t dataSize =
          (mode & 0x20) ? 0x924 : 0x800; // Whole sector vs data only

      LogInfo("SectorReady: pos=%02u:%02u:%02u offset=0x%X dataSize=0x%X "
              "intFlag=0x%02X",
              seekMinutes, seekSeconds, seekSector, sectorOffset, dataSize,
              interruptFlag);

      if (sectorOffset + dataSize <= discData.size()) {
        dataBuf.assign(discData.begin() + sectorOffset,
                       discData.begin() + sectorOffset + dataSize);
        dataReadPos = 0;
        sectorBufferReady = true;
      } else {
        LogError("Sector read OOB: offset=0x%X size=0x%X discSize=0x%zX "
                 "pos=%02u:%02u:%02u",
                 sectorOffset, dataSize, discData.size(), seekMinutes,
                 seekSeconds, seekSector);
      }

      PushResponse(GetStatusByte());
      SetInterrupt(1);

      seekSector++;
      if (seekSector >= 75) {
        seekSector = 0;
        seekSeconds++;
        if (seekSeconds >= 60) {
          seekSeconds = 0;
          seekMinutes++;
        }
      }

      // Hardware-accurate inter-sector delay. The request-data handler above
      // will shorten this when the game signals it's ready for the next sector.
      bool doubleSpeed = mode & 0x80;
      readDelay = doubleSpeed ? (Clock::CPU_HZ / 150) : (Clock::CPU_HZ / 75);
    } else {
      readDelay -= cpuCycles;
    }
  }
}

// ─── Command Execution ─────────────────────────────────────────────────

void CDROM::ExecuteCommand(uint8_t cmd) {
  LogInfo("Executing CD command %02X", cmd);

  switch (cmd) {
  case 0x01:
    CmdGetStat();
    break;
  case 0x02:
    CmdSetLoc();
    break;
  case 0x06:
    CmdReadN();
    break;
  case 0x08:
    CmdStop();
    break;
  case 0x09:
    CmdPause();
    break;
  case 0x0A:
    CmdInit();
    break;
  case 0x0B:
    CmdMute();
    break;
  case 0x0C:
    CmdDemute();
    break;
  case 0x0D:
    CmdSetFilter();
    break;
  case 0x0E:
    CmdSetMode();
    break;
  case 0x15:
    CmdSeekL();
    break;
  case 0x16:
    CmdSeekP();
    break;
  case 0x1A:
    CmdGetID();
    break;
  case 0x1B:
    CmdReadS();
    break;
  case 0x03:
    CmdPlay();
    break;
  case 0x10:
    CmdGetlocL();
    break;
  case 0x11:
    CmdGetlocP();
    break;
  case 0x13:
    CmdGetTN();
    break;
  case 0x14:
    CmdGetTD();
    break;
  case 0x19:
    CmdTest();
    break;
  case 0x1E:
    CmdReadTOC();
    break;
  default:
    LogWarn("Unhandled CD command %02X", cmd);
    PushResponse(GetStatusByte() | 0x01); // Error bit
    SetInterrupt(5);                      // Error
    break;
  }

  // Clear parameter FIFO after command execution
  while (!parameterFIFO.empty())
    parameterFIFO.pop();
}

void CDROM::CmdGetStat() {
  PushResponse(GetStatusByte());
  SetInterrupt(3); // INT3 = Acknowledge
}

void CDROM::CmdSetLoc() {
  if (parameterFIFO.size() >= 3) {
    seekMinutes = BCDToDecimal(parameterFIFO.front());
    parameterFIFO.pop();
    seekSeconds = BCDToDecimal(parameterFIFO.front());
    parameterFIFO.pop();
    seekSector = BCDToDecimal(parameterFIFO.front());
    parameterFIFO.pop();

    LogInfo("SetLoc: %02u:%02u:%02u (offset=0x%X)", seekMinutes, seekSeconds,
            seekSector, GetSectorOffset(seekMinutes, seekSeconds, seekSector));
  }
  PushResponse(GetStatusByte());
  SetInterrupt(3);
}

void CDROM::CmdReadN() {
  reading = true;
  sectorBufferReady = false;
  bool doubleSpeed = mode & 0x80;
  readDelay = 5000;

  LogInfo("CmdReadN: reading=true readDelay=%u mode=0x%02X doubleSpeed=%d "
          "pos=%02u:%02u:%02u",
          readDelay, mode, doubleSpeed ? 1 : 0, seekMinutes, seekSeconds,
          seekSector);

  PushResponse(GetStatusByte());
  SetInterrupt(3);
}

void CDROM::CmdPause() {
  LogInfo("CmdPause: reading was %s, sectorBufferReady=%s readDelay=%u",
          reading ? "true" : "false", sectorBufferReady ? "true" : "false",
          readDelay);
  reading = false;
  PushResponse(GetStatusByte());
  SetInterrupt(3);
  // Second response after motor stops
  QueueSecondResponse(2, {GetStatusByte()}, 33868); // ~1ms delay
}

void CDROM::CmdInit() {
  mode = 0;
  reading = false;
  seeking = false;
  PushResponse(GetStatusByte());
  SetInterrupt(3);
  // Second response after initialization completes
  QueueSecondResponse(2, {GetStatusByte()}, 33868);
}

void CDROM::CmdSetMode() {
  if (!parameterFIFO.empty()) {
    mode = parameterFIFO.front();
    parameterFIFO.pop();
  }
  PushResponse(GetStatusByte());
  SetInterrupt(3);
}

void CDROM::CmdSeekL() {
  seeking = false; // Instant seek for now
  PushResponse(GetStatusByte());
  SetInterrupt(3);
  // Second response when seek completes
  QueueSecondResponse(2, {GetStatusByte()}, 33868);
}

void CDROM::CmdSeekP() {
  seeking = false;
  PushResponse(GetStatusByte());
  SetInterrupt(3);
  QueueSecondResponse(2, {GetStatusByte()}, 33868);
}

void CDROM::CmdGetID() {
  if (!discLoaded) {
    PushResponse(0x08); // Shell open
    SetInterrupt(5);
    return;
  }
  // First response: INT3 with status
  PushResponse(GetStatusByte());
  SetInterrupt(3);

  // Second response: INT2 with 8-byte disc identification
  // Byte 0-1: stat, flags (0x02=licensed, 0x00=audio disc)
  // Byte 2: disc type (0x20 = PS1 disc)
  // Byte 3: 0x00
  // Byte 4-7: "SCEI" (licensed for US) — ASCII region ID
  QueueSecondResponse(
      2, {GetStatusByte(), 0x00, 0x20, 0x00, 0x53, 0x43, 0x45, 0x49}, 33868);
}

void CDROM::CmdReadS() {
  // Same as ReadN but for streaming mode
  CmdReadN();
}

void CDROM::CmdTest() {
  if (!parameterFIFO.empty()) {
    uint8_t subCmd = parameterFIFO.front();
    parameterFIFO.pop();

    if (subCmd == 0x20) {
      // Get CDROM BIOS date/version
      PushResponse(0x94); // Year
      PushResponse(0x09); // Month
      PushResponse(0x19); // Day
      PushResponse(0xC0); // Version
      SetInterrupt(3);
    } else {
      PushResponse(GetStatusByte());
      SetInterrupt(3);
    }
  }
}

void CDROM::CmdReadTOC() {
  PushResponse(GetStatusByte());
  SetInterrupt(3);
  QueueSecondResponse(2, {GetStatusByte()}, 33868);
}

void CDROM::CmdPlay() {
  PushResponse(GetStatusByte());
  SetInterrupt(3);
}

void CDROM::CmdGetlocL() {
  // Return current logical sector position in header format
  PushResponse(DecimalToBCD(seekMinutes));
  PushResponse(DecimalToBCD(seekSeconds));
  PushResponse(DecimalToBCD(seekSector));
  PushResponse(mode); // current mode
  PushResponse(0);    // file
  PushResponse(0);    // channel
  PushResponse(0);    // sub-mode
  PushResponse(0);    // coding info
  SetInterrupt(3);
}

void CDROM::CmdGetlocP() {
  // Return current physical position (track, index, MM:SS:FF absolute and
  // relative)
  PushResponse(0x01); // track
  PushResponse(0x01); // index
  PushResponse(DecimalToBCD(seekMinutes));
  PushResponse(DecimalToBCD(seekSeconds));
  PushResponse(DecimalToBCD(seekSector));
  // Absolute position (same for single-track data disc)
  PushResponse(DecimalToBCD(seekMinutes));
  PushResponse(DecimalToBCD(seekSeconds));
  PushResponse(DecimalToBCD(seekSector));
  SetInterrupt(3);
}

void CDROM::CmdGetTN() {
  // Return first and last track number
  PushResponse(GetStatusByte());
  PushResponse(0x01); // first track (BCD)
  PushResponse(0x01); // last track (BCD) — single track for data discs
  SetInterrupt(3);
}

void CDROM::CmdGetTD() {
  // Return start position of track (param = track number in BCD)
  uint8_t track = 0;
  if (!parameterFIFO.empty()) {
    track = parameterFIFO.front();
    parameterFIFO.pop();
  }
  PushResponse(GetStatusByte());
  if (track == 0) {
    // Track 0 = total disc length
    uint32_t totalSectors =
        static_cast<uint32_t>(discData.size() / RAW_SECTOR_SIZE);
    uint32_t totalLBA = totalSectors;
    uint32_t mm = totalLBA / (75 * 60);
    uint32_t ss = (totalLBA / 75) % 60;
    PushResponse(DecimalToBCD(static_cast<uint8_t>(mm)));
    PushResponse(DecimalToBCD(static_cast<uint8_t>(ss)));
  } else {
    // Track 1 starts at 00:02:00 (after 2-second pregap)
    PushResponse(0x00); // MM
    PushResponse(0x02); // SS
  }
  SetInterrupt(3);
}

void CDROM::CmdStop() {
  reading = false;
  seeking = false;
  PushResponse(GetStatusByte());
  SetInterrupt(3);
  QueueSecondResponse(2, {GetStatusByte()}, 33868);
}

void CDROM::CmdMute() {
  PushResponse(GetStatusByte());
  SetInterrupt(3);
}

void CDROM::CmdDemute() {
  PushResponse(GetStatusByte());
  SetInterrupt(3);
}

void CDROM::CmdSetFilter() {
  // Consume parameters (file, channel) but don't implement XA filtering
  while (!parameterFIFO.empty())
    parameterFIFO.pop();
  PushResponse(GetStatusByte());
  SetInterrupt(3);
}

// ─── Helpers ────────────────────────────────────────────────────────────

void CDROM::PushResponse(uint8_t value) { responseFIFO.push(value); }

void CDROM::SetInterrupt(uint8_t type) {
  // Real hardware holds back new interrupts until the current one is
  // acknowledged. Queue if there's already an unacknowledged interrupt.
  if (interruptFlag & 0x07) {
    QueuedIRQ queued;
    queued.type = type & 0x07;
    // Snapshot the current response FIFO for the queued interrupt
    std::queue<uint8_t> copy = responseFIFO;
    while (!copy.empty()) {
      queued.response.push_back(copy.front());
      copy.pop();
    }
    pendingIRQs.push(std::move(queued));
    LogInfo("CDROM QueueInterrupt INT%d (current flag=0x%02X, queue depth=%zu)",
            type, interruptFlag, pendingIRQs.size());
    return;
  }

  interruptFlag = type & 0x07;
  LogInfo("CDROM SetInterrupt INT%d intEnable=0x%02X flag=0x%02X willIRQ=%d",
          type, interruptEnable, interruptFlag,
          (interruptEnable & interruptFlag) ? 1 : 0);
  if (interruptEnable & interruptFlag) {
    interrupts.RequestIRQ(IRQ::CDROM);
  }
}

void CDROM::QueueSecondResponse(uint8_t intType,
                                const std::vector<uint8_t> &data,
                                uint32_t delayCycles) {
  secondResponsePending = true;
  secondResponseDelay = delayCycles;
  secondResponseType = intType;
  secondResponseData = data;
}

uint8_t CDROM::GetStatusByte() const {
  uint8_t stat = 0x02; // Motor on
  if (reading)
    stat |= 0x20; // Reading
  if (seeking)
    stat |= 0x40; // Seeking
  if (!discLoaded)
    stat |= 0x10; // Shell open
  return stat;
}

uint32_t CDROM::GetSectorOffset(uint8_t mm, uint8_t ss, uint8_t ff) const {
  uint32_t lba = (mm * 60 + ss) * 75 + ff;
  if (lba >= 150)
    lba -= 150;

  // For whole-sector mode (bit 5), skip 12-byte sync to keep sub-header;
  // for data-only mode, skip 24 bytes (sync + header) to get 2048 bytes
  uint32_t headerSkip = (mode & 0x20) ? 12 : 24;
  return lba * RAW_SECTOR_SIZE + headerSkip;
}

uint8_t CDROM::BCDToDecimal(uint8_t bcd) {
  return (bcd >> 4) * 10 + (bcd & 0x0F);
}

uint8_t CDROM::DecimalToBCD(uint8_t dec) {
  return ((dec / 10) << 4) | (dec % 10);
}

// ─── DMA Interface ──────────────────────────────────────────────────────

bool CDROM::ReadSectorData(uint32_t sectorNum, uint8_t *out,
                           uint32_t size) const {
  uint64_t offset =
      static_cast<uint64_t>(sectorNum) * RAW_SECTOR_SIZE + SECTOR_DATA_OFFSET;
  if (offset + size > discData.size())
    return false;
  std::memcpy(out, discData.data() + offset, size);
  return true;
}

uint8_t CDROM::DMARead() {
  if (dataReadPos < dataBuf.size()) {
    return dataBuf[dataReadPos++];
  }
  return 0;
}

bool CDROM::HasDataToRead() const { return dataReadPos < dataBuf.size(); }

// ─── Debug ──────────────────────────────────────────────────────────────

void CDROM::DumpState(std::ostream &os) const {
  os << "=== PS1 CD-ROM ===" << std::endl;
  os << "Index: " << static_cast<int>(index) << std::endl;
  os << "Disc Loaded: " << (discLoaded ? "YES" : "NO") << std::endl;
  os << "Reading: " << reading << " Seeking: " << seeking << std::endl;
  os << "Seek Position: " << static_cast<int>(seekMinutes) << ":"
     << static_cast<int>(seekSeconds) << ":" << static_cast<int>(seekSector)
     << std::endl;
  os << "Mode: " << std::hex << static_cast<int>(mode) << std::endl;
  os << "INT Enable: " << static_cast<int>(interruptEnable)
     << " Flag: " << static_cast<int>(interruptFlag) << std::endl;
}

std::string CDROM::GetDebugSummary() const {
  std::ostringstream os;
  os << "CDROM reading=" << reading << " pos=" << static_cast<int>(seekMinutes)
     << ":" << static_cast<int>(seekSeconds) << ":"
     << static_cast<int>(seekSector);
  return os.str();
}

} // namespace AIO::Emulator::PS1
