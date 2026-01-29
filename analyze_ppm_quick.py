#!/usr/bin/env python3
import sys

fname = sys.argv[1] if len(sys.argv) > 1 else 'test_ogdk.ppm'

with open(fname, 'rb') as f:
    f.readline()  # P6
    f.readline()  # 240 160
    f.readline()  # 255
    data = f.read()

colors = {}
for i in range(0, len(data), 3):
    r, g, b = data[i], data[i+1], data[i+2]
    colors[(r,g,b)] = colors.get((r,g,b), 0) + 1

sorted_c = sorted(colors.items(), key=lambda x: -x[1])[:10]
print(f"Top 10 colors in {fname}:")
for (r,g,b), c in sorted_c:
    print(f'  RGB({r:3},{g:3},{b:3}): {c:5} ({100.0*c/38400:.1f}%)')
