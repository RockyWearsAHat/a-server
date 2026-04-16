#pragma once

#include <cstdint>
#include <vector>

namespace DreamcastEmulator {

class DreamcastMemory;

// PowerVR2 CLX2 — tile-based deferred renderer.
// Receives polygon lists via the TA (Tile Accelerator) FIFO,
// renders per-tile and accumulates into the framebuffer.
class PowerVR2 {
 public:
  explicit PowerVR2(DreamcastMemory* memory);
  ~PowerVR2() = default;

  void Reset();

  // Receive a 32-byte TA command vertex/polygon packet
  void WriteTA(uint32_t addr_offset, uint32_t val);

  // Trigger render process (called by STARTRENDER register write)
  void Render();

  // Frame output (640×480 RGBA8 — or 320×240 upscaled for scaffold)
  const uint32_t* GetFramebuffer() const { return framebuffer_.data(); }
  static constexpr uint32_t kWidth  = 640;
  static constexpr uint32_t kHeight = 480;

  // Frame counter (incremented each VBLANK)
  uint32_t GetFrameCount() const { return frame_count_; }
  void IncrementFrame() { frame_count_++; }

  // Register I/O (0x005F8000 range)
  uint32_t ReadReg (uint32_t reg_offset);
  void     WriteReg(uint32_t reg_offset, uint32_t val);

  // VRAM access (8 MB)
  uint8_t  ReadVram8 (uint32_t offset);
  void     WriteVram8(uint32_t offset, uint8_t val);
  uint32_t ReadVram32(uint32_t offset);
  void     WriteVram32(uint32_t offset, uint32_t val);

  // State
  struct State {
    std::vector<uint8_t>  vram;
    std::vector<uint32_t> framebuffer;
    std::vector<uint32_t> regs;
    uint32_t frame_count;
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  std::vector<uint8_t>  vram_;         // 8 MB VRAM
  std::vector<uint32_t> framebuffer_;  // 640×480 RGBA8
  std::vector<uint32_t> regs_;         // PVR2 registers (256 × 32-bit)
  uint32_t frame_count_;

  DreamcastMemory* memory_;
};

}  // namespace DreamcastEmulator
