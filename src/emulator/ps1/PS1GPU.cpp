#include "emulator/ps1/PS1GPU.h"
#include "emulator/ps1/PS1Memory.h"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace AIO::Emulator::PS1 {

PS1GPU::PS1GPU(PS1Memory &memory)
    : Loggable("PS1.GPU"), memory(memory), vram(GPU::VRAM_SIZE_PIXELS, 0) {}

void PS1GPU::Reset() {
  std::fill(vram.begin(), vram.end(), 0);
  gp0CommandBuffer.clear();
  gp0WordsRemaining = 0;
  gp0CommandCount = 0;
  gp0Mode = GP0Mode::Command;

  texPageBaseX = 0;
  texPageBaseY = 0;
  semiTransparencyMode = 0;
  texPageColorDepth = 0;
  dither = false;
  drawToDisplay = false;
  maskBitSet = false;
  maskBitCheck = false;
  interlaceField = false;
  reverseFlag = false;
  textureDisable = false;
  hRes = 0;
  vRes480 = false;
  palMode = false;
  colorDepth24 = false;
  interlace = false;
  displayDisabled = true;
  irq1 = false;
  dmaDirection = 0;

  drawAreaLeft = 0;
  drawAreaTop = 0;
  drawAreaRight = 0;
  drawAreaBottom = 0;
  drawOffsetX = 0;
  drawOffsetY = 0;

  displayVRAMStartX = 0;
  displayVRAMStartY = 0;
  displayHorizStart = 0x200;
  displayHorizEnd = 0xC00;
  displayVertStart = 0x10;
  displayVertEnd = 0x100;

  texWindowMaskX = 0;
  texWindowMaskY = 0;
  texWindowOffsetX = 0;
  texWindowOffsetY = 0;

  currentScanline = 0;
  dotCounter = 0;
  vblank = false;
  gpuReadBuffer = 0;
}

// ─── GPUSTAT ────────────────────────────────────────────────────────────

uint32_t PS1GPU::ReadGPUSTAT() const {
  uint32_t stat = 0;

  stat |= texPageBaseX;
  stat |= static_cast<uint32_t>(texPageBaseY) << 4;
  stat |= static_cast<uint32_t>(semiTransparencyMode) << 5;
  stat |= static_cast<uint32_t>(texPageColorDepth) << 7;
  stat |= static_cast<uint32_t>(dither) << 9;
  stat |= static_cast<uint32_t>(drawToDisplay) << 10;
  stat |= static_cast<uint32_t>(maskBitSet) << 11;
  stat |= static_cast<uint32_t>(maskBitCheck) << 12;
  stat |= static_cast<uint32_t>(interlaceField) << 13;
  stat |= static_cast<uint32_t>(reverseFlag) << 14;
  stat |= static_cast<uint32_t>(textureDisable) << 15;
  stat |= static_cast<uint32_t>(hRes) << 16;
  // Bit 17 and 18 handle horizontal resolution
  stat |= static_cast<uint32_t>(vRes480) << 19;
  stat |= static_cast<uint32_t>(palMode) << 20;
  stat |= static_cast<uint32_t>(colorDepth24) << 21;
  stat |= static_cast<uint32_t>(interlace) << 22;
  stat |= static_cast<uint32_t>(displayDisabled) << 23;
  stat |= static_cast<uint32_t>(irq1) << 24;

  // DMA / Data request bits
  stat |= 1 << 26; // Ready to receive DMA block
  stat |= 1 << 27; // Ready to send VRAM to CPU
  stat |= 1 << 28; // Ready to receive command word

  stat |= static_cast<uint32_t>(dmaDirection) << 29;

  // Bit 31: even/odd scanline (interlace)
  stat |= static_cast<uint32_t>(currentScanline & 1) << 31;

  return stat;
}

uint32_t PS1GPU::ReadGPUREAD() { return gpuReadBuffer; }

// ─── GP0 Commands ───────────────────────────────────────────────────────

