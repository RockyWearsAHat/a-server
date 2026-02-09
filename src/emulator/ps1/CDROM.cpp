#include "emulator/ps1/CDROM.h"
#include "emulator/ps1/InterruptController.h"
#include "emulator/ps1/PS1Memory.h"
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
  secondResponsePending = false;
  secondResponseDelay = 0;
  secondResponseType = 0;
  secondResponseData.clear();
}

bool CDROM::LoadDisc(const std::string &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    LogError("Failed to open disc image: %s", path.c_str());
    return false;
  }

  auto size = file.tellg();
  file.seekg(0);
  discData.resize(static_cast<size_t>(size));
  file.read(reinterpret_cast<char *>(discData.data()), size);

  discLoaded = true;
  LogInfo("Disc loaded: %s (%zu bytes)", path.c_str(), discData.size());
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
    return stat;
  }
  case 1: {
    // Response FIFO
    if (responseFIFO.empty())
      return 0;
    uint8_t val = responseFIFO.front();
    responseFIFO.pop();
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
      return interruptFlag | 0xE0; // Top 3 bits always set
    }
    return interruptEnable | 0xE0;
  }
  default:
    return 0;
  }
}

void CDROM::Write8(uint32_t addr, uint8_t value) {
  uint32_t reg = addr - IO::CDROM_BASE;

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
      commandDelay = 50000; // Approximate delay in CPU cycles
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
        // Want data — load sector data into buffer
      } else {
        dataBuf.clear();
        dataReadPos = 0;
      }
      break;
    case 1:
      // Interrupt flag (write to acknowledge)
      interruptFlag &= ~(value & 0x1F);
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

  // Handle ongoing reads
  if (reading && readDelay > 0) {
    if (readDelay <= cpuCycles) {
      readDelay = 0;
      // Read a sector
      uint32_t sectorOffset =
          GetSectorOffset(seekMinutes, seekSeconds, seekSector);
      uint32_t dataSize =
          (mode & 0x20) ? 0x924 : 0x800; // Whole sector vs data only

      if (sectorOffset + dataSize <= discData.size()) {
        dataBuf.assign(discData.begin() + sectorOffset,
                       discData.begin() + sectorOffset + dataSize);
        dataReadPos = 0;
      }

      // Set interrupt for data ready
      PushResponse(GetStatusByte());
      SetInterrupt(1);

      // Advance to next sector
      seekSector++;
      if (seekSector >= 75) {
        seekSector = 0;
        seekSeconds++;
        if (seekSeconds >= 60) {
          seekSeconds = 0;
          seekMinutes++;
        }
      }

      // Schedule next sector read (single speed ≈ 75 sectors/sec)
      bool doubleSpeed = mode & 0x80;
      readDelay = doubleSpeed ? (Clock::CPU_HZ / 150) : (Clock::CPU_HZ / 75);
    } else {
      readDelay -= cpuCycles;
    }
  }
}

// ─── Command Execution ─────────────────────────────────────────────────

void CDROM::ExecuteCommand(uint8_t cmd) {
  if constexpr (Trace::CDROM_TRACE) {
    LogDebug("Executing CD command %02X", cmd);
  }

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

    if constexpr (Trace::CDROM_TRACE) {
      LogDebug("SetLoc: %02u:%02u:%02u", seekMinutes, seekSeconds, seekSector);
    }
  }
  PushResponse(GetStatusByte());
  SetInterrupt(3);
}

void CDROM::CmdReadN() {
  reading = true;
  bool doubleSpeed = mode & 0x80;
  readDelay = doubleSpeed ? (Clock::CPU_HZ / 150) : (Clock::CPU_HZ / 75);

  PushResponse(GetStatusByte());
  SetInterrupt(3);
}

void CDROM::CmdPause() {
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
  interruptFlag = type & 0x07;
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
