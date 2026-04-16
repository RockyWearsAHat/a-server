#pragma once

#include <cstdint>
#include <vector>

namespace SaturnEmulator {

class SaturnMemory;

// VDP1 — Sega Saturn sprite/polygon rasterizer.
// Reads command tables from VRAM and renders to one of two 16-bit framebuffers.
class VDP1 {
 public:
  explicit VDP1(SaturnMemory* memory);
  ~VDP1() = default;

  void Reset();

  // Called once per frame to render all queued VDP1 commands
  void RenderFrame();

  // Register I/O
  uint16_t ReadReg (uint32_t reg_offset);
  void     WriteReg(uint32_t reg_offset, uint16_t val);

  // VRAM and framebuffer access
  uint16_t ReadVram (uint32_t offset);
  void     WriteVram(uint32_t offset, uint16_t val);

  // Output framebuffer (320×224, RGBA8)
  const uint32_t* GetFramebuffer() const { return framebuffer_.data(); }
  static constexpr uint32_t kWidth  = 320;
  static constexpr uint32_t kHeight = 224;

  // State
  struct State {
    std::vector<uint8_t>  vram;
    std::vector<uint32_t> framebuffer;
    std::vector<uint16_t> regs;
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  std::vector<uint8_t>  vram_;         // 512 KB VDP1 VRAM
  std::vector<uint32_t> framebuffer_;  // 320×224 RGBA8 output
  std::vector<uint16_t> regs_;         // VDP1 registers (8 × 16-bit)

  SaturnMemory* memory_;
};

}  // namespace SaturnEmulator
