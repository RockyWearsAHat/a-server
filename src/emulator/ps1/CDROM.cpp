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
  discLoaded = false;
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
  case 0x09:
    CmdPause();
    break;
  case 0x0A:
    CmdInit();
    break;
  case 0x0E:
    CmdSetMode();
    break;
  case 0x15:
    CmdSeekL();
    break;
  case 0x1A:
    CmdGetID();
    break;
  case 0x1B:
    CmdReadS();
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
}

void CDROM::CmdInit() {
  mode = 0;
  reading = false;
  seeking = false;
  PushResponse(GetStatusByte());
  SetInterrupt(3);
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
}

void CDROM::CmdGetID() {
  if (!discLoaded) {
    // No disc
    PushResponse(0x08); // Shell open
    SetInterrupt(5);
    return;
  }
  // Licensed disc response
  PushResponse(GetStatusByte());
  SetInterrupt(3);

  // Second response (INT2) with disc info — simplified
  // In a full implementation, this would be delayed
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
}

// ─── Helpers ────────────────────────────────────────────────────────────

void CDROM::PushResponse(uint8_t value) { responseFIFO.push(value); }

void CDROM::SetInterrupt(uint8_t type) {
  interruptFlag = type & 0x07;
  if (interruptEnable & interruptFlag) {
    interrupts.RequestIRQ(IRQ::CDROM);
  }
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
  // MSF to LBA, subtract 2-second pregap
  uint32_t lba = (mm * 60 + ss) * 75 + ff;
  if (lba >= 150)
    lba -= 150; // 2-second pregap

  // Each sector = 2352 bytes in raw disc image
  // Data-only offset = skip 24-byte header
  uint32_t sectorSize = 2352;
  return lba * sectorSize + 24; // Skip sync + header to get to data
}

uint8_t CDROM::BCDToDecimal(uint8_t bcd) {
  return (bcd >> 4) * 10 + (bcd & 0x0F);
}

uint8_t CDROM::DecimalToBCD(uint8_t dec) {
  return ((dec / 10) << 4) | (dec % 10);
}

// ─── DMA Interface ──────────────────────────────────────────────────────

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
