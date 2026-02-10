#!/usr/bin/env python3
import sys

with open('/tmp/ogdk_fixed.ppm', 'rb') as f:
    header = f.readline()
    dims = f.readline()
    maxv = f.readline()
    data = f.read()

w, h = 240, 160

def px(x, y):
    off = (y * w + x) * 3
    return (data[off], data[off+1], data[off+2])

# Text rows y=135-151 - render as ASCII art
print('=== Text area (y=135-151, x=30-210) ===')
for y in range(135, 152):
    row = ''
    for x in range(30, 210):
        r, g, b = px(x, y)
        if r == 0 and g == 0 and b == 0:
            row += '.'
        elif r == 248 and g == 248 and b == 248:
            row += '#'
        else:
            row += 'X'
    print(f'y={y}: {row}')

# Zoom into M at around x=80
print('\n=== First letter area (y=147-152, x=78-90) ===')
for y in range(146, 153):
    row = ''
    for x in range(78, 92):
        r, g, b = px(x, y)
        if r == 0 and g == 0 and b == 0:
            row += '.'
        elif r == 248 and g == 248 and b == 248:
            row += '#'
        else:
            row += f'[{r},{g},{b}]'
    print(f'y={y}: {row}')

# Copyright area y=135-140
print('\n=== Copyright line (y=135-140, x=30-80) ===')
for y in range(135, 141):
    row = ''
    for x in range(30, 80):
        r, g, b = px(x, y)
        if r == 0 and g == 0 and b == 0:
            row += '.'
        elif r == 248 and g == 248 and b == 248:
            row += '#'
        else:
            row += 'X'
    print(f'y={y}: {row}')
