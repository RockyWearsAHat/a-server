#!/usr/bin/env python3
"""Analyze OG-DK PPM frame for copyright and M character rendering."""

import sys

def main():
    ppm_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ogdk_current.ppm"
    
    with open(ppm_path, "rb") as f:
        magic = f.readline()
        dims = f.readline().split()
        w, h = int(dims[0]), int(dims[1])
        maxval = f.readline()
        data = f.read()

    def get_pixel(x, y):
        off = (y * w + x) * 3
        return data[off], data[off+1], data[off+2]

    # Copyright area
    print("=== Copyright area y=144-155, x=16-224 ===")
    for y in range(144, 156):
        row = ""
        for x in range(16, 224):
            r, g, b = get_pixel(x, y)
            if r == 0 and g == 0 and b == 0:
                row += "."
            else:
                row += "#"
        if "#" in row:
            print(f"y={y}: {row}")

    # M character area
    print()
    print("=== Menu text y=96-108, x=40-120 ===")
    for y in range(96, 108):
        row = ""
        for x in range(40, 120):
            r, g, b = get_pixel(x, y)
            if r == 0 and g == 0 and b == 0:
                row += "."
            else:
                row += "#"
        if "#" in row:
            print(f"y={y}: {row}")

    # Zoom in on first M character
    print()
    print("=== First M character detail (y=96-106, x=48-64) ===")
    for y in range(96, 106):
        row = ""
        for x in range(48, 64):
            r, g, b = get_pixel(x, y)
            if r == 0 and g == 0 and b == 0:
                row += "."
            else:
                row += "#"
        if "#" in row:
            print(f"y={y}: {row}")

    # Look for copyright symbol detail
    print()
    print("=== Copyright symbol area y=140-156 (full width) ===")
    for y in range(140, 156):
        has_content = False
        for x in range(0, 240):
            r, g, b = get_pixel(x, y)
            if not (r == 0 and g == 0 and b == 0):
                has_content = True
                break
        if has_content:
            row = ""
            for x in range(0, 240):
                r, g, b = get_pixel(x, y)
                if r == 0 and g == 0 and b == 0:
                    row += "."
                else:
                    row += "#"
            print(f"y={y}: {row.rstrip('.')}")

    # Unique colors in the copyright area
    print()
    colors = set()
    for y in range(140, 156):
        for x in range(0, 240):
            r, g, b = get_pixel(x, y)
            if not (r == 0 and g == 0 and b == 0):
                colors.add((r, g, b))
    print(f"Unique non-black colors in copyright area: {colors}")

    # Full bottom area
    print()
    print("=== Full bottom area y=80-159 (non-empty rows only) ===")
    for y in range(80, 160):
        has_content = False
        for x in range(0, 240):
            r, g, b = get_pixel(x, y)
            if not (r == 0 and g == 0 and b == 0):
                has_content = True
                break
        if has_content:
            row = ""
            for x in range(0, 240):
                r, g, b = get_pixel(x, y)
                if r == 0 and g == 0 and b == 0:
                    row += "."
                else:
                    row += "#"
            print(f"y={y}: {row.rstrip('.')}")

if __name__ == "__main__":
    main()
