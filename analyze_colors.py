#!/usr/bin/env python3
# Analyze the screen to identify corruption patterns
with open('ogdk_test_run.ppm', 'rb') as f:
    f.readline()  # P6
    f.readline()  # dims
    f.readline()  # maxval
    pixels = f.read()

w, h = 240, 160

# Count how many pixels have specific colors
color_counts = {}
for i in range(0, len(pixels), 3):
    c = (pixels[i], pixels[i+1], pixels[i+2])
    color_counts[c] = color_counts.get(c, 0) + 1

print('Color distribution:')
for c, count in sorted(color_counts.items(), key=lambda x: -x[1]):
    pct = 100 * count / (w * h)
    print(f'  RGB{c}: {count:5d} pixels ({pct:5.1f}%)')

# Check for horizontal/vertical banding (sign of tilemap issues)
print()
print('Row color consistency (looking for tilemap issues):')
for y in range(0, 160, 16):
    row_colors = set()
    for x in range(240):
        idx = (y * w + x) * 3
        row_colors.add((pixels[idx], pixels[idx+1], pixels[idx+2]))
    print(f'  Row {y:3d}: {len(row_colors)} unique colors')