void PS1GPU::WriteGP0(uint32_t value) {
  if constexpr (Trace::GPU_CMD) {
    LogDebug("GP0 write: %08X (mode=%d)", value, static_cast<int>(gp0Mode));
  }

  if (gp0Mode == GP0Mode::CopyToVRAM) {
    // Receiving pixel data for VRAM copy
    uint16_t pixel1 = static_cast<uint16_t>(value);
    uint16_t pixel2 = static_cast<uint16_t>(value >> 16);

    WriteVRAM(vramTransferCurrX, vramTransferCurrY, pixel1);
    vramTransferCurrX++;
    if (vramTransferCurrX >= vramTransferX + vramTransferW) {
      vramTransferCurrX = vramTransferX;
      vramTransferCurrY++;
    }

    WriteVRAM(vramTransferCurrX, vramTransferCurrY, pixel2);
    vramTransferCurrX++;
    if (vramTransferCurrX >= vramTransferX + vramTransferW) {
      vramTransferCurrX = vramTransferX;
      vramTransferCurrY++;
    }

    if (vramTransferCurrY >= vramTransferY + vramTransferH) {
      gp0Mode = GP0Mode::Command;
    }
    return;
  }

  gp0CommandBuffer.push_back(value);

  if (gp0WordsRemaining == 0) {
    // First word of a new command
    uint8_t cmd = static_cast<uint8_t>(value >> 24);
    uint32_t length = GP0CommandLength(cmd);
    if (length == 1) {
      ProcessGP0Command();
      gp0CommandBuffer.clear();
    } else {
      gp0WordsRemaining = length - 1;
    }
  } else {
    gp0WordsRemaining--;
    if (gp0WordsRemaining == 0) {
      ProcessGP0Command();
      gp0CommandBuffer.clear();
    }
  }
}

uint32_t PS1GPU::GP0CommandLength(uint8_t cmd) const {
  switch (cmd) {
  case 0x00:
    return 1; // NOP
  case 0x01:
    return 1; // Clear cache
  case 0x02:
    return 3; // Fill rectangle
  case 0x20:
  case 0x22:
    return 4; // Monochrome triangle
  case 0x24:
  case 0x25:
  case 0x26:
  case 0x27:
    return 7; // Textured triangle
  case 0x28:
  case 0x2A:
    return 5; // Monochrome quad
  case 0x2C:
  case 0x2D:
  case 0x2E:
  case 0x2F:
    return 9; // Textured quad
  case 0x30:
  case 0x32:
    return 6; // Shaded triangle
  case 0x34:
  case 0x36:
    return 9; // Shaded textured triangle
  case 0x38:
  case 0x3A:
    return 8; // Shaded quad
  case 0x3C:
  case 0x3E:
    return 12; // Shaded textured quad
  case 0x40:
  case 0x42:
    return 3; // Monochrome line
  case 0x50:
  case 0x52:
    return 4; // Shaded line
  case 0x60:
  case 0x62:
    return 3; // Monochrome rectangle (variable)
  case 0x64:
  case 0x65:
  case 0x66:
  case 0x67:
    return 4; // Textured rectangle (variable)
  case 0x68:
  case 0x6A:
    return 2; // Monochrome 1×1
  case 0x70:
  case 0x72:
    return 2; // Monochrome 8×8
  case 0x74:
  case 0x75:
  case 0x76:
  case 0x77:
    return 3; // Textured 8×8
  case 0x78:
  case 0x7A:
    return 2; // Monochrome 16×16
  case 0x7C:
  case 0x7D:
  case 0x7E:
  case 0x7F:
    return 3; // Textured 16×16
  case 0x80:
    return 4; // VRAM-to-VRAM copy
  case 0xA0:
    return 3; // CPU-to-VRAM copy (header; pixel data follows)
  case 0xC0:
    return 3; // VRAM-to-CPU copy
  case 0xE1:
    return 1; // Draw mode
  case 0xE2:
    return 1; // Texture window
  case 0xE3:
    return 1; // Drawing area top-left
  case 0xE4:
    return 1; // Drawing area bottom-right
  case 0xE5:
    return 1; // Drawing offset
  case 0xE6:
    return 1; // Mask bit setting
  default:
    return 1;
  }
}

