#pragma once

#include <cstdint>

#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/PPU.h"

namespace AIO::Emulator::GBA::Test {

static constexpr int kCyclesPerScanline = 1232;
static constexpr int kCyclesToHBlankStart = 960;

inline uint32_t ARGBFromBGR555(uint16_t bgr555) {
  const uint8_t r = (uint8_t)((bgr555 & 0x1F) << 3);
  const uint8_t g = (uint8_t)(((bgr555 >> 5) & 0x1F) << 3);
  const uint8_t b = (uint8_t)(((bgr555 >> 10) & 0x1F) << 3);
  return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

inline void WriteOam16(GBAMemory &mem, uint32_t oamOffset, uint16_t value) {
  mem.Write16(0x07000000u + oamOffset, value);
}

inline void WriteVramPackedByteViaHalfword(GBAMemory &mem, uint32_t vramAddr,
                                           uint8_t value) {
  const uint32_t aligned = vramAddr & ~1u;
  const bool high = (vramAddr & 1u) != 0;
  const uint16_t existing = mem.Read16(aligned);
  const uint16_t merged =
      high ? (uint16_t)((existing & 0x00FFu) | ((uint16_t)value << 8))
           : (uint16_t)((existing & 0xFF00u) | (uint16_t)value);
  mem.Write16(aligned, merged);
}

inline void RenderToScanlineHBlank(PPU &ppu, int scanline) {
  // Assumes a fresh PPU at start-of-frame.
  ppu.Update(kCyclesPerScanline * scanline + kCyclesToHBlankStart);
  ppu.SwapBuffers();
}

inline uint32_t GetPixel(const PPU &ppu, int x, int y) {
  const auto &fb = ppu.GetFramebuffer();
  return fb[(size_t)y * (size_t)PPU::SCREEN_WIDTH + (size_t)x];
}

} // namespace AIO::Emulator::GBA::Test
