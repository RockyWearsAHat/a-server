#pragma once

#include <cstdint>
#include <vector>

namespace GBEmulator {

class GBMemory;

// Pixel Processing Unit - tilemap-based video output
class GBPPU {
 public:
  explicit GBPPU(GBMemory* memory);
  ~GBPPU() = default;

  // Tick PPU by one master cycle (accumulates to line/frame counts)
  void Tick();

  // Video output
  const uint32_t* GetFramebuffer() const { return framebuffer_.data(); }
  static constexpr uint32_t kFramebufferWidth = 160;
  static constexpr uint32_t kFramebufferHeight = 144;

  // Frame counter (increments at end of each VBlank period)
  uint32_t GetFrameCount() const { return frame_count_; }

  // Register I/O (0xFF40–0xFF4A roughly)
  uint8_t ReadReg(uint8_t reg_offset);
  void WriteReg(uint8_t reg_offset, uint8_t val);

  // State save/restore
  struct State {
    uint32_t cycle_accumulator;
    uint16_t h_counter;
    uint16_t v_counter;
    uint32_t frame_count;
    uint8_t lcdc;      // Control register
    uint8_t stat;      // Status register
    std::vector<uint32_t> framebuffer;
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  // Timing: 456 cycles per line, 154 lines per frame
  uint32_t cycle_accumulator_;  // Accumulates master cycles
  uint16_t h_counter_;          // Horizontal position (0–455)
  uint16_t v_counter_;          // Vertical position (0–153)
  uint32_t frame_count_;        // Frame counter

  // Registers
  uint8_t lcdc_;      // 0xFF40: Control (enable, BG map, tile data, etc.)
  uint8_t stat_;      // 0xFF41: Status (mode, interrupt flags)
  uint8_t scy_;       // 0xFF42: Scroll Y
  uint8_t scx_;       // 0xFF43: Scroll X
  uint8_t ly_;        // 0xFF44: Scanline (read-only)
  uint8_t lyc_;       // 0xFF45: Scanline compare
  uint8_t dma_;       // 0xFF46: DMA transfer (sprite copy)
  uint8_t bgp_;       // 0xFF47: Background palette
  uint8_t obp0_;      // 0xFF48: Sprite palette 0
  uint8_t obp1_;      // 0xFF49: Sprite palette 1
  uint8_t wy_;        // 0xFF4A: Window Y
  uint8_t wx_;        // 0xFF4B: Window X

  // Framebuffer: 160×144 RGBA8 (one uint32_t per pixel)
  std::vector<uint32_t> framebuffer_;

  GBMemory* memory_;  // Unowned reference

  // Helper: render current pixel based on tilemap + scroll
  uint32_t GetPixelColor(uint8_t x, uint8_t y);
};

}  // namespace GBEmulator
