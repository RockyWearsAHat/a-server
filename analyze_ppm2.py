#!/usr/bin/env python3
# Analyze the OG-DK PPM to understand the visual pattern
import numpy as np

with open('current_ogdk.ppm', 'rb') as f:
    assert f.readline().strip() == b'P6'
    dims = f.readline().strip().split()
    w, h = int(dims[0]), int(dims[1])
    assert f.readline().strip() == b'255'
    data = np.frombuffer(f.read(), dtype=np.uint8).reshape((h, w, 3))

# Get unique colors and their counts
colors = {}
for y in range(h):
    for x in range(w):
        c = tuple(data[y, x])
        colors[c] = colors.get(c, 0) + 1

print(f"Image size: {w}x{h}")
print(f"Total unique colors: {len(colors)}")
print("\nTop 10 colors by frequency:")
for i, (color, count) in enumerate(sorted(colors.items(), key=lambda x: -x[1])[:10]):
    pct = 100 * count / (w * h)
    print(f"  {i+1}. RGB{color}: {count} pixels ({pct:.1f}%)")

# Check if image is mostly one color (screen stuck)
most_common_color, most_common_count = max(colors.items(), key=lambda x: x[1])
print(f"\nMost common color: RGB{most_common_color} = {100*most_common_count/(w*h):.1f}% of image")

# Check for recognizable patterns
red_pixels = sum(1 for c in colors if c[0] > 200 and c[1] < 100 and c[2] < 100)
print(f"\nRed-ish pixels (barrels?): {red_pixels} unique colors")

blue_pixels = sum(1 for c in colors if c[2] > 200 and c[0] < 200)
print(f"Blue-ish pixels (girders?): {blue_pixels} unique colors")