void PS1GPU::ProcessGP0Command() {
  uint8_t cmd = static_cast<uint8_t>(gp0CommandBuffer[0] >> 24);
  gp0CommandCount++;

  switch (cmd) {
  case 0x00:
    GP0_Nop(gp0CommandBuffer[0]);
    break;
  case 0x01:
    GP0_ClearCache(gp0CommandBuffer[0]);
    break;
  case 0x02:
    GP0_FillRect(gp0CommandBuffer);
    break;
  case 0x20:
    GP0_MonoTriangle(gp0CommandBuffer, true);
    break;
  case 0x22:
    GP0_MonoTriangle(gp0CommandBuffer, false);
    break;
  case 0x28:
    GP0_MonoQuad(gp0CommandBuffer, true);
    break;
  case 0x2A:
    GP0_MonoQuad(gp0CommandBuffer, false);
    break;
  case 0x24:
  case 0x25:
  case 0x26:
  case 0x27:
    GP0_TexturedTriangle(gp0CommandBuffer, (cmd & 1) == 0);
    break;
  case 0x2C:
  case 0x2D:
  case 0x2E:
  case 0x2F:
    GP0_TexturedQuad(gp0CommandBuffer, (cmd & 1) == 0);
    break;
  case 0x30:
  case 0x32:
    GP0_ShadedTriangle(gp0CommandBuffer, (cmd & 1) == 0);
    break;
  case 0x38:
  case 0x3A:
    GP0_ShadedQuad(gp0CommandBuffer, (cmd & 1) == 0);
    break;
  case 0x40:
  case 0x42:
    GP0_MonoLine(gp0CommandBuffer);
    break;
  case 0x60:
  case 0x62:
    GP0_MonoRect(gp0CommandBuffer);
    break;
  case 0x64:
  case 0x65:
  case 0x66:
  case 0x67:
    GP0_TexturedRect(gp0CommandBuffer);
    break;
  case 0x68:
  case 0x6A:
    GP0_MonoDot(gp0CommandBuffer);
    break;
  case 0x70:
  case 0x72:
    GP0_MonoRect(gp0CommandBuffer);
    break;
  case 0x78:
  case 0x7A:
    GP0_MonoRect(gp0CommandBuffer);
    break;
  case 0x80:
    GP0_CopyRectVRAMtoVRAM(gp0CommandBuffer);
    break;
  case 0xA0:
    GP0_CopyRectCPUtoVRAM(gp0CommandBuffer);
    break;
  case 0xC0:
    GP0_CopyRectVRAMtoCPU(gp0CommandBuffer);
    break;
  case 0xE1:
    GP0_DrawMode(gp0CommandBuffer[0]);
    break;
  case 0xE2:
    GP0_TextureWindow(gp0CommandBuffer[0]);
    break;
  case 0xE3:
    GP0_DrawAreaTopLeft(gp0CommandBuffer[0]);
    break;
  case 0xE4:
    GP0_DrawAreaBottomRight(gp0CommandBuffer[0]);
    break;
  case 0xE5:
    GP0_DrawOffset(gp0CommandBuffer[0]);
    break;
  case 0xE6:
    GP0_MaskBitSetting(gp0CommandBuffer[0]);
    break;
  default:
    if constexpr (Trace::GPU_CMD) {
      LogWarn("Unhandled GP0 cmd %02X", cmd);
    }
    break;
  }
}

// ─── GP0 Environment Commands ───────────────────────────────────────────

void PS1GPU::GP0_Nop([[maybe_unused]] uint32_t cmd) {}
void PS1GPU::GP0_ClearCache([[maybe_unused]] uint32_t cmd) {}

