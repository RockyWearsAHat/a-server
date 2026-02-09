#!/usr/bin/env python3
"""Compare PPM frame dumps to identify flickering pixels."""

import sys
import os

def read_ppm(path):
    with open(path, 'rb') as f:
        magic = f.readline().strip()
        assert magic in (b'P6', b'P3'), f"Unexpected PPM magic: {magic}"
        while True:
            line = f.readline().strip()
            if not line.startswith(b'#'):
                break
        w, h = map(int, line.split())
        maxval = int(f.readline().strip())
        data = f.read()
    return w, h, data

def compare_frames(path1, path2):
    w1, h1, d1 = read_ppm(path1)
    w2, h2, d2 = read_ppm(path2)
    
    if w1 != w2 or h1 != h2:
        print(f"  Dimension mismatch: {w1}x{h1} vs {w2}x{h2}")
        return
    
    diffs = []
    for y in range(h1):
        for x in range(w1):
            idx = (y * w1 + x) * 3
            if idx + 2 >= len(d1) or idx + 2 >= len(d2):
                continue
            r1, g1, b1 = d1[idx], d1[idx+1], d1[idx+2]
            r2, g2, b2 = d2[idx], d2[idx+1], d2[idx+2]
            if r1 != r2 or g1 != g2 or b1 != b2:
                diffs.append((x, y, (r1,g1,b1), (r2,g2,b2)))
    
    return diffs

def main():
    timestamps = [1000, 1500, 2000, 2500, 3000]
    files = [f'/tmp/ogdk_frame_{ms}.ppm' for ms in timestamps]
    
    existing = [(ms, f) for ms, f in zip(timestamps, files) if os.path.exists(f)]
    print(f"Found {len(existing)} frame files")
    
    for ms, f in existing:
        w, h, d = read_ppm(f)
        print(f"  {ms}ms: {w}x{h}, {len(d)} bytes")
    
    for i in range(len(existing) - 1):
        ms1, f1 = existing[i]
        ms2, f2 = existing[i + 1]
        diffs = compare_frames(f1, f2)
        print(f"\n{ms1}ms vs {ms2}ms: {len(diffs)} different pixels")
        if diffs:
            rows = sorted(set(d[1] for d in diffs))
            cols = sorted(set(d[0] for d in diffs))
            print(f"  Row range: {min(rows)}-{max(rows)}")
            print(f"  Col range: {min(cols)}-{max(cols)}")
            print(f"  Affected rows ({len(rows)}): {rows[:30]}")
            print(f"  Affected cols ({len(cols)}): {cols[:30]}")
            # Show first 20 pixel changes
            print(f"  Sample pixel changes:")
            for x, y, c1, c2 in diffs[:20]:
                print(f"    ({x},{y}): RGB{c1} -> RGB{c2}")

if __name__ == '__main__':
    main()
