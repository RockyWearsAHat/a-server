#pragma once

#include "PS1Constants.h"
#include "emulator/common/Loggable.h"
#include <array>
#include <cstdint>
#include <ostream>
#include <string>

namespace AIO::Emulator::PS1 {

// GTE (Geometry Transformation Engine) — COP2
// Accessed via MTC2/MFC2/CTC2/CFC2 and COP2 command instructions
class GTE : public Common::Loggable {
public:
  GTE();
  ~GTE() = default;

  void Reset();

  // ─── Register Access ────────────────────────────────────────────────
  uint32_t ReadData(uint32_t reg) const;        // MFC2: data registers 0-31
  void WriteData(uint32_t reg, uint32_t value); // MTC2

  uint32_t ReadControl(uint32_t reg) const; // CFC2: control registers 0-31
  void WriteControl(uint32_t reg, uint32_t value); // CTC2

  // ─── Command Execution ──────────────────────────────────────────────
  void Execute(uint32_t command); // COP2 instruction
  int GetLastCommandCycles() const { return lastCommandCycles; }

  // ─── Debug ──────────────────────────────────────────────────────────
  void DumpState(std::ostream &os) const;
  std::string GetDebugSummary() const;

private:
  // ─── Data Registers (32 × 32-bit) ───────────────────────────────────
  // Stored as individual fields for clarity and accuracy
  int16_t v[3][3] = {};   // V0-V2 vectors (X,Y,Z each signed 16-bit)
  uint32_t rgbc = 0;      // Color + code (4 × 8-bit)
  uint16_t otz = 0;       // Average Z
  int16_t ir[4] = {};     // IR0-IR3 (signed 16-bit)
  int16_t sxy[4][2] = {}; // SXY0-3 FIFO (X,Y signed 16-bit)
  uint16_t sz[4] = {};    // SZ0-3 FIFO (unsigned 16-bit)
  uint32_t rgb[3] = {};   // RGB0-2 FIFO
  int32_t mac[4] = {};    // MAC0-3 (32-bit, but internal calcs use 44-bit)
  uint32_t irgb = 0;      // Color conversion input
  uint32_t orgb = 0;      // Color conversion output
  int32_t lzcs = 0;       // Leading zero count source
  int32_t lzcr = 0;       // Leading zero count result

  // ─── Control Registers (32 × 32-bit) ────────────────────────────────
  int16_t rt[3][3] = {}; // Rotation matrix 3×3
  int32_t tr[3] = {};    // Translation vector
  int16_t l[3][3] = {};  // Light source matrix 3×3
  int32_t bk[3] = {};    // Background color (R,G,B)
  int16_t lr[3][3] = {}; // Light color matrix 3×3
  int32_t fc[3] = {};    // Far color (R,G,B)
  int32_t ofx = 0;       // Screen offset X (16.16 fixed)
  int32_t ofy = 0;       // Screen offset Y (16.16 fixed)
  uint16_t h = 0;        // Projection plane distance
  int16_t dqa = 0;       // Depth cue coefficient
  int32_t dqb = 0;       // Depth cue offset
  int16_t zsf3 = 0;      // Z scale factor 3
  int16_t zsf4 = 0;      // Z scale factor 4
  uint32_t flag = 0;     // Overflow flags

  int lastCommandCycles = 0;

  // ─── GTE Commands ───────────────────────────────────────────────────
  void CmdRTPS(uint32_t cmd);  // Perspective transform single
  void CmdRTPT(uint32_t cmd);  // Perspective transform triple
  void CmdMVMVA(uint32_t cmd); // Matrix × vector + add
  void CmdNCLIP(uint32_t cmd); // Normal clipping
  void CmdAVSZ3(uint32_t cmd); // Average Z (3 values)
  void CmdAVSZ4(uint32_t cmd); // Average Z (4 values)
  void CmdSQR(uint32_t cmd);   // Square
  void CmdOP(uint32_t cmd);    // Outer product
  void CmdDPCS(uint32_t cmd);  // Depth cue single
  void CmdDPCT(uint32_t cmd);  // Depth cue triple
  void CmdDCPL(uint32_t cmd);  // Depth cue (light)
  void CmdINTPL(uint32_t cmd); // Interpolation
  void CmdNCS(uint32_t cmd);   // Normal color single
  void CmdNCT(uint32_t cmd);   // Normal color triple
  void CmdNCDS(uint32_t cmd);  // Normal color depth single
  void CmdNCDT(uint32_t cmd);  // Normal color depth triple
  void CmdNCCS(uint32_t cmd);  // Normal color color single
  void CmdNCCT(uint32_t cmd);  // Normal color color triple
  void CmdCDP(uint32_t cmd);   // Color depth cue
  void CmdCC(uint32_t cmd);    // Color color
  void CmdGPF(uint32_t cmd);   // General purpose interpolation
  void CmdGPL(uint32_t cmd);   // General purpose interpolation + accumulate

  // Shared lighting core: L×V → LR×light+BK
  void NCSCore(int vIdx, uint32_t cmd);

  // ─── Math Helpers ───────────────────────────────────────────────────
  int64_t CheckMAC(int macIndex, int64_t value);
  int32_t CheckIR(int irIndex, int64_t value, bool lmBit);
  void PushSXY(int16_t x, int16_t y);
  void PushSZ(uint16_t z);
  void PushRGB(uint8_t r, uint8_t g, uint8_t b, uint8_t c);
  int64_t DivideUNR(uint32_t dividend, uint16_t divisor);
  void SetFlag(uint32_t bit);
  int32_t Clamp(int32_t value, int32_t min, int32_t max, uint32_t flagBit);
  int32_t CountLeadingZeros(int32_t value) const;
};

} // namespace AIO::Emulator::PS1
