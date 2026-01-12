#pragma once

#include <cstdint>

namespace AIO::Common {

enum class ScaleMode {
  IntegerNearest,
  FitNearest,
};

struct ScaledSize {
  int width = 0;
  int height = 0;
  int integerScale = 0; // >0 only when using IntegerNearest
};

// Computes output size for nearest-neighbor scaling.
// - For IntegerNearest: chooses an integer scale factor (or clamps requested
// one).
// - For FitNearest: chooses the largest fit preserving aspect ratio (may be
// non-integer). integerScaleRequested: 0 = auto.
ScaledSize ComputeScaledSize(int srcW, int srcH, int targetW, int targetH,
                             ScaleMode mode, int integerScaleRequested);

// Nearest-neighbor scaling for ARGB32 pixels.
// srcStride/dstStride are in pixels (not bytes).
void ScaleNearestARGB32(const uint32_t *srcPixels, int srcW, int srcH,
                        int srcStride, uint32_t *dstPixels, int dstW, int dstH,
                        int dstStride);

// Fast integer nearest-neighbor scaling for ARGB32.
// dst size must be (srcW*integerScale, srcH*integerScale).
void ScaleIntegerNearestARGB32(const uint32_t *srcPixels, int srcW, int srcH,
                               int srcStride, uint32_t *dstPixels,
                               int integerScale, int dstStride);

} // namespace AIO::Common
