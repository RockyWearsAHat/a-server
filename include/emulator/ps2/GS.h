#pragma once

#include <cstdint>
#include <vector>

namespace PS2Emulator {

class PS2Memory;

// GS — Graphics Synthesizer.
// Fixed-function triangle rasterizer with 4 MB internal VRAM.
// Receives draw commands via GIF (Graphics Interface) DMA packets.
class GS {
 public:
  explicit GS(PS2Memory* memory);
  ~GS() = default;

  void Reset();

  // GIF packet submission (A+D format: 64-bit data + 64-bit addr/regs)
  void WriteGifPacket(uint64_t data, uint64_t addr);

  // Privileged register I/O (0x12000000 range)
  uint64_t ReadPrivReg (uint32_t offset);
  void     WritePrivReg(uint32_t offset, uint64_t val);

  // Frame output (640×448 RGBA8 scaffold)
  const uint32_t* GetFramebuffer() const { return framebuffer_.data(); }
  static constexpr uint32_t kWidth  = 640;
  static constexpr uint32_t kHeight = 448;

  uint32_t GetFrameCount() const { return frame_count_; }
  void     IncrementFrame() { frame_count_++; }

  // VRAM access
  uint8_t  ReadVram8 (uint32_t offset);
  void     WriteVram8(uint32_t offset, uint8_t val);

  // State
  struct State {
    std::vector<uint8_t>  vram;
    std::vector<uint32_t> framebuffer;
    std::vector<uint64_t> priv_regs;
    uint32_t frame_count;
  };

  State SaveState() const;
  void  LoadState(const State& state);

 private:
  std::vector<uint8_t>  vram_;
  std::vector<uint32_t> framebuffer_;
  std::vector<uint64_t> priv_regs_;    // 64 × 64-bit privileged regs
  uint32_t frame_count_;

  PS2Memory* memory_;
};

}  // namespace PS2Emulator
