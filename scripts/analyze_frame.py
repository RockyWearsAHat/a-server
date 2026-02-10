#!/usr/bin/env python3
"""Analyze a PPM frame dump to show rendered characters as ASCII art."""
import sys

def read_ppm(path):
    with open(path, 'rb') as f:
        magic = f.readline().strip()
        # Skip comments
        line = f.readline()
        while line.startswith(b'#'):
            line = f.readline()
        w, h = map(int, line.split())
        maxval = int(f.readline().strip())
        data = f.read()
    return w, h, data

def pixel(data, w, x, y):
    idx = (y * w + x) * 3
    return data[idx], data[idx+1], data[idx+2]

def is_lit(data, w, x, y):
    r, g, b = pixel(data, w, x, y)
    return r > 10 or g > 10 or b > 10

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else '/tmp/ogdk_frame.ppm'
    w, h, data = read_ppm(path)
    print(f"Frame: {w}x{h}, data size: {len(data)}")

    # Show M character at x=152-159, y=96-103 (menu text "GAME")
    print("\nM character (x=152-159, y=96-103):")
    for y in range(96, 104):
        row = ''.join('#' if is_lit(data, w, x, y) else '.' for x in range(152, 160))
        print(f"  y={y}: {row}")

    # Show full menu text area "1 PLAYER GAME A" at y=96-103
    print("\nMenu line 1 (x=64-184, y=96-103):")
    for y in range(96, 104):
        row = ''.join('#' if is_lit(data, w, x, y) else '.' for x in range(64, 184))
        print(f"  y={y}: {row}")

    # Show title area DONKEY KONG
    print("\nTitle area DONKEY KONG (y=0-39, full width):")
    for y in range(0, 40):
        has = any(is_lit(data, w, x, y) for x in range(0, 240))
        if not has:
            continue
        row = ''.join('#' if is_lit(data, w, x, y) else '.' for x in range(0, 240))
        print(f"  y={y}: {row}")

    # Bottom area check (copyright?)
    print("\nBottom area (y=148-159):")
    for y in range(148, 160):
        has = any(is_lit(data, w, x, y) for x in range(0, 240))
        if has:
            row = ''.join('#' if is_lit(data, w, x, y) else '.' for x in range(0, 240))
            print(f"  y={y}: {row}")
        else:
            print(f"  y={y}: (black)")

    # OBJ60 at y=86, x=48 (selection cursor?)
    print("\nOBJ60 area (x=48-55, y=86-93):")
    for y in range(86, 94):
        row = ''.join('#' if is_lit(data, w, x, y) else '.' for x in range(48, 56))
        print(f"  y={y}: {row}")

    # Check unique colors in the M region
    print("\nUnique colors in M region:")
    colors = set()
    for y in range(96, 104):
        for x in range(152, 160):
            colors.add(pixel(data, w, x, y))
    for c in sorted(colors):
        print(f"  RGB({c[0]}, {c[1]}, {c[2]})")

if __name__ == '__main__':
    main()
