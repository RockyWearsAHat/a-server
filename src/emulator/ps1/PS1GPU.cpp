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
  hRes2 = 0;
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

  primSemiTransparent = false;
  polyLineLastXY = 0;
  polyLineLastR = 0;
  polyLineLastG = 0;
  polyLineLastB = 0;
  polyLineSemiTransparent = false;
  shadedPolyExpectVertex = false;
  shadedPolyPendingR = 0;
  shadedPolyPendingG = 0;
  shadedPolyPendingB = 0;

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
  stat |= static_cast<uint32_t>(hRes2) << 18;
  stat |= static_cast<uint32_t>(vRes480) << 19;
  stat |= static_cast<uint32_t>(palMode) << 20;
  stat |= static_cast<uint32_t>(colorDepth24) << 21;
  stat |= static_cast<uint32_t>(interlace) << 22;
  stat |= static_cast<uint32_t>(displayDisabled) << 23;
  stat |= static_cast<uint32_t>(irq1) << 24;

  // Bit 25: DMA / Data Request — depends on direction
  if (dmaDirection == 1) {
    // FIFO state (always ready for simplicity)
    stat |= 1 << 25;
  } else if (dmaDirection == 2) {
    // CPU → GP0: same as bit 28 (ready to receive)
    stat |= 1 << 25;
  } else if (dmaDirection == 3) {
    // VRAM → CPU: same as bit 27 (ready to send)
    stat |= 1 << 25;
  }

  // DMA / Data request bits
  stat |= 1 << 26; // Ready to receive DMA block
  stat |= 1 << 27; // Ready to send VRAM to CPU
  stat |= 1 << 28; // Ready to receive command word

  stat |= static_cast<uint32_t>(dmaDirection) << 29;

  // Bit 31: toggles each frame (interlace field / odd-frame flag)
  stat |= static_cast<uint32_t>(oddFrame) << 31;

  return stat;
}

uint32_t PS1GPU::ReadGPUREAD() { return gpuReadBuffer; }

// ─── GP0 Commands ───────────────────────────────────────────────────────

