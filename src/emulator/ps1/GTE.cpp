#include "emulator/ps1/GTE.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace AIO::Emulator::PS1 {

namespace {

bool IsPs1GteDiagEnabled() {
  const char *value = std::getenv("AIO_PS1_GTE_DIAG");
  return value != nullptr && value[0] != '\0' && value[0] != '0';
}

void AppendGteDiagSummary(
    uint64_t totalCommands, const std::array<uint64_t, 64> &opcodeCounts,
    const std::array<std::array<std::array<uint64_t, 4>, 4>, 4> &mvmvaCounts) {
  std::ofstream out("/tmp/ps1_gte_diag_summary.txt", std::ios::app);
  out << "total=" << totalCommands;
  for (uint32_t opcode = 0; opcode < opcodeCounts.size(); ++opcode) {
    if (opcodeCounts[opcode] == 0)
      continue;
    out << ' ' << std::hex << std::uppercase << std::setw(2)
        << std::setfill('0') << opcode << std::dec << '='
        << opcodeCounts[opcode];
  }
  for (uint32_t mx = 0; mx < 4; ++mx) {
    for (uint32_t vx = 0; vx < 4; ++vx) {
      for (uint32_t tx = 0; tx < 4; ++tx) {
        const uint64_t count = mvmvaCounts[mx][vx][tx];
        if (count == 0)
          continue;
        out << " m" << mx << 'v' << vx << 't' << tx << '=' << count;
      }
    }
  }
  out << '\n';
}

} // namespace

GTE::GTE() : Loggable("PS1.GTE") {
  if (IsPs1GteDiagEnabled()) {
    std::ofstream("/tmp/ps1_gte_diag_summary.txt", std::ios::trunc)
        << "PS1 GTE diagnostics enabled\n";
  }
}

void GTE::Reset() {
  for (auto &row : v)
    for (auto &val : row)
      val = 0;
  rgbc = 0;
  otz = 0;
  for (auto &val : ir)
    val = 0;
  for (auto &row : sxy)
    for (auto &val : row)
      val = 0;
  for (auto &val : sz)
    val = 0;
  for (auto &val : rgb)
    val = 0;
  for (auto &val : mac)
    val = 0;
  irgb = 0;
  orgb = 0;
  lzcs = 0;
  lzcr = 0;

  for (auto &row : rt)
    for (auto &val : row)
      val = 0;
  for (auto &val : tr)
    val = 0;
  for (auto &row : l)
    for (auto &val : row)
      val = 0;
  for (auto &val : bk)
    val = 0;
  for (auto &row : lr)
    for (auto &val : row)
      val = 0;
  for (auto &val : fc)
    val = 0;
  ofx = 0;
  ofy = 0;
  h = 0;
  dqa = 0;
  dqb = 0;
  zsf3 = 0;
  zsf4 = 0;
  flag = 0;
  lastCommandCycles = 0;
}

// ─── Data Register Read ────────────────────────────────────────────────

uint32_t GTE::ReadData(uint32_t reg) const {
  switch (reg) {
  case 0:
    return (static_cast<uint16_t>(v[0][0])) |
           (static_cast<uint32_t>(static_cast<uint16_t>(v[0][1])) << 16);
  case 1:
    return static_cast<uint16_t>(v[0][2]);
  case 2:
    return (static_cast<uint16_t>(v[1][0])) |
           (static_cast<uint32_t>(static_cast<uint16_t>(v[1][1])) << 16);
  case 3:
    return static_cast<uint16_t>(v[1][2]);
  case 4:
    return (static_cast<uint16_t>(v[2][0])) |
           (static_cast<uint32_t>(static_cast<uint16_t>(v[2][1])) << 16);
  case 5:
    return static_cast<uint16_t>(v[2][2]);
  case 6:
    return rgbc;
  case 7:
    return otz;
  case 8:
    return static_cast<uint32_t>(static_cast<int32_t>(ir[0]));
  case 9:
    return static_cast<uint32_t>(static_cast<int32_t>(ir[1]));
  case 10:
    return static_cast<uint32_t>(static_cast<int32_t>(ir[2]));
  case 11:
    return static_cast<uint32_t>(static_cast<int32_t>(ir[3]));
  case 12:
    return (static_cast<uint16_t>(sxy[0][0])) |
           (static_cast<uint32_t>(static_cast<uint16_t>(sxy[0][1])) << 16);
  case 13:
    return (static_cast<uint16_t>(sxy[1][0])) |
           (static_cast<uint32_t>(static_cast<uint16_t>(sxy[1][1])) << 16);
  case 14:
  case 15:
    return (static_cast<uint16_t>(sxy[2][0])) |
           (static_cast<uint32_t>(static_cast<uint16_t>(sxy[2][1])) << 16);
  case 16:
    return sz[0];
  case 17:
    return sz[1];
  case 18:
    return sz[2];
  case 19:
    return sz[3];
  case 20:
    return rgb[0];
  case 21:
    return rgb[1];
  case 22:
    return rgb[2];
  case 23:
    return 0; // Prohibited
  case 24:
    return static_cast<uint32_t>(mac[0]);
  case 25:
    return static_cast<uint32_t>(mac[1]);
  case 26:
    return static_cast<uint32_t>(mac[2]);
  case 27:
    return static_cast<uint32_t>(mac[3]);
  case 28:
  case 29:
    // IRGB/ORGB — read-only conversion
    return static_cast<uint32_t>(
        std::clamp<int>(ir[1] / 0x80, 0, 0x1F) |
        (std::clamp<int>(ir[2] / 0x80, 0, 0x1F) << 5) |
        (std::clamp<int>(ir[3] / 0x80, 0, 0x1F) << 10));
  case 30:
    return static_cast<uint32_t>(lzcs);
  case 31:
    return static_cast<uint32_t>(lzcr);
  default:
    return 0;
  }
}

// ─── Data Register Write ───────────────────────────────────────────────

