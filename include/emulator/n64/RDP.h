#pragma once

#include <cstdint>
#include <vector>

namespace N64Emulator {

class N64Memory;

// Reality Display Processor — RDP (triangle rasterizer + texture mapper)
class RDP {
 public:
  explicit RDP(N64Memory* memory);
  ~RDP() = default;

  // Reset to idle state
  void Reset();

  // Execute a command from the command buffer (called by RSP DMA)
  void ProcessCommand(uint64_t cmd_word);

  // Video output
  const uint32_t* GetFramebuffer() const { return framebuffer_.data(); }
  static constexpr uint32_t kFramebufferWidth  = 320;
  static constexpr uint32_t kFramebufferHeight = 240;

  // Frame counter
  uint32_t GetFrameCount() const { return frame_count_; }
  void IncrementFrame() { frame_count_++; }

  // Register I/O
  uint32_t ReadReg(uint32_t reg_offset);
  void WriteReg(uint32_t reg_offset, uint32_t val);

  // State save/restore
  struct State {
    uint32_t frame_count;
    std::vector<uint32_t> framebuffer;
  };

  State SaveState() const;
  void LoadState(const State& state);

 private:
  std::vector<uint32_t> framebuffer_;    // 320×240 RGBA8
  uint32_t frame_count_;
  N64Memory* memory_;                    // Unowned reference
};

}  // namespace N64Emulator
