#pragma once

#include "PS1Constants.h"
#include "emulator/common/Loggable.h"
#include <array>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace AIO::Emulator::PS1 {

class PS1Memory;

class PS1GPU : public Common::Loggable {
public:
  explicit PS1GPU(PS1Memory &memory);
  ~PS1GPU() = default;

  void Reset();

  // ─── Register Interface ─────────────────────────────────────────────
  uint32_t ReadGPUSTAT() const;
  uint32_t ReadGPUREAD();
  void WriteGP0(uint32_t value);
  void WriteGP1(uint32_t value);

  // ─── Timing ─────────────────────────────────────────────────────────
  void Tick(uint32_t cpuCycles);
  bool InVBlank() const { return vblank; }
  uint32_t GetScanline() const { return currentScanline; }

  // ─── VRAM Access (for DMA, tests) ───────────────────────────────────
  uint16_t ReadVRAM(uint32_t x, uint32_t y) const;
  void WriteVRAM(uint32_t x, uint32_t y, uint16_t value);
  const uint16_t *GetVRAMPointer() const { return vram.data(); }
  uint16_t *GetVRAMPointer() { return vram.data(); }

  // ─── Framebuffer for display ────────────────────────────────────────
  const uint16_t *GetFramebuffer() const {
    uint32_t offset = displayVRAMStartY * GPU::VRAM_WIDTH + displayVRAMStartX;
    return vram.data() + offset;
  }
  uint32_t GetDisplayWidth() const;
  uint32_t GetDisplayHeight() const;
  uint32_t GetVRAMStride() const { return GPU::VRAM_WIDTH; }

  // ─── DMA Interface ──────────────────────────────────────────────────
  bool DMAReady() const;
  void DMAWrite(uint32_t value);
  uint32_t DMARead();

  // ─── Debug ──────────────────────────────────────────────────────────
  void DumpState(std::ostream &os) const;
  std::string GetDebugSummary() const;
  uint32_t GetGP0CommandCount() const { return gp0CommandCount; }

private:
  PS1Memory &memory;

  // ─── VRAM ───────────────────────────────────────────────────────────
  std::vector<uint16_t> vram;

  // ─── GP0 Command Buffer ─────────────────────────────────────────────
  std::vector<uint32_t> gp0CommandBuffer;
  uint32_t gp0WordsRemaining = 0;
  uint32_t gp0CommandCount = 0;

  enum class GP0Mode {
    Command,
    CopyToVRAM,
    CopyFromVRAM,
    PolyLine,
    ShadedPolyLine
  };
  GP0Mode gp0Mode = GP0Mode::Command;

  // VRAM transfer state
  uint32_t vramTransferX = 0;
  uint32_t vramTransferY = 0;
  uint32_t vramTransferW = 0;
  uint32_t vramTransferH = 0;
  uint32_t vramTransferCurrX = 0;
  uint32_t vramTransferCurrY = 0;

  // ─── GPUSTAT Fields ─────────────────────────────────────────────────
  uint8_t texPageBaseX = 0;
  uint8_t texPageBaseY = 0;
  uint8_t semiTransparencyMode = 0;
  uint8_t texPageColorDepth = 0;
  bool dither = false;
  bool drawToDisplay = false;
  bool maskBitSet = false;
  bool maskBitCheck = false;
  bool interlaceField = false;
  bool reverseFlag = false;
  bool textureDisable = false;
  uint8_t hRes = 0;
  uint8_t hRes2 = 0;
  bool vRes480 = false;
  bool palMode = false;
  bool colorDepth24 = false;
  bool interlace = false;
  bool displayDisabled = true;
  bool irq1 = false;
  uint8_t dmaDirection = 0;

  // ─── Drawing Area ───────────────────────────────────────────────────
  uint16_t drawAreaLeft = 0;
  uint16_t drawAreaTop = 0;
  uint16_t drawAreaRight = 0;
  uint16_t drawAreaBottom = 0;
  int16_t drawOffsetX = 0;
  int16_t drawOffsetY = 0;

  // ─── Display Area ───────────────────────────────────────────────────
  uint16_t displayVRAMStartX = 0;
  uint16_t displayVRAMStartY = 0;
  uint16_t displayHorizStart = 0x200;
  uint16_t displayHorizEnd = 0xC00;
  uint16_t displayVertStart = 0x10;
  uint16_t displayVertEnd = 0x100;

  // ─── Texture Window ─────────────────────────────────────────────────
  uint8_t texWindowMaskX = 0;
  uint8_t texWindowMaskY = 0;
  uint8_t texWindowOffsetX = 0;
  uint8_t texWindowOffsetY = 0;

