#!/usr/bin/env python3
import sys

def read_ppm_raw(path):
    with open(path, 'rb') as f:
        magic = f.readline()
        dims = f.readline()
        maxval = f.readline()
        data = f.read()
    w, h = map(int, dims.split())
    return w, h, data

path = sys.argv[1] if len(sys.argv) > 1 else "current_ogdk.ppm"
w, h, data = read_ppm_raw(path)
print(f"Image: {w}x{h}")

red_pixels = []
yellow_pixels = []

for y in range(h):
    for x in range(w):
        idx = (y * w + x) * 3
        r, g, b = data[idx], data[idx+1], data[idx+2]
        
        # Red: high R, low G, low B
        if r > 200 and g < 50 and b < 50:
            red_pixels.append((x, y, r, g, b))
        
        # Yellow: high R, high G, low B
        if r > 200 and g > 200 and b < 50:
            yellow_pixels.append((x, y, r, g, b))

print(f"\nRed pixels ({len(red_pixels)} total):")
for p in red_pixels[:20]:
    print(f"  x={p[0]:3}, y={p[1]:3}  RGB({p[2]}, {p[3]}, {p[4]})")
if len(red_pixels) > 20:
    print(f"  ... and {len(red_pixels) - 20} more")

print(f"\nYellow pixels ({len(yellow_pixels)} total):")
for p in yellow_pixels[:20]:
    print(f"  x={p[0]:3}, y={p[1]:3}  RGB({p[2]}, {p[3]}, {p[4]})")
if len(yellow_pixels) > 20:
    print(f"  ... and {len(yellow_pixels) - 20} more")
