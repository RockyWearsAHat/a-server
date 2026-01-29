#!/usr/bin/env python3
"""Analyze PPM image structure to understand tile layout"""
import sys

def read_ppm_raw(path):
    with open(path, 'rb') as f:
        magic = f.readline()
        dims = f.readline()
        maxval = f.readline()
        data = f.read()
    w, h = map(int, dims.split())
    return w, h, data

path = sys.argv[1] if len(sys.argv) > 1 else "current_ogdk.ppm"
w, h, data = read_ppm_raw(path)
print(f"Image: {w}x{h}")

# Analyze 8x8 tile blocks
tiles_per_row = w // 8  # 30 tiles
tiles_per_col = h // 8  # 20 tiles

print(f"\nTile grid: {tiles_per_row}x{tiles_per_col} = {tiles_per_row * tiles_per_col} tiles")

# For each tile, compute simple "pattern" based on colors used
tile_patterns = {}
for ty in range(tiles_per_col):
    for tx in range(tiles_per_row):
        # Get pixels for this tile
        colors = set()
        for py in range(8):
            for px in range(8):
                x = tx * 8 + px
                y = ty * 8 + py
                idx = (y * w + x) * 3
                r, g, b = data[idx], data[idx+1], data[idx+2]
                colors.add((r, g, b))
        
        # Create simple hash: number of colors + dominant color
        colors_tuple = tuple(sorted(colors))
        if colors_tuple not in tile_patterns:
            tile_patterns[colors_tuple] = 0
        tile_patterns[colors_tuple] += 1

print(f"\nUnique tile patterns: {len(tile_patterns)}")

# Show most common patterns
print("\nTop 10 tile patterns by frequency:")
sorted_patterns = sorted(tile_patterns.items(), key=lambda x: -x[1])
for i, (colors, count) in enumerate(sorted_patterns[:10]):
    color_desc = ", ".join(f"RGB({r},{g},{b})" for r, g, b in list(colors)[:3])
    if len(colors) > 3:
        color_desc += f" +{len(colors)-3} more"
    print(f"  {count:3d} tiles: {color_desc}")

# Visual representation of tile grid (simplified)
print("\nTile grid (# = multi-color, . = single/mostly black):")
for ty in range(tiles_per_col):
    row = ""
    for tx in range(tiles_per_row):
        colors = set()
        non_black_count = 0
        for py in range(8):
            for px in range(8):
                x = tx * 8 + px
                y = ty * 8 + py
                idx = (y * w + x) * 3
                r, g, b = data[idx], data[idx+1], data[idx+2]
                colors.add((r, g, b))
                if r > 0 or g > 0 or b > 0:
                    non_black_count += 1
        
        # Determine tile character
        if non_black_count == 0:
            row += "."
        elif non_black_count < 16:
            row += ":"
        elif non_black_count < 32:
            row += "o"
        else:
            row += "#"
    print(f"  Row {ty:2d}: {row}")
