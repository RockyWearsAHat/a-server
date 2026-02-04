#!/usr/bin/env python3
"""Analyze OG-DK frame dumps to understand visual output."""

import sys

def read_ppm(path):
    """Read a PPM file and return width, height, and pixel data."""
    with open(path, 'rb') as f:
        header = f.readline()  # P6
        # Skip comments
        while True:
            line = f.readline()
            if not line.startswith(b'#'):
                break
        dims = line.split()
        width, height = int(dims[0]), int(dims[1])
        maxval = f.readline()
        pixels = f.read()
    return width, height, pixels

def pixel_to_char(r, g, b):
    """Map pixel color to ASCII character."""
    if r == 0 and g == 0 and b == 0:
        return ' '  # black
    elif r == 104 and g == 104 and b == 104:
        return 'd'  # dark gray 686868
    elif r == 152 and g == 152 and b == 152:
        return 'l'  # light gray 989898
    elif r == 128 and g == 248 and b == 248:
        return 'C'  # cyan 80F8F8
    else:
        return '?'

def analyze_frame(path):
    """Analyze and display a frame."""
    w, h, pixels = read_ppm(path)
    print(f"Image: {w}x{h}, {len(pixels)} bytes")
    
    # Count unique colors
    colors = {}
    for i in range(0, len(pixels), 3):
        c = (pixels[i], pixels[i+1], pixels[i+2])
        colors[c] = colors.get(c, 0) + 1
    
    print(f"\nUnique colors: {len(colors)}")
    for c, count in sorted(colors.items(), key=lambda x: -x[1]):
        pct = count * 100 / (w * h)
        print(f"  RGB({c[0]:3d}, {c[1]:3d}, {c[2]:3d}) = #{c[0]:02X}{c[1]:02X}{c[2]:02X}  {count:6d} pixels ({pct:5.1f}%)")
    
    # Print ASCII visualization (every other row to fit terminal)
    print(f"\n=== ASCII visualization (every 2nd row) ===")
    for y in range(0, h, 2):
        line = ""
        for x in range(w):
            idx = (y * w + x) * 3
            r, g, b = pixels[idx], pixels[idx+1], pixels[idx+2]
            line += pixel_to_char(r, g, b)
        print(line)

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ogdk_2000ms.ppm"
    analyze_frame(path)