void PS1GPU::GP0_FillRect(const std::vector<uint32_t> &params) {
  uint8_t r = static_cast<uint8_t>(params[0]);
  uint8_t g = static_cast<uint8_t>(params[0] >> 8);
  uint8_t b = static_cast<uint8_t>(params[0] >> 16);
  uint16_t color = ColorToVRAM(r, g, b);

  int16_t x = static_cast<int16_t>(params[1] & 0x3FF);
  int16_t y = static_cast<int16_t>((params[1] >> 16) & 0x1FF);
  uint16_t w = static_cast<uint16_t>(params[2] & 0x3FF);
  uint16_t h = static_cast<uint16_t>((params[2] >> 16) & 0x1FF);

  // Round up to 16-pixel alignment
  w = ((w + 0xF) & ~0xF);
  h = ((h + 0xF) & ~0xF);

  for (uint16_t dy = 0; dy < h; dy++) {
    for (uint16_t dx = 0; dx < w; dx++) {
      WriteVRAM(static_cast<uint32_t>((x + dx) & 0x3FF),
                static_cast<uint32_t>((y + dy) & 0x1FF), color);
    }
  }
}

void PS1GPU::GP0_DrawMode(uint32_t cmd) {
  texPageBaseX = cmd & 0xF;
  texPageBaseY = (cmd >> 4) & 1;
  semiTransparencyMode = (cmd >> 5) & 3;
  texPageColorDepth = (cmd >> 7) & 3;
  dither = (cmd >> 9) & 1;
  drawToDisplay = (cmd >> 10) & 1;
  textureDisable = (cmd >> 11) & 1;
}

void PS1GPU::GP0_TextureWindow(uint32_t cmd) {
  texWindowMaskX = cmd & 0x1F;
  texWindowMaskY = (cmd >> 5) & 0x1F;
  texWindowOffsetX = (cmd >> 10) & 0x1F;
  texWindowOffsetY = (cmd >> 15) & 0x1F;
}

void PS1GPU::GP0_DrawAreaTopLeft(uint32_t cmd) {
  drawAreaLeft = cmd & 0x3FF;
  drawAreaTop = (cmd >> 10) & 0x1FF;
}

void PS1GPU::GP0_DrawAreaBottomRight(uint32_t cmd) {
  drawAreaRight = cmd & 0x3FF;
  drawAreaBottom = (cmd >> 10) & 0x1FF;
}

void PS1GPU::GP0_DrawOffset(uint32_t cmd) {
  uint32_t x = cmd & 0x7FF;
  uint32_t y = (cmd >> 11) & 0x7FF;
  // Sign-extend from 11 bits
  drawOffsetX = static_cast<int16_t>((x ^ 0x400) - 0x400);
  drawOffsetY = static_cast<int16_t>((y ^ 0x400) - 0x400);
}

void PS1GPU::GP0_MaskBitSetting(uint32_t cmd) {
  maskBitSet = cmd & 1;
  maskBitCheck = (cmd >> 1) & 1;
}

// ─── GP0 Rendering (stubs for now — rasterize later) ────────────────────

void PS1GPU::GP0_MonoTriangle(const std::vector<uint32_t> &params,
                              [[maybe_unused]] bool opaque) {
  uint8_t r = static_cast<uint8_t>(params[0]);
  uint8_t g = static_cast<uint8_t>(params[0] >> 8);
  uint8_t b = static_cast<uint8_t>(params[0] >> 16);
  uint16_t color = ColorToVRAM(r, g, b);

  // Extract 3 vertices and draw (simplified — proper rasterization TODO)
  for (int i = 1; i <= 3; i++) {
    int16_t x = static_cast<int16_t>(params[i] & 0xFFFF) + drawOffsetX;
    int16_t y = static_cast<int16_t>(params[i] >> 16) + drawOffsetY;
    PutPixel(x, y, color);
  }
}

void PS1GPU::GP0_MonoQuad(const std::vector<uint32_t> &params,
                          [[maybe_unused]] bool opaque) {
  uint8_t r = static_cast<uint8_t>(params[0]);
  uint8_t g = static_cast<uint8_t>(params[0] >> 8);
  uint8_t b = static_cast<uint8_t>(params[0] >> 16);
  uint16_t color = ColorToVRAM(r, g, b);

  for (int i = 1; i <= 4; i++) {
    int16_t x = static_cast<int16_t>(params[i] & 0xFFFF) + drawOffsetX;
    int16_t y = static_cast<int16_t>(params[i] >> 16) + drawOffsetY;
    PutPixel(x, y, color);
  }
}

