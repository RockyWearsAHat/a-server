#pragma once

#include <cstdint>

namespace AIO::Common {

enum class ScaleMode {
  IntegerNearest, // Integer scale factor (pixel perfect)
  FitNearest      // Best-fit into target rect using nearest-neighbor
};

struct ScaleResult {
  int width = 0;
  int height = 0;
  int integerScale = 1; // For IntegerNearest; otherwise best-effort
};

// Computes the scaled output size to fit inside (dstW,dstH) while preserving
// aspect ratio. For IntegerNearest: uses integer scaling; for FitNearest: uses
// best-fit (non-integer).
ScaleResult ComputeScaledSize(int srcW, int srcH, int dstW, int dstH,
                              ScaleMode mode, int requestedIntegerScale = 0);

// Nearest-neighbor scaler for packed ARGB32 pixels.
// Strides are in pixels (not bytes).
void ScaleNearestARGB32(const uint32_t *srcPixels, int srcW, int srcH,
                        int srcStridePixels, uint32_t *dstPixels, int dstW,
                        int dstH, int dstStridePixels);

// Fast path for exact integer scaling.
void ScaleIntegerNearestARGB32(const uint32_t *srcPixels, int srcW, int srcH,
                               int srcStridePixels, uint32_t *dstPixels,
                               int integerScale, int dstStridePixels);

} // namespace AIO::Common
