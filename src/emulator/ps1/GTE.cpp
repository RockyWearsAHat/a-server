#include "emulator/ps1/GTE.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace AIO::Emulator::PS1 {

GTE::GTE() : Loggable("PS1.GTE") {}

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
  flag = 0; // Clear flags before each command

  uint32_t opcode = command & 0x3F;

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

  // MAC1 = TRX*1000h + RT11*VX0 + RT12*VY0 + RT13*VZ0
  int64_t mac1 = static_cast<int64_t>(tr[0]) * 0x1000 + rt[0][0] * v[0][0] +
                 rt[0][1] * v[0][1] + rt[0][2] * v[0][2];
  int64_t mac2 = static_cast<int64_t>(tr[1]) * 0x1000 + rt[1][0] * v[0][0] +
                 rt[1][1] * v[0][1] + rt[1][2] * v[0][2];
  int64_t mac3 = static_cast<int64_t>(tr[2]) * 0x1000 + rt[2][0] * v[0][0] +
                 rt[2][1] * v[0][1] + rt[2][2] * v[0][2];

  mac[1] = static_cast<int32_t>(CheckMAC(1, mac1));
  mac[2] = static_cast<int32_t>(CheckMAC(2, mac2));
  mac[3] = static_cast<int32_t>(CheckMAC(3, mac3));

  ir[1] = static_cast<int16_t>(CheckIR(1, mac[1] >> 12, lmBit));
  ir[2] = static_cast<int16_t>(CheckIR(2, mac[2] >> 12, lmBit));
  ir[3] = static_cast<int16_t>(CheckIR(3, mac[3] >> 12, lmBit));

  // SZ FIFO push
  PushSZ(static_cast<uint16_t>(std::clamp<int32_t>(mac[3] >> 12, 0, 0xFFFF)));

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
  mac[0] = static_cast<int32_t>(CheckMAC(0, mac0val));
  ir[0] = static_cast<int16_t>(std::clamp<int32_t>(mac[0] >> 12, 0, 0x1000));
}

