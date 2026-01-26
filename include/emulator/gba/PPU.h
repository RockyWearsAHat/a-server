#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

namespace AIO::Emulator::GBA {

class GBAMemory;

class PPU {
public:
  static const int SCREEN_WIDTH = 240;
  static const int SCREEN_HEIGHT = 160;

  PPU(GBAMemory &memory);
  ~PPU();

  void Update(int cycles);
  const std::vector<uint32_t> &GetFramebuffer() const;
  int GetFrameCount() const { return frameCount; }
  void
  SwapBuffers(); // Call this after frame complete to make it visible to GUI

  // Testing helpers
  static uint32_t ApplyBrightnessIncrease(uint32_t colorARGB, int evyRaw);
  static uint32_t ApplyBrightnessDecrease(uint32_t colorARGB, int evyRaw);

  // For frame step-back: restore a saved framebuffer
  void RestoreFramebuffer(const std::vector<uint32_t> &buffer);

  // Classic NES Series palette mapping workaround
  // These games store colors at palette indices 9-14 but use paletteBank=8
  void SetClassicNesMode(bool enabled);

  uint64_t GetInstanceId() const { return instanceId; }

private:
  void DrawScanline();
  void RenderMode0();
  void RenderMode1();
  void RenderMode2();
  void RenderMode3();
  void RenderMode4();
  void RenderMode5();
  void RenderOBJ();
  void RenderBackground(int bgIndex);
  void RenderAffineBackground(int bgIndex);

  void BuildObjWindowMaskForScanline();

  // Window helpers
  uint8_t GetWindowMaskForPixel(int x, int y);
  bool IsLayerEnabledAtPixel(int x, int y,
                             int layer); // layer 0-3=BG, 4=OBJ, 5=Effects

  // Color effects (blending/brightness)
  void ApplyColorEffects();

  uint16_t ReadRegister(uint32_t offset);

  static void OnIOWrite(void *context, uint32_t offset, uint16_t value);
  void HandleIOWrite(uint32_t offset, uint16_t value);

  GBAMemory &memory;

  // Double buffering for thread-safe framebuffer access
  std::vector<uint32_t> backBuffer;  // PPU renders to this
  std::vector<uint32_t> frontBuffer; // GUI reads from this
  mutable std::mutex bufferMutex;

  // Priority buffer: stores priority (0-3) for each pixel, 4 = backdrop
  // (lowest)
  std::vector<uint8_t> priorityBuffer;

  // Top visible layer id per pixel: 0-3=BG0-3, 4=OBJ, 5=backdrop.
  std::vector<uint8_t> layerBuffer;

  // Underlying pixel state (what was there before the topmost overwrote it).
  std::vector<uint32_t> underColorBuffer;
  std::vector<uint8_t> underLayerBuffer;

  // Marks top pixel as coming from a semi-transparent OBJ.
  std::vector<uint8_t> objSemiTransparentBuffer;
  int cycleCounter;
  int scanline;
  int frameCount;

  uint64_t instanceId{0};

  // Track previous VBlank state for edge detection
  bool prevVBlankState = false;

  // Internal Affine Counters (28-bit fixed point)
  int32_t bg2x_internal = 0;
  int32_t bg2y_internal = 0;
  int32_t bg3x_internal = 0;
  int32_t bg3y_internal = 0;

  // Per-scanline OBJ window coverage. 1=inside OBJ window.
  std::array<uint8_t, SCREEN_WIDTH> objWindowMaskLine{};

  // Classic NES Series palette mapping mode
  bool classicNesMode = false;
};

} // namespace AIO::Emulator::GBA
