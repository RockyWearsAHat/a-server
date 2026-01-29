#!/usr/bin/env python3
"""Analyze a PPM file for pixel statistics."""
import sys

def analyze_ppm(path):
    with open(path, 'rb') as f:
        header = f.readline().decode().strip()  # P6
        dims = f.readline().decode().strip()
        maxval = f.readline().decode().strip()
        data = f.read()

    width, height = map(int, dims.split())
    print(f"Image: {width}x{height}, maxval={maxval}")
    print(f"Data bytes: {len(data)}")

    total = width * height
    nonblack = 0
    black = 0
    color_counts = {}

    for i in range(0, len(data), 3):
        r, g, b = data[i], data[i+1], data[i+2]
        if r == 0 and g == 0 and b == 0:
            black += 1
        else:
            nonblack += 1
        color = (r, g, b)
        color_counts[color] = color_counts.get(color, 0) + 1

    print(f"Total pixels: {total}")
    print(f"Black pixels: {black} ({100*black/total:.2f}%)")
    print(f"Non-black pixels: {nonblack} ({100*nonblack/total:.2f}%)")
    print(f"Unique colors: {len(color_counts)}")
    print(f"\nTop 10 colors:")
    for color, count in sorted(color_counts.items(), key=lambda x: -x[1])[:10]:
        r, g, b = color
        pct = 100*count/total
        print(f"  RGB({r:3}, {g:3}, {b:3}): {count:6} ({pct:5.2f}%)")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <ppm-file>")
        sys.exit(1)
    analyze_ppm(sys.argv[1])
