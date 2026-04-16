#pragma once

#include <cstdint>
#include <vector>

namespace SaturnEmulator {

class SaturnMemory;

// VDP2 — Sega Saturn scroll-plane compositor (backgrounds, colour palettes, sprites overlay).
// Composites VDP1 output with up to 4 NBG scroll planes and 2 RBG rotation planes.
class VDP2 {
 public:
  explicit VDP2(SaturnMemory* memory);
  ~VDP2() = default;

  void Reset();

  // Called each scanline and at frame end
  void TickScanline(uint16_t y);

  // Frame counter (incremented each VBlank)
  uint32_t GetFrameCount() const { return frame_count_; }
  void IncrementFrame() { frame_count_++; }

  // Register I/O
  uint16_t ReadReg (uint32_t reg_offset);
  void     WriteReg(uint32_t reg_offset, uint16_t val);

  // VRAM and colour RAM
  uint16_t ReadVram (uint32_t offset);
  void     WriteVram(uint32_t offset, uint16_t val);
  uint16_t ReadCram (uint32_t offset);
  void     WriteCram(uint32_t offset, uint16_t val);

  // State
  struct State {
    std::vector<uint8_t>  vram;
    std::vector<uint8_t>  cram;
    std::vector<uint16_t> regs;
    uint32_t frame_count;
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  std::vector<uint8_t>  vram_;    // 512 KB VDP2 VRAM
  std::vector<uint8_t>  cram_;    // 4 KB colour RAM
  std::vector<uint16_t> regs_;    // VDP2 registers (128 × 16-bit)
  uint32_t frame_count_;

  SaturnMemory* memory_;
};

}  // namespace SaturnEmulator