void PS1GPU::WriteGP0(uint32_t value) {
  if constexpr (Trace::GPU_CMD) {
    LogDebug("GP0 write: %08X (mode=%d)", value, static_cast<int>(gp0Mode));
  }

  // Handle active polyline collection
  if (gp0Mode == GP0Mode::PolyLine || gp0Mode == GP0Mode::ShadedPolyLine) {
    // Terminator check: upper nibble of the vertex (or color) word == 0x5
    if ((value & 0xF0000000) == 0x50000000) {
      gp0Mode = GP0Mode::Command;
      return;
    }

    if (gp0Mode == GP0Mode::PolyLine) {
      // Next vertex — draw segment from last vertex to this one
      int16_t x0 =
          static_cast<int16_t>(
              static_cast<int32_t>((polyLineLastXY & 0x7FF) << 21) >> 21) +
          drawOffsetX;
      int16_t y0 =
          static_cast<int16_t>(
              static_cast<int32_t>(((polyLineLastXY >> 16) & 0x7FF) << 21) >>
              21) +
          drawOffsetY;
      int16_t x1 = static_cast<int16_t>(
                       static_cast<int32_t>((value & 0x7FF) << 21) >> 21) +
                   drawOffsetX;
      int16_t y1 =
          static_cast<int16_t>(
              static_cast<int32_t>(((value >> 16) & 0x7FF) << 21) >> 21) +
          drawOffsetY;
      uint16_t color = ColorToVRAM(polyLineLastR, polyLineLastG, polyLineLastB);
      // Bresenham line
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
      polyLineLastXY = value;
    } else {
      // ShadedPolyLine: alternating color / vertex words
      if (!shadedPolyExpectVertex) {
        shadedPolyPendingR = static_cast<uint8_t>(value);
        shadedPolyPendingG = static_cast<uint8_t>(value >> 8);
        shadedPolyPendingB = static_cast<uint8_t>(value >> 16);
        shadedPolyExpectVertex = true;
      } else {
        // Vertex word — draw from last vertex
        int16_t x0 =
            static_cast<int16_t>(
                static_cast<int32_t>((polyLineLastXY & 0x7FF) << 21) >> 21) +
            drawOffsetX;
        int16_t y0 =
            static_cast<int16_t>(
                static_cast<int32_t>(((polyLineLastXY >> 16) & 0x7FF) << 21) >>
                21) +
            drawOffsetY;
        int16_t x1 = static_cast<int16_t>(
                         static_cast<int32_t>((value & 0x7FF) << 21) >> 21) +
                     drawOffsetX;
        int16_t y1 =
            static_cast<int16_t>(
                static_cast<int32_t>(((value >> 16) & 0x7FF) << 21) >> 21) +
            drawOffsetY;
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        int totalSteps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
        if (totalSteps == 0)
          totalSteps = 1;
        int step = 0;
        int16_t cx = x0, cy = y0;
        while (true) {
          uint8_t r = static_cast<uint8_t>(
              polyLineLastR +
              (shadedPolyPendingR - polyLineLastR) * step / totalSteps);
          uint8_t g = static_cast<uint8_t>(
              polyLineLastG +
              (shadedPolyPendingG - polyLineLastG) * step / totalSteps);
          uint8_t b = static_cast<uint8_t>(
              polyLineLastB +
              (shadedPolyPendingB - polyLineLastB) * step / totalSteps);
          PutPixel(cx, cy, ColorToVRAM(r, g, b));
          if (cx == x1 && cy == y1)
            break;
          int e2 = 2 * err;
          if (e2 >= dy) {
            err += dy;
            cx += sx;
          }
          if (e2 <= dx) {
            err += dx;
            cy += sy;
          }
          step++;
        }
        polyLineLastXY = value;
        polyLineLastR = shadedPolyPendingR;
        polyLineLastG = shadedPolyPendingG;
        polyLineLastB = shadedPolyPendingB;
        shadedPolyExpectVertex = false;
      }
    }
    return;
  }

  if (gp0Mode == GP0Mode::CopyToVRAM) {
    uint16_t pixel1 = static_cast<uint16_t>(value);
    uint16_t pixel2 = static_cast<uint16_t>(value >> 16);

    WriteVRAM(vramTransferCurrX, vramTransferCurrY, pixel1);
    vramTransferCurrX++;
    if (vramTransferCurrX >= vramTransferX + vramTransferW) {
      vramTransferCurrX = vramTransferX;
      vramTransferCurrY++;
    }

    // Only write the second pixel if still within the transfer bounds (handles
    // odd W*H)
    bool done = (vramTransferCurrY >= vramTransferY + vramTransferH);
    if (!done) {
      WriteVRAM(vramTransferCurrX, vramTransferCurrY, pixel2);
      vramTransferCurrX++;
      if (vramTransferCurrX >= vramTransferX + vramTransferW) {
        vramTransferCurrX = vramTransferX;
        vramTransferCurrY++;
      }
      done = (vramTransferCurrY >= vramTransferY + vramTransferH);
    }

    if (done)
      gp0Mode = GP0Mode::Command;
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

  // Only drawing primitives (0x20-0x7F) carry a semi-transparency bit (bit 1).
  // Environment setup commands (0xE1-0xE6) must NOT alter this flag — their
  // command bytes happen to have bit 1 set (e.g. 0xE2, 0xE3, 0xE6) which would
  // otherwise corrupt all subsequent rendering with unwanted blending.
  if (cmd >= 0x20 && cmd <= 0x7F) {
    primSemiTransparent = (cmd & 2) != 0;
  }

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
    // Transition to polyline mode: retain color and last vertex for
    // continuation
    primSemiTransparent = (cmd == 0x42);
    polyLineLastXY = gp0CommandBuffer[2];
    polyLineLastR = static_cast<uint8_t>(gp0CommandBuffer[0]);
    polyLineLastG = static_cast<uint8_t>(gp0CommandBuffer[0] >> 8);
    polyLineLastB = static_cast<uint8_t>(gp0CommandBuffer[0] >> 16);
    gp0Mode = GP0Mode::PolyLine;
    break;
  case 0x50:
  case 0x52:
    GP0_ShadedLine(gp0CommandBuffer);
    // Transition to shaded polyline mode with last vertex/color for
    // continuation
    primSemiTransparent = (cmd == 0x52);
    polyLineLastXY = gp0CommandBuffer[3];
    polyLineLastR = static_cast<uint8_t>(gp0CommandBuffer[2]);
    polyLineLastG = static_cast<uint8_t>(gp0CommandBuffer[2] >> 8);
    polyLineLastB = static_cast<uint8_t>(gp0CommandBuffer[2] >> 16);
    gp0Mode = GP0Mode::ShadedPolyLine;
    break;
  case 0x34:
  case 0x36:
    GP0_ShadedTexturedTriangle(gp0CommandBuffer, (cmd & 1) == 0);
    break;
  case 0x3C:
  case 0x3E:
    GP0_ShadedTexturedQuad(gp0CommandBuffer, (cmd & 1) == 0);
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
  case 0x74:
  case 0x75:
  case 0x76:
  case 0x77:
    GP0_TexturedRect(gp0CommandBuffer);
    break;
  case 0x78:
  case 0x7A:
    GP0_MonoRect(gp0CommandBuffer);
    break;
  case 0x7C:
  case 0x7D:
  case 0x7E:
  case 0x7F:
    GP0_TexturedRect(gp0CommandBuffer);
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

  // Only width is rounded to 16-pixel alignment per hardware spec
  w = ((w + 0xF) & ~0xF);

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
  drawOffsetX = static_cast<int16_t>((x ^ 0x400) - 0x400);
  drawOffsetY = static_cast<int16_t>((y ^ 0x400) - 0x400);
}

void PS1GPU::GP0_MaskBitSetting(uint32_t cmd) {
  maskBitSet = cmd & 1;
  maskBitCheck = (cmd >> 1) & 1;
}

// ─── GP0 Rendering ──────────────────────────────────────────────────────

void PS1GPU::GP0_MonoTriangle(const std::vector<uint32_t> &params,
                              [[maybe_unused]] bool opaque) {
  uint8_t r = static_cast<uint8_t>(params[0]);
  uint8_t g = static_cast<uint8_t>(params[0] >> 8);
  uint8_t b = static_cast<uint8_t>(params[0] >> 16);

  int16_t x0 = static_cast<int16_t>(
                   static_cast<int32_t>((params[1] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y0 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[1] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;
  int16_t x1 = static_cast<int16_t>(
                   static_cast<int32_t>((params[2] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y1 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[2] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;
  int16_t x2 = static_cast<int16_t>(
                   static_cast<int32_t>((params[3] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y2 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[3] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;

  RasterizeTriangle(x0, y0, r, g, b, x1, y1, r, g, b, x2, y2, r, g, b);
}

void PS1GPU::GP0_MonoQuad(const std::vector<uint32_t> &params,
                          [[maybe_unused]] bool opaque) {
  uint8_t r = static_cast<uint8_t>(params[0]);
  uint8_t g = static_cast<uint8_t>(params[0] >> 8);
  uint8_t b = static_cast<uint8_t>(params[0] >> 16);

  int16_t x0 = static_cast<int16_t>(
                   static_cast<int32_t>((params[1] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y0 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[1] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;
  int16_t x1 = static_cast<int16_t>(
                   static_cast<int32_t>((params[2] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y1 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[2] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;
  int16_t x2 = static_cast<int16_t>(
                   static_cast<int32_t>((params[3] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y2 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[3] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;
  int16_t x3 = static_cast<int16_t>(
                   static_cast<int32_t>((params[4] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y3 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[4] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;

  // Split quad into two triangles (0-1-2, 1-2-3)
  RasterizeTriangle(x0, y0, r, g, b, x1, y1, r, g, b, x2, y2, r, g, b);
  RasterizeTriangle(x1, y1, r, g, b, x2, y2, r, g, b, x3, y3, r, g, b);
}

void PS1GPU::GP0_TexturedTriangle(const std::vector<uint32_t> &params,
                                  [[maybe_unused]] bool opaque) {
  // Format: color+cmd, vert0, tex0+clut, vert1, tex1+texpage, vert2, tex2
  uint8_t r0 = static_cast<uint8_t>(params[0]);
  uint8_t g0 = static_cast<uint8_t>(params[0] >> 8);
  uint8_t b0 = static_cast<uint8_t>(params[0] >> 16);

  int16_t x0 = static_cast<int16_t>(
                   static_cast<int32_t>((params[1] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y0 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[1] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;
  uint8_t u0 = static_cast<uint8_t>(params[2]);
  uint8_t v0 = static_cast<uint8_t>(params[2] >> 8);
  uint16_t clut = static_cast<uint16_t>(params[2] >> 16);

  int16_t x1 = static_cast<int16_t>(
                   static_cast<int32_t>((params[3] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y1 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[3] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;
  uint8_t u1 = static_cast<uint8_t>(params[4]);
  uint8_t v1 = static_cast<uint8_t>(params[4] >> 8);
  uint16_t texPage = static_cast<uint16_t>(params[4] >> 16);

  // Textured polygon commands carry an embedded texPage that updates the GPU's
  // draw-mode state — subsequent sprite rectangles rely on this for correct
  // texture page selection.
  texPageBaseX = texPage & 0xF;
  texPageBaseY = (texPage >> 4) & 1;
  semiTransparencyMode = (texPage >> 5) & 3;
  texPageColorDepth = (texPage >> 7) & 3;
  // Explicitly set OR clear from the embedded texPage bit — never leave it
  // sticky.
  textureDisable = (texPage >> 11) & 1;

  int16_t x2 = static_cast<int16_t>(
                   static_cast<int32_t>((params[5] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y2 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[5] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;
  uint8_t u2 = static_cast<uint8_t>(params[6]);
  uint8_t v2 = static_cast<uint8_t>(params[6] >> 8);

  // When textureDisable is set, render as flat/Gouraud using vertex colour only
  if (textureDisable) {
    RasterizeTriangle(x0, y0, r0, g0, b0, x1, y1, r0, g0, b0, x2, y2, r0, g0,
                      b0);
    return;
  }

  // Bounding box
  int16_t minX = std::min({x0, x1, x2});
  int16_t maxX = std::max({x0, x1, x2});
  int16_t minY = std::min({y0, y1, y2});
  int16_t maxY = std::max({y0, y1, y2});

  // Hardware guard: skip primitives wider or taller than 1023 pixels
  if (maxX - minX > 1023 || maxY - minY > 1023)
    return;

  minX = std::max(minX, static_cast<int16_t>(drawAreaLeft));
  maxX = std::min(maxX, static_cast<int16_t>(drawAreaRight));
  minY = std::max(minY, static_cast<int16_t>(drawAreaTop));
  maxY = std::min(maxY, static_cast<int16_t>(drawAreaBottom));

  int32_t area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
  if (area == 0)
    return;

  for (int16_t py = minY; py <= maxY; py++) {
    for (int16_t px = minX; px <= maxX; px++) {
      int32_t w0 = (x1 - x0) * (py - y0) - (px - x0) * (y1 - y0);
      int32_t w1 = (x2 - x1) * (py - y1) - (px - x1) * (y2 - y1);
      int32_t w2 = (x0 - x2) * (py - y2) - (px - x2) * (y0 - y2);

      bool inside = (area > 0) ? (w0 >= 0 && w1 >= 0 && w2 >= 0)
                               : (w0 <= 0 && w1 <= 0 && w2 <= 0);
      if (!inside)
        continue;

      // Barycentric interpolation for texture coordinates
      int32_t absArea = std::abs(area);
      int32_t bw0 = std::abs(w1); // weight for v0
      int32_t bw1 = std::abs(w2); // weight for v1
      int32_t bw2 = std::abs(w0); // weight for v2

      int32_t u = (bw0 * u0 + bw1 * u1 + bw2 * u2) / absArea;
      int32_t v = (bw0 * v0 + bw1 * v1 + bw2 * v2) / absArea;

      uint16_t texel = SampleTexture(u & 0xFF, v & 0xFF, clut, texPage);
      if (texel == 0)
        continue; // Transparent black

      // Semi-transparent pixels have bit 15 set; blend when the command is also
      // semi-transparent
      bool blendThisPixel = primSemiTransparent && (texel & 0x8000);
      uint16_t finalColor;
      if (!opaque) {
        finalColor = texel;
      } else {
        uint8_t tr = ((texel & 0x1F) << 3);
        uint8_t tg = (((texel >> 5) & 0x1F) << 3);
        uint8_t tb = (((texel >> 10) & 0x1F) << 3);
        tr = static_cast<uint8_t>(std::min(255, (tr * r0) >> 7));
        tg = static_cast<uint8_t>(std::min(255, (tg * g0) >> 7));
        tb = static_cast<uint8_t>(std::min(255, (tb * b0) >> 7));
        finalColor = ColorToVRAM(tr, tg, tb);
      }
      if (blendThisPixel) {
        // Temporarily clear primSemiTransparent so PutPixel does not
        // double-blend
        primSemiTransparent = false;
        uint16_t dst =
            ReadVRAM(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
        finalColor = BlendPixels(finalColor, dst);
        primSemiTransparent = true;
      }
      PutPixel(px, py, finalColor);
    }
  }
}

void PS1GPU::GP0_TexturedQuad(const std::vector<uint32_t> &params,
                              [[maybe_unused]] bool opaque) {
  // 9 words: cmd+c(0), v0(1), t0+clut(2), v1(3), t1+texpage(4), v2(5), t2(6),
  // v3(7), t3(8) Tri 1: verts 0,1,2
  std::vector<uint32_t> tri1 = {params[0], params[1], params[2], params[3],
                                params[4], params[5], params[6]};
  GP0_TexturedTriangle(tri1, opaque);

  // Tri 2: verts 1,2,3 — preserve original CLUT (params[2] upper) and texPage
  // (params[4] upper)
  uint32_t t1WithClut = (params[4] & 0x0000FFFF) | (params[2] & 0xFFFF0000);
  uint32_t t2WithTexPage = (params[6] & 0x0000FFFF) | (params[4] & 0xFFFF0000);
  std::vector<uint32_t> tri2 = {params[0],     params[3], t1WithClut, params[5],
                                t2WithTexPage, params[7], params[8]};
  GP0_TexturedTriangle(tri2, opaque);
}

void PS1GPU::GP0_ShadedTriangle(const std::vector<uint32_t> &params,
                                [[maybe_unused]] bool opaque) {
  // Format: color0+cmd, vert0, color1, vert1, color2, vert2
  uint8_t r0 = static_cast<uint8_t>(params[0]);
  uint8_t g0 = static_cast<uint8_t>(params[0] >> 8);
  uint8_t b0 = static_cast<uint8_t>(params[0] >> 16);
  int16_t x0 = static_cast<int16_t>(
                   static_cast<int32_t>((params[1] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y0 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[1] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;

  uint8_t r1 = static_cast<uint8_t>(params[2]);
  uint8_t g1 = static_cast<uint8_t>(params[2] >> 8);
  uint8_t b1 = static_cast<uint8_t>(params[2] >> 16);
  int16_t x1 = static_cast<int16_t>(
                   static_cast<int32_t>((params[3] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y1 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[3] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;

  uint8_t r2 = static_cast<uint8_t>(params[4]);
  uint8_t g2 = static_cast<uint8_t>(params[4] >> 8);
  uint8_t b2 = static_cast<uint8_t>(params[4] >> 16);
  int16_t x2 = static_cast<int16_t>(
                   static_cast<int32_t>((params[5] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y2 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[5] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;

  RasterizeTriangle(x0, y0, r0, g0, b0, x1, y1, r1, g1, b1, x2, y2, r2, g2, b2);
}

void PS1GPU::GP0_ShadedQuad(const std::vector<uint32_t> &params,
                            [[maybe_unused]] bool opaque) {
  // Format: color0+cmd, vert0, color1, vert1, color2, vert2, color3, vert3
  uint8_t r0 = static_cast<uint8_t>(params[0]);
  uint8_t g0 = static_cast<uint8_t>(params[0] >> 8);
  uint8_t b0 = static_cast<uint8_t>(params[0] >> 16);
  int16_t x0 = static_cast<int16_t>(
                   static_cast<int32_t>((params[1] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y0 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[1] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;

  uint8_t r1 = static_cast<uint8_t>(params[2]);
  uint8_t g1 = static_cast<uint8_t>(params[2] >> 8);
  uint8_t b1 = static_cast<uint8_t>(params[2] >> 16);
  int16_t x1 = static_cast<int16_t>(
                   static_cast<int32_t>((params[3] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y1 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[3] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;

  uint8_t r2 = static_cast<uint8_t>(params[4]);
  uint8_t g2 = static_cast<uint8_t>(params[4] >> 8);
  uint8_t b2 = static_cast<uint8_t>(params[4] >> 16);
  int16_t x2 = static_cast<int16_t>(
                   static_cast<int32_t>((params[5] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y2 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[5] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;

  uint8_t r3 = static_cast<uint8_t>(params[6]);
  uint8_t g3 = static_cast<uint8_t>(params[6] >> 8);
  uint8_t b3 = static_cast<uint8_t>(params[6] >> 16);
  int16_t x3 = static_cast<int16_t>(
                   static_cast<int32_t>((params[7] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y3 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[7] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;

  RasterizeTriangle(x0, y0, r0, g0, b0, x1, y1, r1, g1, b1, x2, y2, r2, g2, b2);
  RasterizeTriangle(x1, y1, r1, g1, b1, x2, y2, r2, g2, b2, x3, y3, r3, g3, b3);
}

void PS1GPU::GP0_ShadedTexturedTriangle(const std::vector<uint32_t> &params,
                                        [[maybe_unused]] bool opaque) {
  // 9 words: color0+cmd, vert0, tex0+clut, color1, vert1, tex1+texpage, color2,
  // vert2, tex2
  uint8_t r0 = static_cast<uint8_t>(params[0]);
  uint8_t g0 = static_cast<uint8_t>(params[0] >> 8);
  uint8_t b0 = static_cast<uint8_t>(params[0] >> 16);
  int16_t x0 = static_cast<int16_t>(
                   static_cast<int32_t>((params[1] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y0 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[1] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;
  uint8_t u0 = static_cast<uint8_t>(params[2]);
  uint8_t v0 = static_cast<uint8_t>(params[2] >> 8);
  uint16_t clut = static_cast<uint16_t>(params[2] >> 16);

  uint8_t r1 = static_cast<uint8_t>(params[3]);
  uint8_t g1 = static_cast<uint8_t>(params[3] >> 8);
  uint8_t b1 = static_cast<uint8_t>(params[3] >> 16);
  int16_t x1 = static_cast<int16_t>(
                   static_cast<int32_t>((params[4] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y1 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[4] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;
  uint8_t u1 = static_cast<uint8_t>(params[5]);
  uint8_t v1 = static_cast<uint8_t>(params[5] >> 8);
  uint16_t texPage = static_cast<uint16_t>(params[5] >> 16);

  // Propagate embedded texPage to GPU draw-mode state (same as
  // TexturedTriangle). Explicitly set OR clear — never leave textureDisable
  // sticky.
  texPageBaseX = texPage & 0xF;
  texPageBaseY = (texPage >> 4) & 1;
  semiTransparencyMode = (texPage >> 5) & 3;
  texPageColorDepth = (texPage >> 7) & 3;
  textureDisable = (texPage >> 11) & 1;

  uint8_t r2 = static_cast<uint8_t>(params[6]);
  uint8_t g2 = static_cast<uint8_t>(params[6] >> 8);
  uint8_t b2 = static_cast<uint8_t>(params[6] >> 16);
  int16_t x2 = static_cast<int16_t>(
                   static_cast<int32_t>((params[7] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y2 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[7] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;
  uint8_t u2 = static_cast<uint8_t>(params[8]);
  uint8_t v2 = static_cast<uint8_t>(params[8] >> 8);

  // When textureDisable is set, use Gouraud shading without texture
  if (textureDisable) {
    RasterizeTriangle(x0, y0, r0, g0, b0, x1, y1, r1, g1, b1, x2, y2, r2, g2,
                      b2);
    return;
  }

  int16_t minX =
      std::max(std::min({x0, x1, x2}), static_cast<int16_t>(drawAreaLeft));
  int16_t maxX =
      std::min(std::max({x0, x1, x2}), static_cast<int16_t>(drawAreaRight));
  int16_t minY =
      std::max(std::min({y0, y1, y2}), static_cast<int16_t>(drawAreaTop));
  int16_t maxY =
      std::min(std::max({y0, y1, y2}), static_cast<int16_t>(drawAreaBottom));

  // Hardware guard: skip oversized primitives
  if (std::max({x0, x1, x2}) - std::min({x0, x1, x2}) > 1023)
    return;
  if (std::max({y0, y1, y2}) - std::min({y0, y1, y2}) > 1023)
    return;

  int32_t area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
  if (area == 0)
    return;

  for (int16_t py = minY; py <= maxY; py++) {
    for (int16_t px = minX; px <= maxX; px++) {
      int32_t w0 = (x1 - x0) * (py - y0) - (px - x0) * (y1 - y0);
      int32_t w1 = (x2 - x1) * (py - y1) - (px - x1) * (y2 - y1);
      int32_t w2 = (x0 - x2) * (py - y2) - (px - x2) * (y0 - y2);

      bool inside = (area > 0) ? (w0 >= 0 && w1 >= 0 && w2 >= 0)
                               : (w0 <= 0 && w1 <= 0 && w2 <= 0);
      if (!inside)
        continue;

      int32_t absArea = std::abs(area);
      int32_t bw0 = std::abs(w1);
      int32_t bw1 = std::abs(w2);
      int32_t bw2 = std::abs(w0);

      int32_t u = (bw0 * u0 + bw1 * u1 + bw2 * u2) / absArea;
      int32_t v = (bw0 * v0 + bw1 * v1 + bw2 * v2) / absArea;

      uint16_t texel = SampleTexture(u & 0xFF, v & 0xFF, clut, texPage);
      if (texel == 0)
        continue;

      bool blendThisPixel = primSemiTransparent && (texel & 0x8000);
      uint16_t finalColor;
      if (!opaque) {
        finalColor = texel;
      } else {
        // Interpolate vertex color for modulation
        uint8_t cr =
            static_cast<uint8_t>((bw0 * r0 + bw1 * r1 + bw2 * r2) / absArea);
        uint8_t cg =
            static_cast<uint8_t>((bw0 * g0 + bw1 * g1 + bw2 * g2) / absArea);
        uint8_t cb =
            static_cast<uint8_t>((bw0 * b0 + bw1 * b1 + bw2 * b2) / absArea);
        uint8_t tr = ((texel & 0x1F) << 3);
        uint8_t tg = (((texel >> 5) & 0x1F) << 3);
        uint8_t tb = (((texel >> 10) & 0x1F) << 3);
        tr = static_cast<uint8_t>(std::min(255, (tr * cr) >> 7));
        tg = static_cast<uint8_t>(std::min(255, (tg * cg) >> 7));
        tb = static_cast<uint8_t>(std::min(255, (tb * cb) >> 7));
        finalColor = ColorToVRAM(tr, tg, tb);
      }
      if (blendThisPixel) {
        primSemiTransparent = false;
        uint16_t dst =
            ReadVRAM(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
        finalColor = BlendPixels(finalColor, dst);
        primSemiTransparent = true;
      }
      PutPixel(px, py, finalColor);
    }
  }
}

void PS1GPU::GP0_ShadedTexturedQuad(const std::vector<uint32_t> &params,
                                    [[maybe_unused]] bool opaque) {
  // 12 words: c0+cmd(0), v0(1), t0+clut(2), c1(3), v1(4), t1+texpage(5),
  //           c2(6), v2(7), t2(8), c3(9), v3(10), t3(11)
  // Tri 1: verts 0,1,2
  std::vector<uint32_t> tri1 = {params[0], params[1], params[2],
                                params[3], params[4], params[5],
                                params[6], params[7], params[8]};
  GP0_ShadedTexturedTriangle(tri1, opaque);

  // Tri 2: verts 1,2,3 — preserve CLUT from params[2] upper and texPage from
  // params[5] upper
  uint32_t t1WithClut = (params[5] & 0x0000FFFF) | (params[2] & 0xFFFF0000);
  uint32_t t2WithTexPage = (params[8] & 0x0000FFFF) | (params[5] & 0xFFFF0000);
  std::vector<uint32_t> tri2 = {params[3], params[4],  t1WithClut,
                                params[6], params[7],  t2WithTexPage,
                                params[9], params[10], params[11]};
  GP0_ShadedTexturedTriangle(tri2, opaque);
}

void PS1GPU::GP0_MonoRect(const std::vector<uint32_t> &params) {
  uint8_t r = static_cast<uint8_t>(params[0]);
  uint8_t g = static_cast<uint8_t>(params[0] >> 8);
  uint8_t b = static_cast<uint8_t>(params[0] >> 16);
  uint16_t color = ColorToVRAM(r, g, b);

  int16_t x = static_cast<int16_t>(
                  static_cast<int32_t>((params[1] & 0x7FF) << 21) >> 21) +
              drawOffsetX;
  int16_t y =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[1] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;

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

void PS1GPU::GP0_TexturedRect(const std::vector<uint32_t> &params) {
  uint8_t r = static_cast<uint8_t>(params[0]);
  uint8_t g = static_cast<uint8_t>(params[0] >> 8);
  uint8_t b = static_cast<uint8_t>(params[0] >> 16);
  uint8_t cmd = static_cast<uint8_t>(params[0] >> 24);

  // PS1 GPU vertex positions are 11-bit signed (-1024 to +1023)
  uint32_t rawXBits = params[1] & 0x7FF;
  uint32_t rawYBits = (params[1] >> 16) & 0x7FF;
  int16_t rawX = static_cast<int16_t>((rawXBits ^ 0x400) - 0x400);
  int16_t rawY = static_cast<int16_t>((rawYBits ^ 0x400) - 0x400);
  int16_t x = rawX + drawOffsetX;
  int16_t y = rawY + drawOffsetY;

  uint8_t u = static_cast<uint8_t>(params[2]);
  uint8_t v = static_cast<uint8_t>(params[2] >> 8);
  uint16_t clut = static_cast<uint16_t>(params[2] >> 16);

  // Rect sprites always derive their texPage from the current GPU draw-mode
  // state (set by E1 or updated by the last textured polygon command).
  uint16_t texPage = texPageBaseX | (texPageBaseY << 4) |
                     (semiTransparencyMode << 5) | (texPageColorDepth << 7);

  uint16_t w = 0, h = 0;
  if ((cmd & 0xF8) == 0x70) {
    w = 8;
    h = 8;
  } else if ((cmd & 0xF8) == 0x78) {
    w = 16;
    h = 16;
  } else if (params.size() >= 4) {
    w = params[3] & 0xFFFF;
    h = params[3] >> 16;
  }

  // When textureDisable is set, render as a solid colour rectangle using the
  // vertex colour so the primitive is still visible.
  if (textureDisable) {
    for (int16_t dy = 0; dy < static_cast<int16_t>(h); dy++) {
      for (int16_t dx = 0; dx < static_cast<int16_t>(w); dx++) {
        PutPixel(x + dx, y + dy, ColorToVRAM(r, g, b));
      }
    }
    return;
  }

  bool rawTexture = (cmd & 1);

  uint32_t pixelsDrawn = 0;
  for (int16_t dy = 0; dy < static_cast<int16_t>(h); dy++) {
    for (int16_t dx = 0; dx < static_cast<int16_t>(w); dx++) {
      uint16_t texel =
          SampleTexture((u + dx) & 0xFF, (v + dy) & 0xFF, clut, texPage);
      if (texel == 0)
        continue;

      bool blendThisPixel = primSemiTransparent && (texel & 0x8000);
      uint16_t finalColor;
      if (rawTexture) {
        finalColor = texel;
      } else {
        uint8_t tr = ((texel & 0x1F) << 3);
        uint8_t tg = (((texel >> 5) & 0x1F) << 3);
        uint8_t tb = (((texel >> 10) & 0x1F) << 3);
        tr = static_cast<uint8_t>(std::min(255, (tr * r) >> 7));
        tg = static_cast<uint8_t>(std::min(255, (tg * g) >> 7));
        tb = static_cast<uint8_t>(std::min(255, (tb * b) >> 7));
        finalColor = ColorToVRAM(tr, tg, tb);
      }
      if (blendThisPixel) {
        primSemiTransparent = false;
        uint16_t dst = ReadVRAM(static_cast<uint32_t>(x + dx),
                                static_cast<uint32_t>(y + dy));
        finalColor = BlendPixels(finalColor, dst);
        primSemiTransparent = true;
      }
      PutPixel(x + dx, y + dy, finalColor);
      pixelsDrawn++;
    }
  }
  if constexpr (Trace::GPU_CMD) {
    LogDebug("TexRect: cmd=%02X pos=(%d,%d) uv=(%u,%u) wh=%ux%u drawn=%u", cmd,
             x, y, u, v, w, h, pixelsDrawn);
  }
}

void PS1GPU::GP0_MonoDot(const std::vector<uint32_t> &params) {
  uint8_t r = static_cast<uint8_t>(params[0]);
  uint8_t g = static_cast<uint8_t>(params[0] >> 8);
  uint8_t b = static_cast<uint8_t>(params[0] >> 16);
  uint16_t color = ColorToVRAM(r, g, b);

  int16_t x = static_cast<int16_t>(
                  static_cast<int32_t>((params[1] & 0x7FF) << 21) >> 21) +
              drawOffsetX;
  int16_t y =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[1] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;
  PutPixel(x, y, color);
}

void PS1GPU::GP0_MonoLine(const std::vector<uint32_t> &params) {
  uint8_t r = static_cast<uint8_t>(params[0]);
  uint8_t g = static_cast<uint8_t>(params[0] >> 8);
  uint8_t b = static_cast<uint8_t>(params[0] >> 16);
  uint16_t color = ColorToVRAM(r, g, b);

  int16_t x0 = static_cast<int16_t>(
                   static_cast<int32_t>((params[1] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y0 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[1] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;
  int16_t x1 = static_cast<int16_t>(
                   static_cast<int32_t>((params[2] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y1 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[2] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;

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

void PS1GPU::GP0_ShadedLine(const std::vector<uint32_t> &params) {
  uint8_t r0 = static_cast<uint8_t>(params[0]);
  uint8_t g0 = static_cast<uint8_t>(params[0] >> 8);
  uint8_t b0 = static_cast<uint8_t>(params[0] >> 16);
  int16_t x0 = static_cast<int16_t>(
                   static_cast<int32_t>((params[1] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y0 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[1] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;

  uint8_t r1 = static_cast<uint8_t>(params[2]);
  uint8_t g1 = static_cast<uint8_t>(params[2] >> 8);
  uint8_t b1 = static_cast<uint8_t>(params[2] >> 16);
  int16_t x1 = static_cast<int16_t>(
                   static_cast<int32_t>((params[3] & 0x7FF) << 21) >> 21) +
               drawOffsetX;
  int16_t y1 =
      static_cast<int16_t>(
          static_cast<int32_t>(((params[3] >> 16) & 0x7FF) << 21) >> 21) +
      drawOffsetY;

  int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  int totalSteps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
  if (totalSteps == 0)
    totalSteps = 1;
  int step = 0;

  while (true) {
    // Interpolate color
    uint8_t r = static_cast<uint8_t>(r0 + (r1 - r0) * step / totalSteps);
    uint8_t g = static_cast<uint8_t>(g0 + (g1 - g0) * step / totalSteps);
    uint8_t b = static_cast<uint8_t>(b0 + (b1 - b0) * step / totalSteps);
    PutPixel(x0, y0, ColorToVRAM(r, g, b));

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
    step++;
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
  hRes2 = (cmd >> 6) & 1;
  vRes480 = (cmd >> 2) & 1;
  palMode = (cmd >> 3) & 1;
  colorDepth24 = (cmd >> 4) & 1;
  interlace = (cmd >> 5) & 1;
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
      oddFrame = !oddFrame;
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
  if (hRes2)
    return 368;
  static constexpr uint32_t widths[] = {256, 320, 512, 640};
  return widths[hRes & 3];
}

uint32_t PS1GPU::GetDisplayHeight() const { return vRes480 ? 480 : 240; }

// ─── Rasterizer Helpers ─────────────────────────────────────────────────

void PS1GPU::PutPixel(int16_t x, int16_t y, uint16_t color) {
  if (!IsInDrawingArea(x, y))
    return;
  if (primSemiTransparent) {
    uint16_t dst = ReadVRAM(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    color = BlendPixels(color, dst);
  }
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

// Blend src (foreground) into dst (background) using the current
// semi-transparency mode. All arithmetic is in 5-bit channel space (PS1 15-bit
// color).
uint16_t PS1GPU::BlendPixels(uint16_t src, uint16_t dst) const {
  int32_t sr = src & 0x1F, sg = (src >> 5) & 0x1F, sb = (src >> 10) & 0x1F;
  int32_t dr = dst & 0x1F, dg = (dst >> 5) & 0x1F, db = (dst >> 10) & 0x1F;
  int32_t r, g, b;
  switch (semiTransparencyMode) {
  case 0: // B/2 + F/2
    r = (dr + sr) / 2;
    g = (dg + sg) / 2;
    b = (db + sb) / 2;
    break;
  case 1: // B + F (additive)
    r = std::min(31, dr + sr);
    g = std::min(31, dg + sg);
    b = std::min(31, db + sb);
    break;
  case 2: // B - F (subtractive)
    r = std::max(0, dr - sr);
    g = std::max(0, dg - sg);
    b = std::max(0, db - sb);
    break;
  case 3: // B + F/4
    r = std::min(31, dr + sr / 4);
    g = std::min(31, dg + sg / 4);
    b = std::min(31, db + sb / 4);
    break;
  default:
    return src;
  }
  return static_cast<uint16_t>(r | (g << 5) | (b << 10));
}

// ─── Triangle Rasterizer (Gouraud shading via half-space method) ────────

void PS1GPU::RasterizeTriangle(int16_t x0, int16_t y0, uint8_t r0, uint8_t g0,
                               uint8_t b0, int16_t x1, int16_t y1, uint8_t r1,
                               uint8_t g1, uint8_t b1, int16_t x2, int16_t y2,
                               uint8_t r2, uint8_t g2, uint8_t b2) {
  // Hardware guard: ignore primitives whose bounding box exceeds 1023 pixels
  // in either dimension (PS1 GPU behaviour).
  if (std::max({x0, x1, x2}) - std::min({x0, x1, x2}) > 1023)
    return;
  if (std::max({y0, y1, y2}) - std::min({y0, y1, y2}) > 1023)
    return;

  // Bounding box clipped to drawing area
  int16_t minX =
      std::max(std::min({x0, x1, x2}), static_cast<int16_t>(drawAreaLeft));
  int16_t maxX =
      std::min(std::max({x0, x1, x2}), static_cast<int16_t>(drawAreaRight));
  int16_t minY =
      std::max(std::min({y0, y1, y2}), static_cast<int16_t>(drawAreaTop));
  int16_t maxY =
      std::min(std::max({y0, y1, y2}), static_cast<int16_t>(drawAreaBottom));

  // 2x signed area — also used as denominator for barycentric weights
  int32_t area = static_cast<int32_t>(x1 - x0) * (y2 - y0) -
                 static_cast<int32_t>(x2 - x0) * (y1 - y0);
  if (area == 0)
    return;

  for (int16_t py = minY; py <= maxY; py++) {
    for (int16_t px = minX; px <= maxX; px++) {
      // Edge functions
      int32_t w0 = static_cast<int32_t>(x1 - x0) * (py - y0) -
                   static_cast<int32_t>(px - x0) * (y1 - y0);
      int32_t w1 = static_cast<int32_t>(x2 - x1) * (py - y1) -
                   static_cast<int32_t>(px - x1) * (y2 - y1);
      int32_t w2 = static_cast<int32_t>(x0 - x2) * (py - y2) -
                   static_cast<int32_t>(px - x2) * (y0 - y2);

      bool inside = (area > 0) ? (w0 >= 0 && w1 >= 0 && w2 >= 0)
                               : (w0 <= 0 && w1 <= 0 && w2 <= 0);
      if (!inside)
        continue;

      // Barycentric interpolation
      int32_t absArea = std::abs(area);
      int32_t bw0 = std::abs(w1); // weight for vertex 0
      int32_t bw1 = std::abs(w2); // weight for vertex 1
      int32_t bw2 = std::abs(w0); // weight for vertex 2

      uint8_t r =
          static_cast<uint8_t>((bw0 * r0 + bw1 * r1 + bw2 * r2) / absArea);
      uint8_t g =
          static_cast<uint8_t>((bw0 * g0 + bw1 * g1 + bw2 * g2) / absArea);
      uint8_t b =
          static_cast<uint8_t>((bw0 * b0 + bw1 * b1 + bw2 * b2) / absArea);

      PutPixel(px, py, ColorToVRAM(r, g, b));
    }
  }
}

// ─── Texture Sampling ──────────────────────────────────────────────────

uint16_t PS1GPU::SampleTexture(int32_t texU, int32_t texV, uint16_t clut,
                               uint16_t texPage) const {
  // Apply texture window masking
  texU = (texU & ~(texWindowMaskX * 8)) |
         ((texWindowOffsetX & texWindowMaskX) * 8);
  texV = (texV & ~(texWindowMaskY * 8)) |
         ((texWindowOffsetY & texWindowMaskY) * 8);

  uint32_t tpX = (texPage & 0xF) * 64; // Texture page X base (in VRAM pixels)
  uint32_t tpY = ((texPage >> 4) & 1) * 256; // Texture page Y base
  uint32_t colorDepth = (texPage >> 7) & 3;  // 0=4bit, 1=8bit, 2=15bit

  uint32_t clutX = (clut & 0x3F) * 16;  // CLUT X in VRAM
  uint32_t clutY = (clut >> 6) & 0x1FF; // CLUT Y in VRAM

  if (colorDepth == 0) {
    // 4-bit color (16 colors from CLUT)
    uint32_t texelX = tpX + texU / 4;
    uint32_t texelY = tpY + texV;
    uint16_t texelWord = ReadVRAM(texelX, texelY);
    uint8_t nibble = (texelWord >> ((texU & 3) * 4)) & 0xF;
    return ReadVRAM(clutX + nibble, clutY);
  } else if (colorDepth == 1) {
    // 8-bit color (256 colors from CLUT)
    uint32_t texelX = tpX + texU / 2;
    uint32_t texelY = tpY + texV;
    uint16_t texelWord = ReadVRAM(texelX, texelY);
    uint8_t index = (texelWord >> ((texU & 1) * 8)) & 0xFF;
    return ReadVRAM(clutX + index, clutY);
  } else {
    // 15-bit direct color
    return ReadVRAM(tpX + texU, tpY + texV);
  }
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