void GTE::CmdRTPT(uint32_t cmd) {
  // Perspective transform triple — apply RTPS to V0, V1, V2
  bool lmBit = (cmd >> 10) & 1;

  for (int vi = 0; vi < 3; vi++) {
    int64_t mac1 = static_cast<int64_t>(tr[0]) * 0x1000 + rt[0][0] * v[vi][0] +
                   rt[0][1] * v[vi][1] + rt[0][2] * v[vi][2];
    int64_t mac2 = static_cast<int64_t>(tr[1]) * 0x1000 + rt[1][0] * v[vi][0] +
                   rt[1][1] * v[vi][1] + rt[1][2] * v[vi][2];
    int64_t mac3 = static_cast<int64_t>(tr[2]) * 0x1000 + rt[2][0] * v[vi][0] +
                   rt[2][1] * v[vi][1] + rt[2][2] * v[vi][2];

    mac[1] = static_cast<int32_t>(CheckMAC(1, mac1));
    mac[2] = static_cast<int32_t>(CheckMAC(2, mac2));
    mac[3] = static_cast<int32_t>(CheckMAC(3, mac3));

    ir[1] = static_cast<int16_t>(CheckIR(1, mac[1] >> 12, lmBit));
    ir[2] = static_cast<int16_t>(CheckIR(2, mac[2] >> 12, lmBit));
    ir[3] = static_cast<int16_t>(CheckIR(3, mac[3] >> 12, lmBit));

    PushSZ(static_cast<uint16_t>(std::clamp<int32_t>(mac[3] >> 12, 0, 0xFFFF)));

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
      mac[0] = static_cast<int32_t>(CheckMAC(0, mac0val));
      ir[0] =
          static_cast<int16_t>(std::clamp<int32_t>(mac[0] >> 12, 0, 0x1000));
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

  mac[0] = static_cast<int32_t>(CheckMAC(0, result));
}

void GTE::CmdAVSZ3([[maybe_unused]] uint32_t cmd) {
  // Average of SZ1, SZ2, SZ3
  int64_t result = static_cast<int64_t>(zsf3) * (sz[1] + sz[2] + sz[3]);
  mac[0] = static_cast<int32_t>(CheckMAC(0, result));
  otz = static_cast<uint16_t>(std::clamp<int32_t>(mac[0] >> 12, 0, 0xFFFF));
}

void GTE::CmdAVSZ4([[maybe_unused]] uint32_t cmd) {
  // Average of SZ0, SZ1, SZ2, SZ3
  int64_t result = static_cast<int64_t>(zsf4) * (sz[0] + sz[1] + sz[2] + sz[3]);
  mac[0] = static_cast<int32_t>(CheckMAC(0, result));
  otz = static_cast<uint16_t>(std::clamp<int32_t>(mac[0] >> 12, 0, 0xFFFF));
}

void GTE::CmdSQR(uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;
  mac[1] = ir[1] * ir[1];
  mac[2] = ir[2] * ir[2];
  mac[3] = ir[3] * ir[3];
  ir[1] = static_cast<int16_t>(CheckIR(1, mac[1] >> 12, lmBit));
  ir[2] = static_cast<int16_t>(CheckIR(2, mac[2] >> 12, lmBit));
  ir[3] = static_cast<int16_t>(CheckIR(3, mac[3] >> 12, lmBit));
}

void GTE::CmdOP(uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;
  // Outer product of D and IR
  mac[1] = static_cast<int32_t>(static_cast<int64_t>(rt[1][1]) * ir[3] -
                                static_cast<int64_t>(rt[2][2]) * ir[2]);
  mac[2] = static_cast<int32_t>(static_cast<int64_t>(rt[2][2]) * ir[1] -
                                static_cast<int64_t>(rt[0][0]) * ir[3]);
  mac[3] = static_cast<int32_t>(static_cast<int64_t>(rt[0][0]) * ir[2] -
                                static_cast<int64_t>(rt[1][1]) * ir[1]);
  ir[1] = static_cast<int16_t>(CheckIR(1, mac[1] >> 12, lmBit));
  ir[2] = static_cast<int16_t>(CheckIR(2, mac[2] >> 12, lmBit));
  ir[3] = static_cast<int16_t>(CheckIR(3, mac[3] >> 12, lmBit));
}

void GTE::CmdMVMVA(uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;
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
    mac[i + 1] = static_cast<int32_t>(CheckMAC(i + 1, result));
    ir[i + 1] = static_cast<int16_t>(CheckIR(i + 1, mac[i + 1] >> 12, lmBit));
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
    mac[i + 1] = static_cast<int32_t>(CheckMAC(i + 1, result));
    ir[i + 1] = static_cast<int16_t>(CheckIR(i + 1, mac[i + 1] >> 12, lmBit));
  }

  PushRGB(static_cast<uint8_t>(std::clamp<int32_t>(mac[1] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[2] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[3] >> 4, 0, 255)),
          (rgbc >> 24) & 0xFF);
}

void GTE::CmdDPCT([[maybe_unused]] uint32_t cmd) {
  // Depth cue triple — apply DPCS three times using RGB FIFO
  for (int i = 0; i < 3; i++) {
    CmdDPCS(cmd);
  }
}

void GTE::CmdINTPL(uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;
  for (int i = 0; i < 3; i++) {
    int64_t result = static_cast<int64_t>(fc[i]) * 0x1000 -
                     (static_cast<int64_t>(ir[i + 1]) << 12);
    result = (static_cast<int64_t>(ir[i + 1]) << 12) + ir[0] * (result >> 12);
    mac[i + 1] = static_cast<int32_t>(CheckMAC(i + 1, result));
    ir[i + 1] = static_cast<int16_t>(CheckIR(i + 1, mac[i + 1] >> 12, lmBit));
  }

  PushRGB(static_cast<uint8_t>(std::clamp<int32_t>(mac[1] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[2] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[3] >> 4, 0, 255)),
          (rgbc >> 24) & 0xFF);
}

void GTE::CmdNCS([[maybe_unused]] uint32_t cmd) {
  // Normal Color Single on V0
  // Simplified: just output the RGBC color
  PushRGB(rgbc & 0xFF, (rgbc >> 8) & 0xFF, (rgbc >> 16) & 0xFF,
          (rgbc >> 24) & 0xFF);
}