void GTE::WriteData(uint32_t reg, uint32_t value) {
  switch (reg) {
  case 0:
    v[0][0] = static_cast<int16_t>(value);
    v[0][1] = static_cast<int16_t>(value >> 16);
    break;
  case 1:
    v[0][2] = static_cast<int16_t>(value);
    break;
  case 2:
    v[1][0] = static_cast<int16_t>(value);
    v[1][1] = static_cast<int16_t>(value >> 16);
    break;
  case 3:
    v[1][2] = static_cast<int16_t>(value);
    break;
  case 4:
    v[2][0] = static_cast<int16_t>(value);
    v[2][1] = static_cast<int16_t>(value >> 16);
    break;
  case 5:
    v[2][2] = static_cast<int16_t>(value);
    break;
  case 6:
    rgbc = value;
    break;
  case 7:
    otz = static_cast<uint16_t>(value);
    break;
  case 8:
    ir[0] = static_cast<int16_t>(value);
    break;
  case 9:
    ir[1] = static_cast<int16_t>(value);
    break;
  case 10:
    ir[2] = static_cast<int16_t>(value);
    break;
  case 11:
    ir[3] = static_cast<int16_t>(value);
    break;
  case 12:
    sxy[0][0] = static_cast<int16_t>(value);
    sxy[0][1] = static_cast<int16_t>(value >> 16);
    break;
  case 13:
    sxy[1][0] = static_cast<int16_t>(value);
    sxy[1][1] = static_cast<int16_t>(value >> 16);
    break;
  case 14:
    sxy[2][0] = static_cast<int16_t>(value);
    sxy[2][1] = static_cast<int16_t>(value >> 16);
    break;
  case 15:
    // SXY FIFO push
    PushSXY(static_cast<int16_t>(value), static_cast<int16_t>(value >> 16));
    break;
  case 16:
    sz[0] = static_cast<uint16_t>(value);
    break;
  case 17:
    sz[1] = static_cast<uint16_t>(value);
    break;
  case 18:
    sz[2] = static_cast<uint16_t>(value);
    break;
  case 19:
    sz[3] = static_cast<uint16_t>(value);
    break;
  case 20:
    rgb[0] = value;
    break;
  case 21:
    rgb[1] = value;
    break;
  case 22:
    rgb[2] = value;
    break;
  case 24:
    mac[0] = static_cast<int32_t>(value);
    break;
  case 25:
    mac[1] = static_cast<int32_t>(value);
    break;
  case 26:
    mac[2] = static_cast<int32_t>(value);
    break;
  case 27:
    mac[3] = static_cast<int32_t>(value);
    break;
  case 28:
    irgb = value;
    ir[1] = static_cast<int16_t>((value & 0x1F) * 0x80);
    ir[2] = static_cast<int16_t>(((value >> 5) & 0x1F) * 0x80);
    ir[3] = static_cast<int16_t>(((value >> 10) & 0x1F) * 0x80);
    break;
  case 30:
    lzcs = static_cast<int32_t>(value);
    lzcr = CountLeadingZeros(lzcs);
    break;
  }
}

// ─── Control Register Read ─────────────────────────────────────────────

uint32_t GTE::ReadControl(uint32_t reg) const {
  switch (reg) {
  case 0:
    return (static_cast<uint16_t>(rt[0][0])) |
           (static_cast<uint32_t>(static_cast<uint16_t>(rt[0][1])) << 16);
  case 1:
    return (static_cast<uint16_t>(rt[0][2])) |
           (static_cast<uint32_t>(static_cast<uint16_t>(rt[1][0])) << 16);
  case 2:
    return (static_cast<uint16_t>(rt[1][1])) |
           (static_cast<uint32_t>(static_cast<uint16_t>(rt[1][2])) << 16);
  case 3:
    return (static_cast<uint16_t>(rt[2][0])) |
           (static_cast<uint32_t>(static_cast<uint16_t>(rt[2][1])) << 16);
  case 4:
    return static_cast<uint16_t>(rt[2][2]);
  case 5:
    return static_cast<uint32_t>(tr[0]);
  case 6:
    return static_cast<uint32_t>(tr[1]);
  case 7:
    return static_cast<uint32_t>(tr[2]);
  case 8:
    return (static_cast<uint16_t>(l[0][0])) |
           (static_cast<uint32_t>(static_cast<uint16_t>(l[0][1])) << 16);
  case 9:
    return (static_cast<uint16_t>(l[0][2])) |
           (static_cast<uint32_t>(static_cast<uint16_t>(l[1][0])) << 16);
  case 10:
    return (static_cast<uint16_t>(l[1][1])) |
           (static_cast<uint32_t>(static_cast<uint16_t>(l[1][2])) << 16);
  case 11:
    return (static_cast<uint16_t>(l[2][0])) |
           (static_cast<uint32_t>(static_cast<uint16_t>(l[2][1])) << 16);
  case 12:
    return static_cast<uint16_t>(l[2][2]);
  case 13:
    return static_cast<uint32_t>(bk[0]);
  case 14:
    return static_cast<uint32_t>(bk[1]);
  case 15:
    return static_cast<uint32_t>(bk[2]);
  case 16:
    return (static_cast<uint16_t>(lr[0][0])) |
           (static_cast<uint32_t>(static_cast<uint16_t>(lr[0][1])) << 16);
  case 17:
    return (static_cast<uint16_t>(lr[0][2])) |
           (static_cast<uint32_t>(static_cast<uint16_t>(lr[1][0])) << 16);
  case 18:
    return (static_cast<uint16_t>(lr[1][1])) |
           (static_cast<uint32_t>(static_cast<uint16_t>(lr[1][2])) << 16);
  case 19:
    return (static_cast<uint16_t>(lr[2][0])) |
           (static_cast<uint32_t>(static_cast<uint16_t>(lr[2][1])) << 16);
  case 20:
    return static_cast<uint16_t>(lr[2][2]);
  case 21:
    return static_cast<uint32_t>(fc[0]);
  case 22:
    return static_cast<uint32_t>(fc[1]);
  case 23:
    return static_cast<uint32_t>(fc[2]);
  case 24:
    return static_cast<uint32_t>(ofx);
  case 25:
    return static_cast<uint32_t>(ofy);
  case 26:
    return static_cast<uint32_t>(static_cast<int32_t>(h));
  case 27:
    return static_cast<uint32_t>(static_cast<int32_t>(dqa));
  case 28:
    return static_cast<uint32_t>(dqb);
  case 29:
    return static_cast<uint32_t>(static_cast<int32_t>(zsf3));
  case 30:
    return static_cast<uint32_t>(static_cast<int32_t>(zsf4));
  case 31:
    return flag;
  default:
    return 0;
  }
}

// ─── Control Register Write ────────────────────────────────────────────