void PS1GPU::GP0_TexturedTriangle(
    [[maybe_unused]] const std::vector<uint32_t> &params,
    [[maybe_unused]] bool opaque) {
  // Textured rendering placeholder
}

void PS1GPU::GP0_TexturedQuad(
    [[maybe_unused]] const std::vector<uint32_t> &params,
    [[maybe_unused]] bool opaque) {
  // Textured rendering placeholder
}

void PS1GPU::GP0_ShadedTriangle(
    [[maybe_unused]] const std::vector<uint32_t> &params,
    [[maybe_unused]] bool opaque) {
  // Gouraud shaded triangle placeholder
}

void PS1GPU::GP0_ShadedQuad(
    [[maybe_unused]] const std::vector<uint32_t> &params,
    [[maybe_unused]] bool opaque) {
  // Gouraud shaded quad placeholder
}

void PS1GPU::GP0_MonoRect(const std::vector<uint32_t> &params) {
  uint8_t r = static_cast<uint8_t>(params[0]);
  uint8_t g = static_cast<uint8_t>(params[0] >> 8);
  uint8_t b = static_cast<uint8_t>(params[0] >> 16);
  uint16_t color = ColorToVRAM(r, g, b);

  int16_t x = static_cast<int16_t>(params[1] & 0xFFFF) + drawOffsetX;
  int16_t y = static_cast<int16_t>(params[1] >> 16) + drawOffsetY;

  uint16_t w = 1, h = 1;
  uint8_t cmd = static_cast<uint8_t>(params[0] >> 24);
  if (cmd == 0x70 || cmd == 0x72) {
    w = 8;
    h = 8;
  } else if (cmd == 0x78 || cmd == 0x7A) {
    w = 16;
    h = 16;
  } else if (params.size() >= 3) {
    w = params[2] & 0xFFFF;
    h = params[2] >> 16;
  }

  for (int16_t dy = 0; dy < static_cast<int16_t>(h); dy++) {
    for (int16_t dx = 0; dx < static_cast<int16_t>(w); dx++) {
      PutPixel(x + dx, y + dy, color);
    }
  }
}

void PS1GPU::GP0_TexturedRect(
    [[maybe_unused]] const std::vector<uint32_t> &params) {
  // Textured rectangle placeholder
}

void PS1GPU::GP0_MonoDot(const std::vector<uint32_t> &params) {
  uint8_t r = static_cast<uint8_t>(params[0]);
  uint8_t g = static_cast<uint8_t>(params[0] >> 8);
  uint8_t b = static_cast<uint8_t>(params[0] >> 16);
  uint16_t color = ColorToVRAM(r, g, b);

  int16_t x = static_cast<int16_t>(params[1] & 0xFFFF) + drawOffsetX;
  int16_t y = static_cast<int16_t>(params[1] >> 16) + drawOffsetY;
  PutPixel(x, y, color);
}

void PS1GPU::GP0_MonoLine(const std::vector<uint32_t> &params) {
  uint8_t r = static_cast<uint8_t>(params[0]);
  uint8_t g = static_cast<uint8_t>(params[0] >> 8);
  uint8_t b = static_cast<uint8_t>(params[0] >> 16);
  uint16_t color = ColorToVRAM(r, g, b);

  int16_t x0 = static_cast<int16_t>(params[1] & 0xFFFF) + drawOffsetX;
  int16_t y0 = static_cast<int16_t>(params[1] >> 16) + drawOffsetY;
  int16_t x1 = static_cast<int16_t>(params[2] & 0xFFFF) + drawOffsetX;
  int16_t y1 = static_cast<int16_t>(params[2] >> 16) + drawOffsetY;

  // Bresenham line drawing
  int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    PutPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1)
      break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

// ─── VRAM Transfer Commands ────────────────────────────────────────────