void GTE::CmdNCT([[maybe_unused]] uint32_t cmd) {
  // Normal Color Triple
  for (int i = 0; i < 3; i++)
    CmdNCS(cmd);
}

void GTE::CmdNCDS([[maybe_unused]] uint32_t cmd) {
  CmdNCS(cmd); // Simplified
}

void GTE::CmdNCDT([[maybe_unused]] uint32_t cmd) {
  for (int i = 0; i < 3; i++)
    CmdNCDS(cmd);
}

void GTE::CmdNCCS([[maybe_unused]] uint32_t cmd) {
  CmdNCS(cmd); // Simplified
}

void GTE::CmdNCCT([[maybe_unused]] uint32_t cmd) {
  for (int i = 0; i < 3; i++)
    CmdNCCS(cmd);
}

void GTE::CmdCDP([[maybe_unused]] uint32_t cmd) {
  CmdDPCS(cmd); // Simplified
}

void GTE::CmdCC([[maybe_unused]] uint32_t cmd) {
  PushRGB(rgbc & 0xFF, (rgbc >> 8) & 0xFF, (rgbc >> 16) & 0xFF,
          (rgbc >> 24) & 0xFF);
}

void GTE::CmdDCPL(uint32_t cmd) {
  CmdDPCS(cmd); // Simplified
}

void GTE::CmdGPF(uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;
  mac[1] = ir[0] * ir[1];
  mac[2] = ir[0] * ir[2];
  mac[3] = ir[0] * ir[3];
  ir[1] = static_cast<int16_t>(CheckIR(1, mac[1] >> 12, lmBit));
  ir[2] = static_cast<int16_t>(CheckIR(2, mac[2] >> 12, lmBit));
  ir[3] = static_cast<int16_t>(CheckIR(3, mac[3] >> 12, lmBit));
  PushRGB(static_cast<uint8_t>(std::clamp<int32_t>(mac[1] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[2] >> 4, 0, 255)),
          static_cast<uint8_t>(std::clamp<int32_t>(mac[3] >> 4, 0, 255)),
          (rgbc >> 24) & 0xFF);
}

void GTE::CmdGPL(uint32_t cmd) {
  bool lmBit = (cmd >> 10) & 1;
  mac[1] = mac[1] + ir[0] * ir[1];
  mac[2] = mac[2] + ir[0] * ir[2];
  mac[3] = mac[3] + ir[0] * ir[3];
  ir[1] = static_cast<int16_t>(CheckIR(1, mac[1] >> 12, lmBit));
  ir[2] = static_cast<int16_t>(CheckIR(2, mac[2] >> 12, lmBit));
  ir[3] = static_cast<int16_t>(CheckIR(3, mac[3] >> 12, lmBit));
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
    // MAC1-3: 43-bit + sign
    if (value > 0x7FFFFFFFFFF)
      SetFlag(1 << (30 - macIndex));
    if (value < -static_cast<int64_t>(0x80000000000))
      SetFlag(1 << (27 - macIndex));
  }
  return value;
}

int32_t GTE::CheckIR(int irIndex, int64_t value, bool lmBit) {
  int32_t min = lmBit ? 0 : -0x8000;
  int32_t max = 0x7FFF;

  if (irIndex == 0) {
    min = 0;
    max = 0x1000;
  }

  if (value < min) {
    SetFlag(1 << (24 - irIndex));
    return min;
  }
  if (value > max) {
    SetFlag(1 << (24 - irIndex));
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

int64_t GTE::DivideUNR(uint32_t dividend, uint16_t divisor) {
  if (divisor == 0) {
    SetFlag(1 << 17); // Division overflow
    return 0x1FFFF;
  }

  if (static_cast<uint32_t>(dividend) * 2 > static_cast<uint32_t>(divisor)) {
    // UNR division algorithm (unrolled Newton-Raphson)
    // Using a lookup table approximation
    uint32_t shift = 0;
    uint32_t d = divisor;
    while (d < 0x8000) {
      d <<= 1;
      shift++;
    }

    int64_t result = (static_cast<int64_t>(dividend) << (16 + shift)) / divisor;
    if (result > 0x1FFFF) {
      SetFlag(1 << 17);
      return 0x1FFFF;
    }
    return result;
  }

  return (static_cast<int64_t>(dividend) << 16) / divisor;
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