void GTE::WriteControl(uint32_t reg, uint32_t value) {
  switch (reg) {
  case 0:
    rt[0][0] = static_cast<int16_t>(value);
    rt[0][1] = static_cast<int16_t>(value >> 16);
    break;
  case 1:
    rt[0][2] = static_cast<int16_t>(value);
    rt[1][0] = static_cast<int16_t>(value >> 16);
    break;
  case 2:
    rt[1][1] = static_cast<int16_t>(value);
    rt[1][2] = static_cast<int16_t>(value >> 16);
    break;
  case 3:
    rt[2][0] = static_cast<int16_t>(value);
    rt[2][1] = static_cast<int16_t>(value >> 16);
    break;
  case 4:
    rt[2][2] = static_cast<int16_t>(value);
    break;
  case 5:
    tr[0] = static_cast<int32_t>(value);
    break;
  case 6:
    tr[1] = static_cast<int32_t>(value);
    break;
  case 7:
    tr[2] = static_cast<int32_t>(value);
    break;
  case 8:
    l[0][0] = static_cast<int16_t>(value);
    l[0][1] = static_cast<int16_t>(value >> 16);
    break;
  case 9:
    l[0][2] = static_cast<int16_t>(value);
    l[1][0] = static_cast<int16_t>(value >> 16);
    break;
  case 10:
    l[1][1] = static_cast<int16_t>(value);
    l[1][2] = static_cast<int16_t>(value >> 16);
    break;
  case 11:
    l[2][0] = static_cast<int16_t>(value);
    l[2][1] = static_cast<int16_t>(value >> 16);
    break;
  case 12:
    l[2][2] = static_cast<int16_t>(value);
    break;
  case 13:
    bk[0] = static_cast<int32_t>(value);
    break;
  case 14:
    bk[1] = static_cast<int32_t>(value);
    break;
  case 15:
    bk[2] = static_cast<int32_t>(value);
    break;
  case 16:
    lr[0][0] = static_cast<int16_t>(value);
    lr[0][1] = static_cast<int16_t>(value >> 16);
    break;
  case 17:
    lr[0][2] = static_cast<int16_t>(value);
    lr[1][0] = static_cast<int16_t>(value >> 16);
    break;
  case 18:
    lr[1][1] = static_cast<int16_t>(value);
    lr[1][2] = static_cast<int16_t>(value >> 16);
    break;
  case 19:
    lr[2][0] = static_cast<int16_t>(value);
    lr[2][1] = static_cast<int16_t>(value >> 16);
    break;
  case 20:
    lr[2][2] = static_cast<int16_t>(value);
    break;
  case 21:
    fc[0] = static_cast<int32_t>(value);
    break;
  case 22:
    fc[1] = static_cast<int32_t>(value);
    break;
  case 23:
    fc[2] = static_cast<int32_t>(value);
    break;
  case 24:
    ofx = static_cast<int32_t>(value);
    break;
  case 25:
    ofy = static_cast<int32_t>(value);
    break;
  case 26:
    h = static_cast<uint16_t>(value);
    break;
  case 27:
    dqa = static_cast<int16_t>(value);
    break;
  case 28:
    dqb = static_cast<int32_t>(value);
    break;
  case 29:
    zsf3 = static_cast<int16_t>(value);
    break;
  case 30:
    zsf4 = static_cast<int16_t>(value);
    break;
  case 31:
    // Flag register — bits 12-30 writable, bit 31 = error summary
    flag = value & 0x7FFFF000;
    if (flag & 0x7F87E000)
      flag |= (1u << 31);
    break;
  }
}

// ─── Command Execution ─────────────────────────────────────────────────

void GTE::Execute(uint32_t command) {
  static const bool gteDiagEnabled = IsPs1GteDiagEnabled();
  static uint64_t totalCommands = 0;
  static std::array<uint64_t, 64> opcodeCounts{};
  static std::array<std::array<std::array<uint64_t, 4>, 4>, 4> mvmvaCounts{};

  flag = 0; // Clear flags before each command

  uint32_t opcode = command & 0x3F;

  if (gteDiagEnabled) {
    totalCommands++;
    opcodeCounts[opcode]++;
    if (opcode == 0x12) {
      const uint32_t mx = (command >> 17) & 3;
      const uint32_t vx = (command >> 15) & 3;
      const uint32_t tx = (command >> 13) & 3;
      mvmvaCounts[mx][vx][tx]++;
    }
    if ((totalCommands % 1000) == 0) {
      AppendGteDiagSummary(totalCommands, opcodeCounts, mvmvaCounts);
    }
  }

  if constexpr (Trace::GTE_TRACE) {
    LogDebug("GTE command %02X (full: %08X)", opcode, command);
  }

  switch (opcode) {
  case 0x01:
    CmdRTPS(command);
    lastCommandCycles = 15;
    break;
  case 0x06:
    CmdNCLIP(command);
    lastCommandCycles = 8;
    break;
  case 0x0C:
    CmdOP(command);
    lastCommandCycles = 6;
    break;
  case 0x10:
    CmdDPCS(command);
    lastCommandCycles = 8;
    break;
  case 0x11:
    CmdINTPL(command);
    lastCommandCycles = 8;
    break;
  case 0x12:
    CmdMVMVA(command);
    lastCommandCycles = 8;
    break;
  case 0x13:
    CmdNCDS(command);
    lastCommandCycles = 19;
    break;
  case 0x14:
    CmdCDP(command);
    lastCommandCycles = 13;
    break;
  case 0x16:
    CmdNCDT(command);
    lastCommandCycles = 44;
    break;
  case 0x1B:
    CmdNCCS(command);
    lastCommandCycles = 17;
    break;
  case 0x1C:
    CmdCC(command);
    lastCommandCycles = 11;
    break;
  case 0x1E:
    CmdNCS(command);
    lastCommandCycles = 14;
    break;
  case 0x20:
    CmdNCT(command);
    lastCommandCycles = 30;
    break;
  case 0x28:
    CmdSQR(command);
    lastCommandCycles = 5;
    break;
  case 0x29:
    CmdDCPL(command);
    lastCommandCycles = 8;
    break;
  case 0x2A:
    CmdDPCT(command);
    lastCommandCycles = 17;
    break;
  case 0x2D:
    CmdAVSZ3(command);
    lastCommandCycles = 5;
    break;
  case 0x2E:
    CmdAVSZ4(command);
    lastCommandCycles = 6;
    break;
  case 0x30:
    CmdRTPT(command);
    lastCommandCycles = 23;
    break;
  case 0x3D:
    CmdGPF(command);
    lastCommandCycles = 5;
    break;
  case 0x3E:
    CmdGPL(command);
    lastCommandCycles = 5;
    break;
  case 0x3F:
    CmdNCCT(command);
    lastCommandCycles = 39;
    break;
  default:
    LogWarn("Unknown GTE command %02X", opcode);
    lastCommandCycles = 1;
    break;
  }

  // Set error summary flag
  if (flag & 0x7F87E000)
    flag |= (1u << 31);
}

// ─── GTE Commands ──────────────────────────────────────────────────────

void GTE::CmdRTPS(uint32_t cmd) {
  // Perspective Transformation Single (on V0)
  bool lmBit = (cmd >> 10) & 1;
  uint32_t sf = (cmd >> 19) & 1;
  uint32_t shift = sf * 12;

  // MAC1 = TRX*1000h + RT11*VX0 + RT12*VY0 + RT13*VZ0
  int64_t mac1 = static_cast<int64_t>(tr[0]) * 0x1000 + rt[0][0] * v[0][0] +
                 rt[0][1] * v[0][1] + rt[0][2] * v[0][2];
  int64_t mac2 = static_cast<int64_t>(tr[1]) * 0x1000 + rt[1][0] * v[0][0] +
                 rt[1][1] * v[0][1] + rt[1][2] * v[0][2];
  int64_t mac3 = static_cast<int64_t>(tr[2]) * 0x1000 + rt[2][0] * v[0][0] +
                 rt[2][1] * v[0][1] + rt[2][2] * v[0][2];

  mac[1] = CheckMAC(1, mac1);
  mac[2] = CheckMAC(2, mac2);
  mac[3] = CheckMAC(3, mac3);

  ir[1] = static_cast<int16_t>(CheckIR(1, mac[1] >> shift, lmBit));
  ir[2] = static_cast<int16_t>(CheckIR(2, mac[2] >> shift, lmBit));
  ir[3] = static_cast<int16_t>(CheckIR(3, mac[3] >> shift, lmBit));

  // SZ FIFO push
  PushSZ(
      static_cast<uint16_t>(std::clamp<int64_t>(mac[3] >> shift, 0, 0xFFFF)));

  // Perspective divide
  int64_t divResult = DivideUNR(static_cast<uint32_t>(h), sz[3]);

  // SXY FIFO push
  int32_t sx = static_cast<int32_t>(
      (static_cast<int64_t>(ir[1]) * divResult + ofx) >> 16);
  int32_t sy = static_cast<int32_t>(
      (static_cast<int64_t>(ir[2]) * divResult + ofy) >> 16);
  PushSXY(static_cast<int16_t>(Clamp(sx, -0x400, 0x3FF, 1 << 14)),
          static_cast<int16_t>(Clamp(sy, -0x400, 0x3FF, 1 << 13)));

  // Depth cueing
  int64_t mac0val =
      static_cast<int64_t>(dqb) + static_cast<int64_t>(dqa) * divResult;
  mac[0] = CheckMAC(0, mac0val);
  ir[0] = static_cast<int16_t>(CheckIR(0, mac[0] >> 12, false));
}