void PS1GPU::GP0_CopyRectVRAMtoVRAM(const std::vector<uint32_t> &params) {
  uint32_t srcX = params[1] & 0x3FF;
  uint32_t srcY = (params[1] >> 16) & 0x1FF;
  uint32_t dstX = params[2] & 0x3FF;
  uint32_t dstY = (params[2] >> 16) & 0x1FF;
  uint32_t w = params[3] & 0x3FF;
  uint32_t h = (params[3] >> 16) & 0x1FF;
  if (w == 0)
    w = 0x400;
  if (h == 0)
    h = 0x200;

  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      uint16_t pixel = ReadVRAM((srcX + x) & 0x3FF, (srcY + y) & 0x1FF);
      WriteVRAM((dstX + x) & 0x3FF, (dstY + y) & 0x1FF, pixel);
    }
  }
}

void PS1GPU::GP0_CopyRectCPUtoVRAM(const std::vector<uint32_t> &params) {
  vramTransferX = params[1] & 0x3FF;
  vramTransferY = (params[1] >> 16) & 0x1FF;
  vramTransferW = params[2] & 0x3FF;
  vramTransferH = (params[2] >> 16) & 0x1FF;
  if (vramTransferW == 0)
    vramTransferW = 0x400;
  if (vramTransferH == 0)
    vramTransferH = 0x200;
  vramTransferCurrX = vramTransferX;
  vramTransferCurrY = vramTransferY;
  gp0Mode = GP0Mode::CopyToVRAM;
}

void PS1GPU::GP0_CopyRectVRAMtoCPU(const std::vector<uint32_t> &params) {
  vramTransferX = params[1] & 0x3FF;
  vramTransferY = (params[1] >> 16) & 0x1FF;
  vramTransferW = params[2] & 0x3FF;
  vramTransferH = (params[2] >> 16) & 0x1FF;
  if (vramTransferW == 0)
    vramTransferW = 0x400;
  if (vramTransferH == 0)
    vramTransferH = 0x200;
  vramTransferCurrX = vramTransferX;
  vramTransferCurrY = vramTransferY;
  gp0Mode = GP0Mode::CopyFromVRAM;

  // Prepare first read
  uint16_t p0 = ReadVRAM(vramTransferCurrX, vramTransferCurrY);
  vramTransferCurrX++;
  uint16_t p1 = ReadVRAM(vramTransferCurrX, vramTransferCurrY);
  vramTransferCurrX++;
  gpuReadBuffer = p0 | (static_cast<uint32_t>(p1) << 16);
}

// ─── GP1 Commands ───────────────────────────────────────────────────────

void PS1GPU::WriteGP1(uint32_t value) {
  uint8_t cmd = static_cast<uint8_t>(value >> 24);

  if constexpr (Trace::GPU_CMD) {
    LogDebug("GP1 cmd %02X: %08X", cmd, value);
  }

  switch (cmd) {
  case 0x00:
    GP1_Reset();
    break;
  case 0x01:
    GP1_ResetCommandBuffer();
    break;
  case 0x02:
    GP1_AckIRQ();
    break;
  case 0x03:
    GP1_DisplayEnable(value);
    break;
  case 0x04:
    GP1_DMADirection(value);
    break;
  case 0x05:
    GP1_DisplayAreaStart(value);
    break;
  case 0x06:
    GP1_HorizontalDisplayRange(value);
    break;
  case 0x07:
    GP1_VerticalDisplayRange(value);
    break;
  case 0x08:
    GP1_DisplayMode(value);
    break;
  case 0x10:
  case 0x11:
  case 0x12:
  case 0x13:
  case 0x14:
  case 0x15:
  case 0x16:
  case 0x17:
  case 0x18:
  case 0x19:
  case 0x1A:
  case 0x1B:
  case 0x1C:
  case 0x1D:
  case 0x1E:
  case 0x1F:
    GP1_GetGPUInfo(value);
    break;
  default:
    if constexpr (Trace::GPU_CMD) {
      LogWarn("Unhandled GP1 cmd %02X", cmd);
    }
    break;
  }
}

