#!/usr/bin/env python3
import sys

def analyze_ppm(path):
    with open(path, 'rb') as f:
        header = f.readline().strip()
        if header != b'P6':
            print(f"Not a P6 PPM file: {header}")
            return
        line = f.readline()
        while line.startswith(b'#'):
            line = f.readline()
        width, height = map(int, line.strip().split())
        maxval = int(f.readline().strip())
        pixels = f.read()
    
    print(f"Image: {width}x{height}")
    
    # Scan for non-black rows
    non_black_rows = []
    for y in range(height):
        row_start = y * width * 3
        row_end = row_start + width * 3
        row = pixels[row_start:row_end]
        if any(row):
            non_black_rows.append(y)
    
    print(f"Non-black rows: {len(non_black_rows)}/{height}")
    if non_black_rows:
        print(f"  First: {min(non_black_rows)}, Last: {max(non_black_rows)}")
    
    # Scan for non-black columns
    non_black_cols = set()
    for y in range(height):
        for x in range(width):
            idx = (y * width + x) * 3
            r, g, b = pixels[idx], pixels[idx+1], pixels[idx+2]
            if r or g or b:
                non_black_cols.add(x)
    
    print(f"Non-black cols: {len(non_black_cols)}/{width}")
    if non_black_cols:
        print(f"  First: {min(non_black_cols)}, Last: {max(non_black_cols)}")
    
    # Show grid view of content
    print("\nContent grid (40x20 downsampled):")
    for sy in range(20):
        y = int(sy * height / 20)
        row_str = []
        for sx in range(40):
            x = int(sx * width / 40)
            idx = (y * width + x) * 3
            r, g, b = pixels[idx], pixels[idx+1], pixels[idx+2]
            if r == 0 and g == 0 and b == 0:
                row_str.append('.')
            elif r == 128 and g == 248 and b == 248:
                row_str.append('C')
            elif r == 104 and g == 104 and b == 104:
                row_str.append('G')
            elif r == 152 and g == 152 and b == 152:
                row_str.append('g')
            elif r == 248 and g == 248 and b == 248:
                row_str.append('W')
            elif r == 248 and g == 0 and b == 0:
                row_str.append('R')
            elif r == 248 and g == 248 and b == 0:
                row_str.append('Y')
            else:
                row_str.append('?')
        print(f"  {''.join(row_str)}")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: analyze_frame.py <ppm_file>")
        sys.exit(1)
    analyze_ppm(sys.argv[1])