void GTE::CmdRTPT(uint32_t cmd) {
  // Perspective transform triple — apply RTPS to V0, V1, V2
  bool lmBit = (cmd >> 10) & 1;
  uint32_t sf = (cmd >> 19) & 1;
  uint32_t shift = sf * 12;

  for (int vi = 0; vi < 3; vi++) {
    int64_t mac1 = static_cast<int64_t>(tr[0]) * 0x1000 + rt[0][0] * v[vi][0] +
                   rt[0][1] * v[vi][1] + rt[0][2] * v[vi][2];
    int64_t mac2 = static_cast<int64_t>(tr[1]) * 0x1000 + rt[1][0] * v[vi][0] +
                   rt[1][1] * v[vi][1] + rt[1][2] * v[vi][2];
    int64_t mac3 = static_cast<int64_t>(tr[2]) * 0x1000 + rt[2][0] * v[vi][0] +
                   rt[2][1] * v[vi][1] + rt[2][2] * v[vi][2];

    mac[1] = CheckMAC(1, mac1);
    mac[2] = CheckMAC(2, mac2);
    mac[3] = CheckMAC(3, mac3);

    ir[1] = static_cast<int16_t>(CheckIR(1, mac[1] >> shift, lmBit));
    ir[2] = static_cast<int16_t>(CheckIR(2, mac[2] >> shift, lmBit));
    ir[3] = static_cast<int16_t>(CheckIR(3, mac[3] >> shift, lmBit));

    PushSZ(
        static_cast<uint16_t>(std::clamp<int64_t>(mac[3] >> shift, 0, 0xFFFF)));

    int64_t divResult = DivideUNR(static_cast<uint32_t>(h), sz[3]);

    int32_t sx = static_cast<int32_t>(
        (static_cast<int64_t>(ir[1]) * divResult + ofx) >> 16);
    int32_t sy = static_cast<int32_t>(
        (static_cast<int64_t>(ir[2]) * divResult + ofy) >> 16);
    PushSXY(static_cast<int16_t>(Clamp(sx, -0x400, 0x3FF, 1 << 14)),
            static_cast<int16_t>(Clamp(sy, -0x400, 0x3FF, 1 << 13)));

    if (vi == 2) {
      int64_t mac0val =
          static_cast<int64_t>(dqb) + static_cast<int64_t>(dqa) * divResult;
      mac[0] = CheckMAC(0, mac0val);
      ir[0] = static_cast<int16_t>(CheckIR(0, mac[0] >> 12, false));
    }
  }
}

void GTE::CmdNCLIP([[maybe_unused]] uint32_t cmd) {
  // Normal clipping: MAC0 = SX0*SY1 - SX1*SY0 + SX1*SY2 - SX2*SY1 + SX2*SY0 -
  // SX0*SY2
  int64_t result = static_cast<int64_t>(sxy[0][0]) * sxy[1][1] -
                   static_cast<int64_t>(sxy[1][0]) * sxy[0][1] +
                   static_cast<int64_t>(sxy[1][0]) * sxy[2][1] -
                   static_cast<int64_t>(sxy[2][0]) * sxy[1][1] +
                   static_cast<int64_t>(sxy[2][0]) * sxy[0][1] -
                   static_cast<int64_t>(sxy[0][0]) * sxy[2][1];

  mac[0] = CheckMAC(0, result);
}

void GTE::CmdAVSZ3([[maybe_unused]] uint32_t cmd) {
  // Average of SZ1, SZ2, SZ3
  int64_t result = static_cast<int64_t>(zsf3) * (sz[1] + sz[2] + sz[3]);
  mac[0] = CheckMAC(0, result);
  otz = static_cast<uint16_t>(std::clamp<int32_t>(mac[0] >> 12, 0, 0xFFFF));
}

void GTE::CmdAVSZ4([[maybe_unused]] uint32_t cmd) {
  // Average of SZ0, SZ1, SZ2, SZ3
  int64_t result = static_cast<int64_t>(zsf4) * (sz[0] + sz[1] + sz[2] + sz[3]);
  mac[0] = CheckMAC(0, result);
  otz = static_cast<uint16_t>(std::clamp<int32_t>(mac[0] >> 12, 0, 0xFFFF));
}

void GTE::CmdSQR(uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;
  uint32_t sf = (cmd >> 19) & 1;
  mac[1] = ir[1] * ir[1];
  mac[2] = ir[2] * ir[2];
  mac[3] = ir[3] * ir[3];
  ir[1] = static_cast<int16_t>(CheckIR(1, mac[1] >> (sf * 12), lmBit));
  ir[2] = static_cast<int16_t>(CheckIR(2, mac[2] >> (sf * 12), lmBit));
  ir[3] = static_cast<int16_t>(CheckIR(3, mac[3] >> (sf * 12), lmBit));
}

void GTE::CmdOP(uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;
  uint32_t sf = (cmd >> 19) & 1;
  // Outer product of D and IR
  mac[1] = static_cast<int32_t>(static_cast<int64_t>(rt[1][1]) * ir[3] -
                                static_cast<int64_t>(rt[2][2]) * ir[2]);
  mac[2] = static_cast<int32_t>(static_cast<int64_t>(rt[2][2]) * ir[1] -
                                static_cast<int64_t>(rt[0][0]) * ir[3]);
  mac[3] = static_cast<int32_t>(static_cast<int64_t>(rt[0][0]) * ir[2] -
                                static_cast<int64_t>(rt[1][1]) * ir[1]);
  ir[1] = static_cast<int16_t>(CheckIR(1, mac[1] >> (sf * 12), lmBit));
  ir[2] = static_cast<int16_t>(CheckIR(2, mac[2] >> (sf * 12), lmBit));
  ir[3] = static_cast<int16_t>(CheckIR(3, mac[3] >> (sf * 12), lmBit));
}

