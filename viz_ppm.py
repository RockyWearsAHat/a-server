#!/usr/bin/env python3
"""Visualize a PPM file as ASCII art."""
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: viz_ppm.py <ppm_file>")
        sys.exit(1)
    
    with open(sys.argv[1], 'rb') as f:
        header = f.readline()  # P6
        dims = f.readline()    # 240 160
        maxval = f.readline()  # 255
        data = f.read()

    width, height = 240, 160
    print(f"Image: {width}x{height}")
    print()
    
    for y in range(0, height, 8):  # Sample every 8 rows
        row = ''
        for x in range(0, width, 4):  # Sample every 4 pixels
            idx = (y * width + x) * 3
            r, g, b = data[idx], data[idx+1], data[idx+2]
            if r == 0 and g == 0 and b == 0:
                row += ' '  # black
            elif r == 128 and g == 248 and b == 248:
                row += '#'  # cyan
            elif r == 104:
                row += '='  # gray
            elif r == 152:
                row += '-'  # light gray
            elif r == 248 and g == 248 and b == 248:
                row += '@'  # white
            elif r == 248 and g == 0:
                row += '*'  # red
            elif r == 248 and g == 248:
                row += '+'  # yellow
            else:
                row += '?'  # unknown
        print(row)

if __name__ == '__main__':
    main()