  // ─── Polyline State ─────────────────────────────────────────────────
  uint32_t polyLineLastXY = 0;
  uint8_t polyLineLastR = 0;
  uint8_t polyLineLastG = 0;
  uint8_t polyLineLastB = 0;
  bool polyLineSemiTransparent = false;
  // Shaded-polyline pending next-segment color (replaces the unsafe static locals)
  bool shadedPolyExpectVertex = false;
  uint8_t shadedPolyPendingR = 0;
  uint8_t shadedPolyPendingG = 0;
  uint8_t shadedPolyPendingB = 0;

  // ─── Per-primitive Semi-transparency ────────────────────────────────
  bool primSemiTransparent = false;

  // ─── Timing ─────────────────────────────────────────────────────────
  uint32_t currentScanline = 0;
  uint32_t dotCounter = 0;
  bool vblank = false;
  bool oddFrame = false;

  // ─── GPU Read Buffer ────────────────────────────────────────────────
  uint32_t gpuReadBuffer = 0;

  // ─── GP0 Command Processing ─────────────────────────────────────────
  void ProcessGP0Command();
  uint32_t GP0CommandLength(uint8_t cmd) const;

  // GP0 command handlers
  void GP0_ClearCache(uint32_t cmd);
  void GP0_FillRect(const std::vector<uint32_t> &params);
  void GP0_DrawMode(uint32_t cmd);
  void GP0_TextureWindow(uint32_t cmd);
  void GP0_DrawAreaTopLeft(uint32_t cmd);
  void GP0_DrawAreaBottomRight(uint32_t cmd);
  void GP0_DrawOffset(uint32_t cmd);
  void GP0_MaskBitSetting(uint32_t cmd);
  void GP0_Nop(uint32_t cmd);

  // Rendering commands (polygons, lines, rects)
  void GP0_MonoTriangle(const std::vector<uint32_t> &params, bool opaque);
  void GP0_MonoQuad(const std::vector<uint32_t> &params, bool opaque);
  void GP0_TexturedTriangle(const std::vector<uint32_t> &params, bool opaque);
  void GP0_TexturedQuad(const std::vector<uint32_t> &params, bool opaque);
  void GP0_ShadedTriangle(const std::vector<uint32_t> &params, bool opaque);
  void GP0_ShadedQuad(const std::vector<uint32_t> &params, bool opaque);
  void GP0_ShadedTexturedTriangle(const std::vector<uint32_t> &params,
                                  bool opaque);
  void GP0_ShadedTexturedQuad(const std::vector<uint32_t> &params, bool opaque);
  void GP0_MonoRect(const std::vector<uint32_t> &params);
  void GP0_TexturedRect(const std::vector<uint32_t> &params);
  void GP0_MonoDot(const std::vector<uint32_t> &params);
  void GP0_MonoLine(const std::vector<uint32_t> &params);
  void GP0_ShadedLine(const std::vector<uint32_t> &params);

  // VRAM transfer commands
  void GP0_CopyRectVRAMtoVRAM(const std::vector<uint32_t> &params);
  void GP0_CopyRectCPUtoVRAM(const std::vector<uint32_t> &params);
  void GP0_CopyRectVRAMtoCPU(const std::vector<uint32_t> &params);

  // ─── GP1 Command Processing ─────────────────────────────────────────
  void GP1_Reset();
  void GP1_ResetCommandBuffer();
  void GP1_AckIRQ();
  void GP1_DisplayEnable(uint32_t cmd);
  void GP1_DMADirection(uint32_t cmd);
  void GP1_DisplayAreaStart(uint32_t cmd);
  void GP1_HorizontalDisplayRange(uint32_t cmd);
  void GP1_VerticalDisplayRange(uint32_t cmd);
  void GP1_DisplayMode(uint32_t cmd);
  void GP1_GetGPUInfo(uint32_t cmd);

  // ─── Rasterizer Helpers ─────────────────────────────────────────────
  void PutPixel(int16_t x, int16_t y, uint16_t color);
  bool IsInDrawingArea(int16_t x, int16_t y) const;
  static uint16_t ColorToVRAM(uint8_t r, uint8_t g, uint8_t b);
  uint16_t BlendPixels(uint16_t src, uint16_t dst) const;

  // Triangle rasterization via edge function (half-space) method
  void RasterizeTriangle(int16_t x0, int16_t y0, uint8_t r0, uint8_t g0,
                         uint8_t b0, int16_t x1, int16_t y1, uint8_t r1,
                         uint8_t g1, uint8_t b1, int16_t x2, int16_t y2,
                         uint8_t r2, uint8_t g2, uint8_t b2);

  // Texture sampling from VRAM CLUT/direct
  uint16_t SampleTexture(int32_t texU, int32_t texV, uint16_t clut,
                         uint16_t texPage) const;
};

} // namespace AIO::Emulator::PS1