void GTE::CmdMVMVA(uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;
  uint32_t sf = (cmd >> 19) & 1;
  uint32_t mx = (cmd >> 17) & 3;
  uint32_t vx = (cmd >> 15) & 3;
  uint32_t tx = (cmd >> 13) & 3;

  // Select matrix
  const int16_t(*matrix)[3];
  switch (mx) {
  case 0:
    matrix = rt;
    break;
  case 1:
    matrix = l;
    break;
  case 2:
    matrix = lr;
    break;
  default:
    matrix = rt;
    break;
  }

  // Select vector
  int16_t vec[3];
  switch (vx) {
  case 0:
    vec[0] = v[0][0];
    vec[1] = v[0][1];
    vec[2] = v[0][2];
    break;
  case 1:
    vec[0] = v[1][0];
    vec[1] = v[1][1];
    vec[2] = v[1][2];
    break;
  case 2:
    vec[0] = v[2][0];
    vec[1] = v[2][1];
    vec[2] = v[2][2];
    break;
  case 3:
    vec[0] = ir[1];
    vec[1] = ir[2];
    vec[2] = ir[3];
    break;
  default:
    vec[0] = vec[1] = vec[2] = 0;
    break;
  }

  // Select translation vector
  int32_t tvec[3];
  switch (tx) {
  case 0:
    tvec[0] = tr[0];
    tvec[1] = tr[1];
    tvec[2] = tr[2];
    break;
  case 1:
    tvec[0] = bk[0];
    tvec[1] = bk[1];
    tvec[2] = bk[2];
    break;
  case 2:
    tvec[0] = fc[0];
    tvec[1] = fc[1];
    tvec[2] = fc[2];
    break;
  case 3:
    tvec[0] = tvec[1] = tvec[2] = 0;
    break;
  default:
    tvec[0] = tvec[1] = tvec[2] = 0;
    break;
  }

  for (int i = 0; i < 3; i++) {
    int64_t result = static_cast<int64_t>(tvec[i]) * 0x1000 +
                     matrix[i][0] * vec[0] + matrix[i][1] * vec[1] +
                     matrix[i][2] * vec[2];
    mac[i + 1] = CheckMAC(i + 1, result);
    ir[i + 1] =
        static_cast<int16_t>(CheckIR(i + 1, mac[i + 1] >> (sf * 12), lmBit));
  }
}

void GTE::CmdDPCS(uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;
  uint8_t r = rgbc & 0xFF;
  uint8_t g = (rgbc >> 8) & 0xFF;
  uint8_t b = (rgbc >> 16) & 0xFF;

  mac[1] = static_cast<int32_t>(r) << 16;
  mac[2] = static_cast<int32_t>(g) << 16;
  mac[3] = static_cast<int32_t>(b) << 16;

  // Interpolation towards far color
  for (int i = 0; i < 3; i++) {
    int64_t result = static_cast<int64_t>(fc[i]) * 0x1000 - mac[i + 1];
    result = mac[i + 1] + ir[0] * (result >> 12);
    mac[i + 1] = CheckMAC(i + 1, result);
    ir[i + 1] = static_cast<int16_t>(CheckIR(i + 1, mac[i + 1] >> 12, lmBit));
  }

  PushRGB(static_cast<uint8_t>(std::clamp<int32_t>(mac[1] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[2] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[3] >> 4, 0, 255)),
          (rgbc >> 24) & 0xFF);
}

void GTE::CmdDPCT([[maybe_unused]] uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;

  // Depth cue triple — reads from RGB FIFO front (rgb[0]), not rgbc
  for (int iter = 0; iter < 3; iter++) {
    uint8_t r = rgb[0] & 0xFF;
    uint8_t g = (rgb[0] >> 8) & 0xFF;
    uint8_t b = (rgb[0] >> 16) & 0xFF;

    mac[1] = static_cast<int32_t>(r) << 16;
    mac[2] = static_cast<int32_t>(g) << 16;
    mac[3] = static_cast<int32_t>(b) << 16;

    for (int i = 0; i < 3; i++) {
      int64_t result = static_cast<int64_t>(fc[i]) * 0x1000 - mac[i + 1];
      result = mac[i + 1] + ir[0] * (result >> 12);
      mac[i + 1] = CheckMAC(i + 1, result);
      ir[i + 1] = static_cast<int16_t>(CheckIR(i + 1, mac[i + 1] >> 12, lmBit));
    }

    PushRGB(static_cast<uint8_t>(std::clamp<int32_t>(mac[1] >> 4, 0, 255)),
            static_cast<uint8_t>(std::clamp<int32_t>(mac[2] >> 4, 0, 255)),
            static_cast<uint8_t>(std::clamp<int32_t>(mac[3] >> 4, 0, 255)),
            (rgbc >> 24) & 0xFF);
  }
}

void GTE::CmdINTPL(uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;
  for (int i = 0; i < 3; i++) {
    int64_t result = static_cast<int64_t>(fc[i]) * 0x1000 -
                     (static_cast<int64_t>(ir[i + 1]) << 12);
    result = (static_cast<int64_t>(ir[i + 1]) << 12) + ir[0] * (result >> 12);
    mac[i + 1] = CheckMAC(i + 1, result);
    ir[i + 1] = static_cast<int16_t>(CheckIR(i + 1, mac[i + 1] >> 12, lmBit));
  }

  PushRGB(static_cast<uint8_t>(std::clamp<int32_t>(mac[1] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[2] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[3] >> 4, 0, 255)),
          (rgbc >> 24) & 0xFF);
}

void GTE::NCSCore(int vIdx, uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;

  // Step 1: Light matrix × normal vector → light intensity
  for (int i = 0; i < 3; i++) {
    int64_t result = static_cast<int64_t>(l[i][0]) * v[vIdx][0] +
                     static_cast<int64_t>(l[i][1]) * v[vIdx][1] +
                     static_cast<int64_t>(l[i][2]) * v[vIdx][2];
    mac[i + 1] = CheckMAC(i + 1, result);
    ir[i + 1] = static_cast<int16_t>(CheckIR(i + 1, mac[i + 1] >> 12, lmBit));
  }

  // Step 2: Light color matrix × light vector + background color → lit color
  for (int i = 0; i < 3; i++) {
    int64_t result = static_cast<int64_t>(bk[i]) * 0x1000 +
                     static_cast<int64_t>(lr[i][0]) * ir[1] +
                     static_cast<int64_t>(lr[i][1]) * ir[2] +
                     static_cast<int64_t>(lr[i][2]) * ir[3];
    mac[i + 1] = CheckMAC(i + 1, result);
    ir[i + 1] = static_cast<int16_t>(CheckIR(i + 1, mac[i + 1] >> 12, lmBit));
  }
}

void GTE::CmdNCS(uint32_t cmd) {
  NCSCore(0, cmd);
  PushRGB(static_cast<uint8_t>(std::clamp<int32_t>(mac[1] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[2] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[3] >> 4, 0, 255)),
          (rgbc >> 24) & 0xFF);
}

void GTE::CmdNCT(uint32_t cmd) {
  for (int i = 0; i < 3; i++) {
    NCSCore(i, cmd);
    PushRGB(static_cast<uint8_t>(std::clamp<int32_t>(mac[1] >> 4, 0, 255)),
            static_cast<uint8_t>(std::clamp<int32_t>(mac[2] >> 4, 0, 255)),
            static_cast<uint8_t>(std::clamp<int32_t>(mac[3] >> 4, 0, 255)),
            (rgbc >> 24) & 0xFF);
  }
}