void PS1GPU::GP1_Reset() { Reset(); }

void PS1GPU::GP1_ResetCommandBuffer() {
  gp0CommandBuffer.clear();
  gp0WordsRemaining = 0;
  gp0Mode = GP0Mode::Command;
}

void PS1GPU::GP1_AckIRQ() { irq1 = false; }

void PS1GPU::GP1_DisplayEnable(uint32_t cmd) { displayDisabled = cmd & 1; }

void PS1GPU::GP1_DMADirection(uint32_t cmd) { dmaDirection = cmd & 3; }

void PS1GPU::GP1_DisplayAreaStart(uint32_t cmd) {
  displayVRAMStartX = cmd & 0x3FE;
  displayVRAMStartY = (cmd >> 10) & 0x1FF;
}

void PS1GPU::GP1_HorizontalDisplayRange(uint32_t cmd) {
  displayHorizStart = cmd & 0xFFF;
  displayHorizEnd = (cmd >> 12) & 0xFFF;
}

void PS1GPU::GP1_VerticalDisplayRange(uint32_t cmd) {
  displayVertStart = cmd & 0x3FF;
  displayVertEnd = (cmd >> 10) & 0x3FF;
}

void PS1GPU::GP1_DisplayMode(uint32_t cmd) {
  hRes = cmd & 3;
  vRes480 = (cmd >> 2) & 1;
  palMode = (cmd >> 3) & 1;
  colorDepth24 = (cmd >> 4) & 1;
  interlace = (cmd >> 5) & 1;
  // Bit 6 = horizontal resolution 2 (368 mode)
  reverseFlag = (cmd >> 7) & 1;
}

void PS1GPU::GP1_GetGPUInfo(uint32_t cmd) {
  uint32_t info = cmd & 0xF;
  switch (info) {
  case 3:
    gpuReadBuffer = (drawAreaLeft | (drawAreaTop << 10));
    break;
  case 4:
    gpuReadBuffer = (drawAreaRight | (drawAreaBottom << 10));
    break;
  case 5:
    gpuReadBuffer = (static_cast<uint32_t>(drawOffsetX) & 0x7FF) |
                    ((static_cast<uint32_t>(drawOffsetY) & 0x7FF) << 11);
    break;
  case 7:
    gpuReadBuffer = 2;
    break; // GPU version
  default:
    gpuReadBuffer = 0;
    break;
  }
}

// ─── Timing ─────────────────────────────────────────────────────────────

void PS1GPU::Tick(uint32_t cpuCycles) {
  uint32_t cpuCyclesPerScanline = palMode ? Clock::CPU_CYCLES_PER_SCANLINE_PAL
                                          : Clock::CPU_CYCLES_PER_SCANLINE_NTSC;
  uint32_t totalScanlines =
      palMode ? Clock::SCANLINES_PAL : Clock::SCANLINES_NTSC;
  uint32_t visibleScanlines =
      palMode ? Clock::VISIBLE_SCANLINES_PAL : Clock::VISIBLE_SCANLINES_NTSC;

  // Accumulate CPU cycles directly to avoid integer division precision loss
  dotCounter += cpuCycles;

  while (dotCounter >= cpuCyclesPerScanline) {
    dotCounter -= cpuCyclesPerScanline;
    currentScanline++;

    if (currentScanline >= totalScanlines) {
      currentScanline = 0;
    }

    vblank = (currentScanline >= visibleScanlines);
  }
}

// ─── DMA Interface ──────────────────────────────────────────────────────

bool PS1GPU::DMAReady() const {
  switch (dmaDirection) {
  case 0:
    return false; // Off
  case 1:
    return true; // FIFO (always ready for simplicity)
  case 2:
    return true; // CPU to GP0
  case 3:
    return true; // VRAM to CPU
  default:
    return false;
  }
}

void PS1GPU::DMAWrite(uint32_t value) { WriteGP0(value); }

