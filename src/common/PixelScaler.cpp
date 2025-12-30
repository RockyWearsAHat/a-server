#include "common/PixelScaler.h"

#include <algorithm>
#include <cmath>

namespace AIO::Common {

ScaleResult ComputeScaledSize(int srcW, int srcH, int dstW, int dstH,
                              ScaleMode mode, int requestedIntegerScale) {
  ScaleResult out;
  if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
    return out;

  if (mode == ScaleMode::IntegerNearest) {
    const int sx = dstW / srcW;
    const int sy = dstH / srcH;
    const int maxScale = std::min(sx, sy);

    // If the window is smaller than the source resolution, integer scaling
    // cannot fit. Fall back to best-fit nearest (non-integer) to avoid
    // clipping.
    if (maxScale < 1) {
      const double scaleX =
          static_cast<double>(dstW) / static_cast<double>(srcW);
      const double scaleY =
          static_cast<double>(dstH) / static_cast<double>(srcH);
      const double s = std::max(0.0, std::min(scaleX, scaleY));

      out.width = std::max(1, static_cast<int>(std::lround(srcW * s)));
      out.height = std::max(1, static_cast<int>(std::lround(srcH * s)));
      out.integerScale = 0; // signal: not an exact integer scale
      return out;
    }

    int scale = requestedIntegerScale;
    if (scale <= 0) {
      scale = maxScale; // auto: largest integer that fits
    } else {
      scale = std::min(std::max(1, scale), maxScale); // clamp to fit
    }

    out.integerScale = scale;
    out.width = srcW * scale;
    out.height = srcH * scale;
    return out;
  }

  // FitNearest (preserve aspect ratio)
  const double scaleX = static_cast<double>(dstW) / static_cast<double>(srcW);
  const double scaleY = static_cast<double>(dstH) / static_cast<double>(srcH);
  const double s = std::max(0.0, std::min(scaleX, scaleY));

  const int w = std::max(1, static_cast<int>(std::lround(srcW * s)));
  const int h = std::max(1, static_cast<int>(std::lround(srcH * s)));

  out.width = w;
  out.height = h;
  out.integerScale = 1;
  return out;
}

void ScaleNearestARGB32(const uint32_t *srcPixels, int srcW, int srcH,
                        int srcStridePixels, uint32_t *dstPixels, int dstW,
                        int dstH, int dstStridePixels) {
  if (!srcPixels || !dstPixels)
    return;
  if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
    return;

  // Map dst pixel centers back to src via integer division.
  for (int y = 0; y < dstH; ++y) {
    const int sy = std::min(srcH - 1, (y * srcH) / dstH);
    const uint32_t *srcRow = srcPixels + sy * srcStridePixels;
    uint32_t *dstRow = dstPixels + y * dstStridePixels;

    for (int x = 0; x < dstW; ++x) {
      const int sx = std::min(srcW - 1, (x * srcW) / dstW);
      dstRow[x] = srcRow[sx];
    }
  }
}

void ScaleIntegerNearestARGB32(const uint32_t *srcPixels, int srcW, int srcH,
                               int srcStridePixels, uint32_t *dstPixels,
                               int integerScale, int dstStridePixels) {
  if (!srcPixels || !dstPixels)
    return;
  if (srcW <= 0 || srcH <= 0)
    return;
  if (integerScale <= 0)
    return;

  for (int y = 0; y < srcH; ++y) {
    const uint32_t *srcRow = srcPixels + y * srcStridePixels;
    for (int ry = 0; ry < integerScale; ++ry) {
      uint32_t *dstRow = dstPixels + (y * integerScale + ry) * dstStridePixels;
      for (int x = 0; x < srcW; ++x) {
        const uint32_t px = srcRow[x];
        const int dx = x * integerScale;
        for (int rx = 0; rx < integerScale; ++rx) {
          dstRow[dx + rx] = px;
        }
      }
    }
  }
}

} // namespace AIO::Common