void GTE::CmdNCDS(uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;
  NCSCore(0, cmd);

  // Depth-cue: interpolate toward far color using IR0
  uint8_t colorR = rgbc & 0xFF;
  uint8_t colorG = (rgbc >> 8) & 0xFF;
  uint8_t colorB = (rgbc >> 16) & 0xFF;

  // Apply RGBC color modulation
  mac[1] = static_cast<int32_t>(
      CheckMAC(1, static_cast<int64_t>(colorR) * ir[1]) << 4);
  mac[2] = static_cast<int32_t>(
      CheckMAC(2, static_cast<int64_t>(colorG) * ir[2]) << 4);
  mac[3] = static_cast<int32_t>(
      CheckMAC(3, static_cast<int64_t>(colorB) * ir[3]) << 4);

  // Interpolate toward far color
  for (int i = 0; i < 3; i++) {
    int64_t result = static_cast<int64_t>(fc[i]) * 0x1000 - mac[i + 1];
    result = mac[i + 1] + ir[0] * (result >> 12);
    mac[i + 1] = CheckMAC(i + 1, result);
    ir[i + 1] = static_cast<int16_t>(CheckIR(i + 1, mac[i + 1] >> 12, lmBit));
  }

  PushRGB(static_cast<uint8_t>(std::clamp<int32_t>(mac[1] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[2] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[3] >> 4, 0, 255)),
          (rgbc >> 24) & 0xFF);
}

void GTE::CmdNCDT(uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;
  uint8_t colorR = rgbc & 0xFF;
  uint8_t colorG = (rgbc >> 8) & 0xFF;
  uint8_t colorB = (rgbc >> 16) & 0xFF;

  for (int vi = 0; vi < 3; vi++) {
    NCSCore(vi, cmd);

    mac[1] = static_cast<int32_t>(
        CheckMAC(1, static_cast<int64_t>(colorR) * ir[1]) << 4);
    mac[2] = static_cast<int32_t>(
        CheckMAC(2, static_cast<int64_t>(colorG) * ir[2]) << 4);
    mac[3] = static_cast<int32_t>(
        CheckMAC(3, static_cast<int64_t>(colorB) * ir[3]) << 4);

    for (int i = 0; i < 3; i++) {
      int64_t result = static_cast<int64_t>(fc[i]) * 0x1000 - mac[i + 1];
      result = mac[i + 1] + ir[0] * (result >> 12);
      mac[i + 1] = CheckMAC(i + 1, result);
      ir[i + 1] = static_cast<int16_t>(CheckIR(i + 1, mac[i + 1] >> 12, lmBit));
    }

    PushRGB(static_cast<uint8_t>(std::clamp<int32_t>(mac[1] >> 4, 0, 255)),
            static_cast<uint8_t>(std::clamp<int32_t>(mac[2] >> 4, 0, 255)),
            static_cast<uint8_t>(std::clamp<int32_t>(mac[3] >> 4, 0, 255)),
            (rgbc >> 24) & 0xFF);
  }
}

void GTE::CmdNCCS(uint32_t cmd) {
  NCSCore(0, cmd);

  // Multiply lit color by RGBC vertex color
  uint8_t colorR = rgbc & 0xFF;
  uint8_t colorG = (rgbc >> 8) & 0xFF;
  uint8_t colorB = (rgbc >> 16) & 0xFF;

  mac[1] = static_cast<int32_t>(
      CheckMAC(1, static_cast<int64_t>(colorR) * ir[1]) << 4);
  mac[2] = static_cast<int32_t>(
      CheckMAC(2, static_cast<int64_t>(colorG) * ir[2]) << 4);
  mac[3] = static_cast<int32_t>(
      CheckMAC(3, static_cast<int64_t>(colorB) * ir[3]) << 4);
  ir[1] = static_cast<int16_t>(CheckIR(1, mac[1] >> 12, (cmd >> 10) & 1));
  ir[2] = static_cast<int16_t>(CheckIR(2, mac[2] >> 12, (cmd >> 10) & 1));
  ir[3] = static_cast<int16_t>(CheckIR(3, mac[3] >> 12, (cmd >> 10) & 1));

  PushRGB(static_cast<uint8_t>(std::clamp<int32_t>(mac[1] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[2] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[3] >> 4, 0, 255)),
          (rgbc >> 24) & 0xFF);
}

void GTE::CmdNCCT(uint32_t cmd) {
  uint8_t colorR = rgbc & 0xFF;
  uint8_t colorG = (rgbc >> 8) & 0xFF;
  uint8_t colorB = (rgbc >> 16) & 0xFF;

  for (int vi = 0; vi < 3; vi++) {
    NCSCore(vi, cmd);

    mac[1] = static_cast<int32_t>(
        CheckMAC(1, static_cast<int64_t>(colorR) * ir[1]) << 4);
    mac[2] = static_cast<int32_t>(
        CheckMAC(2, static_cast<int64_t>(colorG) * ir[2]) << 4);
    mac[3] = static_cast<int32_t>(
        CheckMAC(3, static_cast<int64_t>(colorB) * ir[3]) << 4);
    ir[1] = static_cast<int16_t>(CheckIR(1, mac[1] >> 12, (cmd >> 10) & 1));
    ir[2] = static_cast<int16_t>(CheckIR(2, mac[2] >> 12, (cmd >> 10) & 1));
    ir[3] = static_cast<int16_t>(CheckIR(3, mac[3] >> 12, (cmd >> 10) & 1));

    PushRGB(static_cast<uint8_t>(std::clamp<int32_t>(mac[1] >> 4, 0, 255)),
            static_cast<uint8_t>(std::clamp<int32_t>(mac[2] >> 4, 0, 255)),
            static_cast<uint8_t>(std::clamp<int32_t>(mac[3] >> 4, 0, 255)),
            (rgbc >> 24) & 0xFF);
  }
}

void GTE::CmdCDP(uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;

  // IR already contains light vector — apply LR matrix + BK
  for (int i = 0; i < 3; i++) {
    int64_t result = static_cast<int64_t>(bk[i]) * 0x1000 +
                     static_cast<int64_t>(lr[i][0]) * ir[1] +
                     static_cast<int64_t>(lr[i][1]) * ir[2] +
                     static_cast<int64_t>(lr[i][2]) * ir[3];
    mac[i + 1] = CheckMAC(i + 1, result);
    ir[i + 1] = static_cast<int16_t>(CheckIR(i + 1, mac[i + 1] >> 12, lmBit));
  }

  // Multiply by RGBC, then depth-cue toward FC
  uint8_t colorR = rgbc & 0xFF;
  uint8_t colorG = (rgbc >> 8) & 0xFF;
  uint8_t colorB = (rgbc >> 16) & 0xFF;

  mac[1] = static_cast<int32_t>(
      CheckMAC(1, static_cast<int64_t>(colorR) * ir[1]) << 4);
  mac[2] = static_cast<int32_t>(
      CheckMAC(2, static_cast<int64_t>(colorG) * ir[2]) << 4);
  mac[3] = static_cast<int32_t>(
      CheckMAC(3, static_cast<int64_t>(colorB) * ir[3]) << 4);

  for (int i = 0; i < 3; i++) {
    int64_t result = static_cast<int64_t>(fc[i]) * 0x1000 - mac[i + 1];
    result = mac[i + 1] + ir[0] * (result >> 12);
    mac[i + 1] = CheckMAC(i + 1, result);
    ir[i + 1] = static_cast<int16_t>(CheckIR(i + 1, mac[i + 1] >> 12, lmBit));
  }

  PushRGB(static_cast<uint8_t>(std::clamp<int32_t>(mac[1] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[2] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[3] >> 4, 0, 255)),
          (rgbc >> 24) & 0xFF);
}

