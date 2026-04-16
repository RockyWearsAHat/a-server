#pragma once

#include <cstdint>
#include <vector>

namespace GameCubeEmulator {

class GameCubeMemory;

// Flipper GPU — TEV (Texture Environment Unit) tile renderer.
// Receives commands via the GP FIFO write-gather buffer.
// EFB (Embedded Frame Buffer) is 640×480 RGBA8 internally.
class Flipper {
 public:
  explicit Flipper(GameCubeMemory* memory);
  ~Flipper() = default;

  void Reset();

  // GP FIFO command submission (write-gather word)
  void WriteFifo(uint32_t val);

  // Register I/O (0xCC000000 range)
  uint32_t ReadReg (uint32_t reg_offset);
  void     WriteReg(uint32_t reg_offset, uint32_t val);

  // Frame output (640×480 RGBA8)
  const uint32_t* GetFramebuffer() const { return efb_.data(); }
  static constexpr uint32_t kWidth  = 640;
  static constexpr uint32_t kHeight = 480;

  uint32_t GetFrameCount() const { return frame_count_; }
  void     IncrementFrame() { frame_count_++; }

  // State
  struct State {
    std::vector<uint32_t> efb;
    std::vector<uint32_t> regs;
    uint32_t frame_count;
  };

  State SaveState() const;
  void  LoadState(const State& state);

 private:
  std::vector<uint32_t> efb_;           // 640×480 RGBA8
  std::vector<uint32_t> regs_;          // Flipper registers (256 × 32-bit)
  uint32_t frame_count_;

  GameCubeMemory* memory_;
};

}  // namespace GameCubeEmulator
