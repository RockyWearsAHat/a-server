#!/usr/bin/env python3
"""Analyze the current_ogdk.ppm to understand what's being rendered."""

from PIL import Image
import os

if not os.path.exists('current_ogdk.ppm'):
    print("No current_ogdk.ppm found")
    exit(1)

img = Image.open('current_ogdk.ppm')
print(f'Image size: {img.size}')

print('\n=== Visual representation of current output ===')
print('Legend: . = black, # = gray, C = cyan, W = white, R = red, Y = yellow, L = light gray')

w, h = img.size

# Check specific rows  
for y_row in [8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 128, 136, 144, 152]:
    row_str = ''
    for x in range(0, min(240, w), 4):
        pixel = img.getpixel((x, y_row))
        r, g, b = pixel[:3]
        if r == 0 and g == 0 and b == 0:
            row_str += '.'
        elif r > 200 and g > 200 and b > 200:
            row_str += 'W'
        elif r > 200 and g < 50 and b < 50:
            row_str += 'R'
        elif r < 50 and g > 200 and b > 200:
            row_str += 'C'
        elif r > 200 and g > 200 and b < 50:
            row_str += 'Y'
        elif r > 140 and g > 140 and b > 140:
            row_str += 'L'
        elif r > 80:
            row_str += '#'
        else:
            row_str += '.'
    print(f'Row {y_row:3d}: {row_str}')

# Count unique colors
colors = {}
for y in range(h):
    for x in range(w):
        c = img.getpixel((x, y))[:3]
        colors[c] = colors.get(c, 0) + 1

print(f"\n{len(colors)} unique colors:")
for c, count in sorted(colors.items(), key=lambda x: -x[1]):
    print(f"  RGB{c}: {count} pixels")