void GTE::CmdCC(uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;

  // IR already contains light vector — apply LR matrix + BK
  for (int i = 0; i < 3; i++) {
    int64_t result = static_cast<int64_t>(bk[i]) * 0x1000 +
                     static_cast<int64_t>(lr[i][0]) * ir[1] +
                     static_cast<int64_t>(lr[i][1]) * ir[2] +
                     static_cast<int64_t>(lr[i][2]) * ir[3];
    mac[i + 1] = CheckMAC(i + 1, result);
    ir[i + 1] = static_cast<int16_t>(CheckIR(i + 1, mac[i + 1] >> 12, lmBit));
  }

  // Multiply by RGBC vertex color
  uint8_t colorR = rgbc & 0xFF;
  uint8_t colorG = (rgbc >> 8) & 0xFF;
  uint8_t colorB = (rgbc >> 16) & 0xFF;

  mac[1] = static_cast<int32_t>(
      CheckMAC(1, static_cast<int64_t>(colorR) * ir[1]) << 4);
  mac[2] = static_cast<int32_t>(
      CheckMAC(2, static_cast<int64_t>(colorG) * ir[2]) << 4);
  mac[3] = static_cast<int32_t>(
      CheckMAC(3, static_cast<int64_t>(colorB) * ir[3]) << 4);
  ir[1] = static_cast<int16_t>(CheckIR(1, mac[1] >> 12, lmBit));
  ir[2] = static_cast<int16_t>(CheckIR(2, mac[2] >> 12, lmBit));
  ir[3] = static_cast<int16_t>(CheckIR(3, mac[3] >> 12, lmBit));

  PushRGB(static_cast<uint8_t>(std::clamp<int32_t>(mac[1] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[2] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[3] >> 4, 0, 255)),
          (rgbc >> 24) & 0xFF);
}

void GTE::CmdDCPL(uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;

  // IR already contains lit color — multiply by RGBC then depth-cue toward FC
  uint8_t colorR = rgbc & 0xFF;
  uint8_t colorG = (rgbc >> 8) & 0xFF;
  uint8_t colorB = (rgbc >> 16) & 0xFF;

  mac[1] = static_cast<int32_t>(
      CheckMAC(1, static_cast<int64_t>(colorR) * ir[1]) << 4);
  mac[2] = static_cast<int32_t>(
      CheckMAC(2, static_cast<int64_t>(colorG) * ir[2]) << 4);
  mac[3] = static_cast<int32_t>(
      CheckMAC(3, static_cast<int64_t>(colorB) * ir[3]) << 4);

  for (int i = 0; i < 3; i++) {
    int64_t result = static_cast<int64_t>(fc[i]) * 0x1000 - mac[i + 1];
    result = mac[i + 1] + ir[0] * (result >> 12);
    mac[i + 1] = CheckMAC(i + 1, result);
    ir[i + 1] = static_cast<int16_t>(CheckIR(i + 1, mac[i + 1] >> 12, lmBit));
  }

  PushRGB(static_cast<uint8_t>(std::clamp<int32_t>(mac[1] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[2] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[3] >> 4, 0, 255)),
          (rgbc >> 24) & 0xFF);
}

void GTE::CmdGPF(uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;
  uint32_t sf = (cmd >> 19) & 1;
  mac[1] = ir[0] * ir[1];
  mac[2] = ir[0] * ir[2];
  mac[3] = ir[0] * ir[3];
  ir[1] = static_cast<int16_t>(CheckIR(1, mac[1] >> (sf * 12), lmBit));
  ir[2] = static_cast<int16_t>(CheckIR(2, mac[2] >> (sf * 12), lmBit));
  ir[3] = static_cast<int16_t>(CheckIR(3, mac[3] >> (sf * 12), lmBit));
  PushRGB(static_cast<uint8_t>(std::clamp<int32_t>(mac[1] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[2] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[3] >> 4, 0, 255)),
          (rgbc >> 24) & 0xFF);
}

void GTE::CmdGPL(uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;
  uint32_t sf = (cmd >> 19) & 1;
  mac[1] = mac[1] + ir[0] * ir[1];
  mac[2] = mac[2] + ir[0] * ir[2];
  mac[3] = mac[3] + ir[0] * ir[3];
  ir[1] = static_cast<int16_t>(CheckIR(1, mac[1] >> (sf * 12), lmBit));
  ir[2] = static_cast<int16_t>(CheckIR(2, mac[2] >> (sf * 12), lmBit));
  ir[3] = static_cast<int16_t>(CheckIR(3, mac[3] >> (sf * 12), lmBit));
  PushRGB(static_cast<uint8_t>(std::clamp<int32_t>(mac[1] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[2] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[3] >> 4, 0, 255)),
          (rgbc >> 24) & 0xFF);
}

// ─── Math Helpers ──────────────────────────────────────────────────────

int64_t GTE::CheckMAC(int macIndex, int64_t value) {
  if (macIndex == 0) {
    // MAC0: 31-bit + sign
    if (value > 0x7FFFFFFF)
      SetFlag(1 << 16);
    if (value < -static_cast<int64_t>(0x80000000))
      SetFlag(1 << 15);
  } else {
    // MAC1-3: 43-bit + sign (bits 30/29/28 positive, 27/26/25 negative)
    if (value > 0x7FFFFFFFFFF)
      SetFlag(1 << (31 - macIndex));
    if (value < -static_cast<int64_t>(0x80000000000))
      SetFlag(1 << (28 - macIndex));
  }
  return value;
}

int32_t GTE::CheckIR(int irIndex, int64_t value, bool lmBit) {
  int32_t min = lmBit ? 0 : -0x8000;
  int32_t max = 0x7FFF;

  // IR0 uses bit 12; IR1/2/3 use bits 24/23/22
  uint32_t flagBit;
  if (irIndex == 0) {
    min = 0;
    max = 0x1000;
    flagBit = 1 << 12;
  } else {
    flagBit = 1 << (25 - irIndex);
  }

  if (value < min) {
    SetFlag(flagBit);
    return min;
  }
  if (value > max) {
    SetFlag(flagBit);
    return max;
  }
  return static_cast<int32_t>(value);
}

void GTE::PushSXY(int16_t x, int16_t y) {
  sxy[0][0] = sxy[1][0];
  sxy[0][1] = sxy[1][1];
  sxy[1][0] = sxy[2][0];
  sxy[1][1] = sxy[2][1];
  sxy[2][0] = x;
  sxy[2][1] = y;
}

