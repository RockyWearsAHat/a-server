#include "emulator/gb/GBPPU.h"
#include "emulator/gb/GBMemory.h"
#include <cstring>

namespace GBEmulator {

GBPPU::GBPPU(GBMemory* memory)
    : cycle_accumulator_(0),
      h_counter_(0), v_counter_(0),
      frame_count_(0),
      lcdc_(0x91), stat_(0x00), scy_(0), scx_(0),
      ly_(0), lyc_(0), dma_(0),
      bgp_(0xFC), obp0_(0xFF), obp1_(0xFF),
      wy_(0), wx_(0),
      memory_(memory) {
  framebuffer_.resize(kFramebufferWidth * kFramebufferHeight, 0xFFFFFFFFU);
}

void GBPPU::Tick() {
  cycle_accumulator_++;

  // PPU clock: 456 cycles = 1 scanline (144 visible + 10 vblank = 154 lines per frame)
  if (cycle_accumulator_ >= 456) {
    cycle_accumulator_ = 0;
    v_counter_++;
    ly_ = v_counter_ & 0xFF;

    // End of frame
    if (v_counter_ >= 154) {
      v_counter_ = 0;
      frame_count_++;
    }
  }
}

GBPPU::State GBPPU::SaveState() const {
  return State{
    .cycle_accumulator = cycle_accumulator_,
    .h_counter = h_counter_,
    .v_counter = v_counter_,
    .frame_count = frame_count_,
    .lcdc = lcdc_,
    .stat = stat_,
    .framebuffer = framebuffer_,
  };
}

void GBPPU::LoadState(const State& state) {
  cycle_accumulator_ = state.cycle_accumulator;
  h_counter_ = state.h_counter;
  v_counter_ = state.v_counter;
  frame_count_ = state.frame_count;
  lcdc_ = state.lcdc;
  stat_ = state.stat;
  framebuffer_ = state.framebuffer;
  ly_ = state.v_counter & 0xFF;
}

uint8_t GBPPU::ReadReg(uint8_t reg_offset) {
  switch (reg_offset & 0x0F) {
    case 0x00: return lcdc_;
    case 0x01: return stat_;
    case 0x02: return scy_;
    case 0x03: return scx_;
    case 0x04: return ly_;
    case 0x05: return lyc_;
    case 0x06: return dma_;
    case 0x07: return bgp_;
    case 0x08: return obp0_;
    case 0x09: return obp1_;
    case 0x0A: return wy_;
    case 0x0B: return wx_;
    default: return 0xFF;
  }
}

void GBPPU::WriteReg(uint8_t reg_offset, uint8_t val) {
  switch (reg_offset & 0x0F) {
    case 0x00: lcdc_ = val; break;
    case 0x01: stat_ = val; break;
    case 0x02: scy_ = val; break;
    case 0x03: scx_ = val; break;
    case 0x04: ly_ = 0; v_counter_ = 0; break;  // LY write clears counter
    case 0x05: lyc_ = val; break;
    case 0x06: dma_ = val; break;  // DMA trigger (implementation deferred)
    case 0x07: bgp_ = val; break;
    case 0x08: obp0_ = val; break;
    case 0x09: obp1_ = val; break;
    case 0x0A: wy_ = val; break;
    case 0x0B: wx_ = val; break;
  }
}

uint32_t GBPPU::GetPixelColor(uint8_t x, uint8_t y) {
  // Simple gradient for now (scaffold)
  uint8_t r = x;
  uint8_t g = y;
  uint8_t b = (x + y) >> 1;
  return 0xFF000000U | (r << 16) | (g << 8) | b;
}

}  // namespace GBEmulator
