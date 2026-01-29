#!/usr/bin/env python3
"""Render a binary PPM (P6) as a small ASCII preview.

Usage:
  python3 scripts/ppm_ascii_preview.py path/to/frame.ppm [--w 96] [--h 64]

Notes:
- Pure-Python, no PIL dependency.
- Downsamples using nearest-neighbor for speed.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass


@dataclass(frozen=True)
class RGB:
    r: int
    g: int
    b: int


ASCII_RAMP = " .:-=+*#%@"  # dark -> bright


def _read_token(data: bytes, idx: int) -> tuple[bytes, int]:
    n = len(data)
    # skip whitespace and comments
    while idx < n:
        c = data[idx]
        if c in b" \t\r\n":
            idx += 1
            continue
        if c == ord("#"):
            # comment to end of line
            while idx < n and data[idx] not in b"\r\n":
                idx += 1
            continue
        break

    start = idx
    while idx < n and data[idx] not in b" \t\r\n":
        idx += 1
    return data[start:idx], idx


def load_ppm(path: str) -> tuple[int, int, bytes]:
    raw = open(path, "rb").read()
    idx = 0
    magic, idx = _read_token(raw, idx)
    if magic != b"P6":
        raise ValueError(f"Unsupported PPM magic {magic!r} (expected P6)")

    w_b, idx = _read_token(raw, idx)
    h_b, idx = _read_token(raw, idx)
    maxv_b, idx = _read_token(raw, idx)
    w = int(w_b)
    h = int(h_b)
    maxv = int(maxv_b)
    if maxv != 255:
        raise ValueError(f"Unsupported maxval {maxv} (expected 255)")

    # Skip a single whitespace char after header if present
    if idx < len(raw) and raw[idx] in b" \t\r\n":
        idx += 1

    pixel_bytes = raw[idx:]
    expected = w * h * 3
    if len(pixel_bytes) < expected:
        raise ValueError(f"Truncated pixel data: {len(pixel_bytes)} < {expected}")
    return w, h, pixel_bytes[:expected]


def luminance(rgb: RGB) -> float:
    # ITU-R BT.709
    return 0.2126 * rgb.r + 0.7152 * rgb.g + 0.0722 * rgb.b


def rgb_at(pixels: bytes, w: int, x: int, y: int) -> RGB:
    i = (y * w + x) * 3
    return RGB(pixels[i], pixels[i + 1], pixels[i + 2])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("ppm")
    ap.add_argument("--w", type=int, default=96)
    ap.add_argument("--h", type=int, default=64)
    args = ap.parse_args()

    w, h, pix = load_ppm(args.ppm)

    out_w = max(8, min(args.w, w))
    out_h = max(8, min(args.h, h))

    # Nearest-neighbor downsample
    for oy in range(out_h):
        sy = (oy * h) // out_h
        row = []
        for ox in range(out_w):
            sx = (ox * w) // out_w
            c = rgb_at(pix, w, sx, sy)
            yv = luminance(c) / 255.0
            idx = int(yv * (len(ASCII_RAMP) - 1) + 0.5)
            row.append(ASCII_RAMP[idx])
        print("".join(row))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