void GTE::PushSZ(uint16_t z) {
  sz[0] = sz[1];
  sz[1] = sz[2];
  sz[2] = sz[3];
  sz[3] = z;
}

void GTE::PushRGB(uint8_t r, uint8_t g, uint8_t b, uint8_t c) {
  rgb[0] = rgb[1];
  rgb[1] = rgb[2];
  rgb[2] = r | (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(c) << 24);
}

// Hardware-accurate PS1 UNR (Unsigned Newton-Raphson) division table
static constexpr uint8_t unrTable[257] = {
    0xFF, 0xFD, 0xFB, 0xF9, 0xF7, 0xF5, 0xF3, 0xF1, 0xEF, 0xEE, 0xEC, 0xEA,
    0xE8, 0xE6, 0xE4, 0xE3, 0xE1, 0xDF, 0xDD, 0xDC, 0xDA, 0xD8, 0xD6, 0xD5,
    0xD3, 0xD1, 0xD0, 0xCE, 0xCD, 0xCB, 0xC9, 0xC8, 0xC6, 0xC5, 0xC3, 0xC1,
    0xC0, 0xBE, 0xBD, 0xBB, 0xBA, 0xB8, 0xB7, 0xB5, 0xB4, 0xB2, 0xB1, 0xAF,
    0xAE, 0xAC, 0xAB, 0xA9, 0xA8, 0xA7, 0xA5, 0xA4, 0xA2, 0xA1, 0xA0, 0x9E,
    0x9D, 0x9C, 0x9A, 0x99, 0x98, 0x96, 0x95, 0x94, 0x92, 0x91, 0x90, 0x8F,
    0x8D, 0x8C, 0x8B, 0x8A, 0x88, 0x87, 0x86, 0x85, 0x84, 0x82, 0x81, 0x80,
    0x7F, 0x7E, 0x7D, 0x7B, 0x7A, 0x79, 0x78, 0x77, 0x76, 0x75, 0x74, 0x73,
    0x71, 0x70, 0x6F, 0x6E, 0x6D, 0x6C, 0x6B, 0x6A, 0x69, 0x68, 0x67, 0x66,
    0x65, 0x64, 0x63, 0x62, 0x61, 0x60, 0x5F, 0x5E, 0x5D, 0x5D, 0x5C, 0x5B,
    0x5A, 0x59, 0x58, 0x57, 0x56, 0x55, 0x54, 0x54, 0x53, 0x52, 0x51, 0x50,
    0x4F, 0x4F, 0x4E, 0x4D, 0x4C, 0x4B, 0x4B, 0x4A, 0x49, 0x48, 0x47, 0x47,
    0x46, 0x45, 0x44, 0x44, 0x43, 0x42, 0x42, 0x41, 0x40, 0x3F, 0x3F, 0x3E,
    0x3D, 0x3D, 0x3C, 0x3B, 0x3B, 0x3A, 0x39, 0x39, 0x38, 0x37, 0x37, 0x36,
    0x35, 0x35, 0x34, 0x33, 0x33, 0x32, 0x32, 0x31, 0x30, 0x30, 0x2F, 0x2F,
    0x2E, 0x2D, 0x2D, 0x2C, 0x2C, 0x2B, 0x2B, 0x2A, 0x29, 0x29, 0x28, 0x28,
    0x27, 0x27, 0x26, 0x26, 0x25, 0x25, 0x24, 0x24, 0x23, 0x23, 0x22, 0x22,
    0x21, 0x21, 0x20, 0x20, 0x1F, 0x1F, 0x1E, 0x1E, 0x1D, 0x1D, 0x1C, 0x1C,
    0x1B, 0x1B, 0x1B, 0x1A, 0x1A, 0x19, 0x19, 0x18, 0x18, 0x18, 0x17, 0x17,
    0x16, 0x16, 0x16, 0x15, 0x15, 0x14, 0x14, 0x14, 0x13, 0x13, 0x12, 0x12,
    0x12, 0x11, 0x11, 0x11, 0x10, 0x10, 0x0F, 0x0F, 0x0F, 0x0E, 0x0E, 0x0E,
    0x0D, 0x0D, 0x0D, 0x0C, 0x0C,
};

int64_t GTE::DivideUNR(uint32_t dividend, uint16_t divisor) {
  if (divisor == 0) {
    SetFlag(1 << 17);
    return 0x1FFFF;
  }

  if (dividend >= static_cast<uint32_t>(divisor) * 2) {
    SetFlag(1 << 17);
    return 0x1FFFF;
  }

  // PS1 hardware UNR algorithm (NOCASH spec): CLZ normalization, table lookup,
  // Newton-Raphson reciprocal refinement
  int z = std::countl_zero(divisor);
  uint64_t n = static_cast<uint64_t>(dividend) << z;
  uint64_t d = static_cast<uint64_t>(divisor) << z;
  uint32_t u =
      unrTable[static_cast<uint32_t>((d - 0x7FC0) >> 7) & 0x1FF] + 0x101;
  d = (0x2000080 - d * u) >> 8;
  d = (0x80 + d * u) >> 8;

  int64_t result =
      std::min<int64_t>(0x1FFFF, static_cast<int64_t>((n * d + 0x8000) >> 16));
  return result;
}

void GTE::SetFlag(uint32_t bit) { flag |= bit; }

int32_t GTE::Clamp(int32_t value, int32_t min, int32_t max, uint32_t flagBit) {
  if (value < min) {
    SetFlag(flagBit);
    return min;
  }
  if (value > max) {
    SetFlag(flagBit);
    return max;
  }
  return value;
}

int32_t GTE::CountLeadingZeros(int32_t value) const {
  if (value == 0)
    return 32;
  uint32_t uval = (value >= 0) ? static_cast<uint32_t>(value)
                               : ~static_cast<uint32_t>(value);
  if (uval == 0)
    return 32;
  return static_cast<int32_t>(std::countl_zero(uval));
}

// ─── Debug ──────────────────────────────────────────────────────────────

void GTE::DumpState(std::ostream &os) const {
  os << "=== GTE State ===" << std::endl;
  os << "V0: (" << v[0][0] << "," << v[0][1] << "," << v[0][2] << ")"
     << std::endl;
  os << "V1: (" << v[1][0] << "," << v[1][1] << "," << v[1][2] << ")"
     << std::endl;
  os << "V2: (" << v[2][0] << "," << v[2][1] << "," << v[2][2] << ")"
     << std::endl;
  os << "IR: " << ir[0] << " " << ir[1] << " " << ir[2] << " " << ir[3]
     << std::endl;
  os << "MAC: " << mac[0] << " " << mac[1] << " " << mac[2] << " " << mac[3]
     << std::endl;
  os << "SXY2: (" << sxy[2][0] << "," << sxy[2][1] << ") SZ3: " << sz[3]
     << std::endl;
  os << "Flag: " << std::hex << flag << std::endl;
}

std::string GTE::GetDebugSummary() const {
  std::ostringstream os;
  os << "GTE flag=" << std::hex << flag;
  return os.str();
}

} // namespace AIO::Emulator::PS1