uint32_t PS1GPU::DMARead() {
  // Read from VRAM (for VRAM-to-CPU transfers)
  if (gp0Mode == GP0Mode::CopyFromVRAM) {
    uint32_t result = gpuReadBuffer;
    // Prepare next pair
    uint16_t p0 = ReadVRAM(vramTransferCurrX, vramTransferCurrY);
    vramTransferCurrX++;
    if (vramTransferCurrX >= vramTransferX + vramTransferW) {
      vramTransferCurrX = vramTransferX;
      vramTransferCurrY++;
    }
    uint16_t p1 = ReadVRAM(vramTransferCurrX, vramTransferCurrY);
    vramTransferCurrX++;
    if (vramTransferCurrX >= vramTransferX + vramTransferW) {
      vramTransferCurrX = vramTransferX;
      vramTransferCurrY++;
    }
    gpuReadBuffer = p0 | (static_cast<uint32_t>(p1) << 16);

    if (vramTransferCurrY >= vramTransferY + vramTransferH) {
      gp0Mode = GP0Mode::Command;
    }
    return result;
  }
  return gpuReadBuffer;
}

// ─── VRAM Access ────────────────────────────────────────────────────────

uint16_t PS1GPU::ReadVRAM(uint32_t x, uint32_t y) const {
  x &= (GPU::VRAM_WIDTH - 1);
  y &= (GPU::VRAM_HEIGHT - 1);
  return vram[y * GPU::VRAM_WIDTH + x];
}

void PS1GPU::WriteVRAM(uint32_t x, uint32_t y, uint16_t value) {
  x &= (GPU::VRAM_WIDTH - 1);
  y &= (GPU::VRAM_HEIGHT - 1);

  if (maskBitCheck && (vram[y * GPU::VRAM_WIDTH + x] & 0x8000))
    return;

  if (maskBitSet)
    value |= 0x8000;
  vram[y * GPU::VRAM_WIDTH + x] = value;
}

// ─── Display Dimensions ────────────────────────────────────────────────

uint32_t PS1GPU::GetDisplayWidth() const {
  static constexpr uint32_t widths[] = {256, 368, 320, 368, 512, 368, 640, 368};
  return widths[hRes & 7];
}

uint32_t PS1GPU::GetDisplayHeight() const { return vRes480 ? 480 : 240; }

// ─── Rasterizer Helpers ─────────────────────────────────────────────────

void PS1GPU::PutPixel(int16_t x, int16_t y, uint16_t color) {
  if (!IsInDrawingArea(x, y))
    return;
  WriteVRAM(static_cast<uint32_t>(x), static_cast<uint32_t>(y), color);
}

bool PS1GPU::IsInDrawingArea(int16_t x, int16_t y) const {
  return x >= static_cast<int16_t>(drawAreaLeft) &&
         x <= static_cast<int16_t>(drawAreaRight) &&
         y >= static_cast<int16_t>(drawAreaTop) &&
         y <= static_cast<int16_t>(drawAreaBottom);
}

uint16_t PS1GPU::ColorToVRAM(uint8_t r, uint8_t g, uint8_t b) {
  return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10);
}

// ─── Debug ──────────────────────────────────────────────────────────────

void PS1GPU::DumpState(std::ostream &os) const {
  os << "=== PS1 GPU ===" << std::endl;
  os << "GPUSTAT: " << std::hex << ReadGPUSTAT() << std::endl;
  os << "Scanline: " << std::dec << currentScanline << " VBlank: " << vblank
     << std::endl;
  os << "Draw Area: (" << drawAreaLeft << "," << drawAreaTop << ")-("
     << drawAreaRight << "," << drawAreaBottom << ")" << std::endl;
  os << "Draw Offset: (" << drawOffsetX << "," << drawOffsetY << ")"
     << std::endl;
  os << "Display: " << GetDisplayWidth() << "x" << GetDisplayHeight()
     << std::endl;
  os << "GP0 Commands: " << gp0CommandCount << std::endl;
}

std::string PS1GPU::GetDebugSummary() const {
  std::ostringstream os;
  os << "GPU scan=" << currentScanline << " cmds=" << gp0CommandCount;
  return os.str();
}

} // namespace AIO::Emulator::PS1
