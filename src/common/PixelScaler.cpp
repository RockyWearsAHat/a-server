#include "common/PixelScaler.h"

#include <algorithm>
#include <cmath>

namespace AIO::Common {

static ScaledSize ComputeFitNearest(int srcW, int srcH, int targetW,
                                    int targetH) {
  ScaledSize out;
  if (srcW <= 0 || srcH <= 0 || targetW <= 0 || targetH <= 0) {
    return out;
  }

  const double sx = static_cast<double>(targetW) / static_cast<double>(srcW);
  const double sy = static_cast<double>(targetH) / static_cast<double>(srcH);
  const double s = std::min(sx, sy);

  // Use floor to guarantee we fit within the target.
  out.width = std::max(1, static_cast<int>(std::floor(srcW * s)));
  out.height = std::max(1, static_cast<int>(std::floor(srcH * s)));
  out.integerScale = 0;
  return out;
}

ScaledSize ComputeScaledSize(int srcW, int srcH, int targetW, int targetH,
                             ScaleMode mode, int integerScaleRequested) {
  ScaledSize out;
  if (srcW <= 0 || srcH <= 0 || targetW <= 0 || targetH <= 0) {
    return out;
  }

  if (mode == ScaleMode::FitNearest) {
    return ComputeFitNearest(srcW, srcH, targetW, targetH);
  }

  // IntegerNearest
  const int maxScaleW = targetW / srcW;
  const int maxScaleH = targetH / srcH;
  const int maxScale = std::min(maxScaleW, maxScaleH);

  if (maxScale <= 0) {
    // Can't even fit 1x; fall back to fit scaling (still nearest).
    return ComputeFitNearest(srcW, srcH, targetW, targetH);
  }

  int scale = maxScale;
  if (integerScaleRequested > 0) {
    scale = std::min(integerScaleRequested, maxScale);
    scale = std::max(scale, 1);
  }

  out.width = srcW * scale;
  out.height = srcH * scale;
  out.integerScale = scale;
  return out;
}

void ScaleNearestARGB32(const uint32_t *srcPixels, int srcW, int srcH,
                        int srcStride, uint32_t *dstPixels, int dstW, int dstH,
                        int dstStride) {
  if (!srcPixels || !dstPixels)
    return;
  if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
    return;

  for (int y = 0; y < dstH; ++y) {
    const int srcY = (y * srcH) / dstH;
    const uint32_t *srcRow = srcPixels + (srcY * srcStride);
    uint32_t *dstRow = dstPixels + (y * dstStride);

    for (int x = 0; x < dstW; ++x) {
      const int srcX = (x * srcW) / dstW;
      dstRow[x] = srcRow[srcX];
    }
  }
}

void ScaleIntegerNearestARGB32(const uint32_t *srcPixels, int srcW, int srcH,
                               int srcStride, uint32_t *dstPixels,
                               int integerScale, int dstStride) {
  if (!srcPixels || !dstPixels)
    return;
  if (srcW <= 0 || srcH <= 0 || integerScale <= 0)
    return;

  for (int y = 0; y < srcH; ++y) {
    const uint32_t *srcRow = srcPixels + (y * srcStride);

    for (int ky = 0; ky < integerScale; ++ky) {
      uint32_t *dstRow = dstPixels + ((y * integerScale + ky) * dstStride);

      for (int x = 0; x < srcW; ++x) {
        const uint32_t p = srcRow[x];
        uint32_t *block = dstRow + (x * integerScale);
        std::fill(block, block + integerScale, p);
      }
    }
  }
}

} // namespace AIO::Common
