#pragma once

#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/PPU.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace AIO::Emulator::GBA::Test {

// Timing constants used throughout these unit tests.
// Visible portion of a scanline is 960 cycles in this emulator.
static constexpr int kCyclesToHBlankStart = 960;
static constexpr int kCyclesPerScanline = 1232;

inline uint32_t ARGBFromBGR555(uint16_t bgr555) {
  const uint8_t r = static_cast<uint8_t>((bgr555 & 0x1Fu) << 3);
  const uint8_t g = static_cast<uint8_t>(((bgr555 >> 5) & 0x1Fu) << 3);
  const uint8_t b = static_cast<uint8_t>(((bgr555 >> 10) & 0x1Fu) << 3);
  return 0xFF000000u | (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}

inline void WriteOam16(GBAMemory &mem, uint32_t oamByteOffset, uint16_t value) {
  mem.Write16(0x07000000u + oamByteOffset, value);
}

// Writes a single byte into VRAM by using a halfword write (avoids region-specific
// byte-write behavior).
inline void WriteVramPackedByteViaHalfword(GBAMemory &mem, uint32_t address,
                                          uint8_t value) {
  const uint32_t aligned = address & ~1u;
  const uint16_t cur = mem.Read16(aligned);
  uint16_t next = cur;
  if ((address & 1u) == 0u) {
    next = static_cast<uint16_t>((cur & 0xFF00u) | value);
  } else {
    next = static_cast<uint16_t>((cur & 0x00FFu) | (static_cast<uint16_t>(value) << 8));
  }
  mem.Write16(aligned, next);
}

inline void RenderToScanlineHBlank(PPU &ppu, int scanline) {
  static std::unordered_map<uint64_t, int> lastScanline;

  const uint64_t key = ppu.GetInstanceId();
  const auto it = lastScanline.find(key);

  int cycles = 0;
  if (it == lastScanline.end()) {
    cycles = scanline * kCyclesPerScanline + kCyclesToHBlankStart;
  } else {
    const int prev = it->second;
    if (scanline >= prev) {
      cycles = (scanline - prev) * kCyclesPerScanline;
    } else {
      // Tests in this suite should be monotonic; if not, just render forward by 0.
      cycles = 0;
    }
  }

  lastScanline[key] = scanline;
  if (cycles > 0) {
    ppu.Update(cycles);
  }
  ppu.SwapBuffers();
}

inline uint32_t GetPixel(const PPU &ppu, int x, int y) {
  const auto &fb = ppu.GetFramebuffer();
  if (x < 0 || y < 0 || x >= PPU::SCREEN_WIDTH || y >= PPU::SCREEN_HEIGHT) {
    return 0;
  }
  const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(PPU::SCREEN_WIDTH) +
                     static_cast<size_t>(x);
  if (idx >= fb.size()) return 0;
  return fb[idx];
}

} // namespace AIO::Emulator::GBA::Test
