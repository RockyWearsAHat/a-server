#!/usr/bin/env python3
"""Analyze PPM frame dumps to identify color issues."""

import struct
import sys
from collections import Counter

def read_ppm(filename):
    """Read a P6 PPM file and return width, height, and pixel data."""
    with open(filename, 'rb') as f:
        # Read header
        magic = f.readline().strip()
        if magic != b'P6':
            raise ValueError(f"Not a P6 PPM file: {magic}")
        
        # Skip comments
        line = f.readline()
        while line.startswith(b'#'):
            line = f.readline()
        
        # Parse dimensions
        width, height = map(int, line.split())
        
        # Read max color value
        maxval = int(f.readline().strip())
        
        # Read pixel data
        pixels = []
        for y in range(height):
            row = []
            for x in range(width):
                r, g, b = struct.unpack('BBB', f.read(3))
                row.append((r, g, b))
            pixels.append(row)
        
        return width, height, pixels

def describe_color(r, g, b):
    """Give a human-readable description of an RGB color."""
    if r < 20 and g < 20 and b < 20:
        return "black"
    if r > 200 and g > 200 and b > 200:
        return "white"
    if r > g + 30 and r > b + 30:
        if r > 150 and g < 100:
            return "red/pink"
        return "reddish"
    if g > r + 30 and g > b + 30:
        return "green"
    if b > r + 30 and b > g + 30:
        return "blue"
    if abs(r - g) < 30 and abs(g - b) < 30:
        return "gray"
    if r > 100 and g > 100 and b < 80:
        return "yellow"
    if r > 100 and g < 80 and b > 100:
        return "magenta/pink"
    if r < 80 and g > 100 and b > 100:
        return "cyan"
    return "mixed"

def analyze_frame(filename):
    """Analyze a frame and report on colors and sprites."""
    print(f"\n{'='*60}")
    print(f"Analyzing: {filename}")
    print('='*60)
    
    width, height, pixels = read_ppm(filename)
    print(f"Dimensions: {width}x{height}")
    
    # Count all colors
    color_counts = Counter()
    for row in pixels:
        for rgb in row:
            color_counts[rgb] += 1
    
    # Find dominant colors (excluding very common background colors)
    sorted_colors = color_counts.most_common(20)
    print(f"\nTop 20 colors:")
    for i, ((r, g, b), count) in enumerate(sorted_colors, 1):
        pct = (count / (width * height)) * 100
        desc = describe_color(r, g, b)
        print(f"  {i:2}. RGB({r:3},{g:3},{b:3}) = {desc:15} : {count:6} pixels ({pct:5.1f}%)")
    
    # Look for pink/magenta pixels (potential problem colors)
    pink_pixels = []
    for y, row in enumerate(pixels):
        for x, (r, g, b) in enumerate(row):
            # Pink: high red, low green, medium-high blue
            if r > 150 and g < 120 and b > 100 and b < r:
                pink_pixels.append((x, y, r, g, b))
    
    if pink_pixels:
        print(f"\nFound {len(pink_pixels)} PINK pixels!")
        print("Sample locations (first 10):")
        for x, y, r, g, b in pink_pixels[:10]:
            print(f"  ({x:3},{y:3}): RGB({r},{g},{b})")
    else:
        print("\nNo obvious pink pixels found.")
    
    # Look for sprites (non-background objects in typical sprite Y range)
    sprite_y_start = 20
    sprite_y_end = 150
    sprite_colors = Counter()
    for y in range(sprite_y_start, min(sprite_y_end, height)):
        for rgb in pixels[y]:
            if rgb != sorted_colors[0][0]:  # Not the most common (background) color
                sprite_colors[rgb] += 1
    
    if sprite_colors:
        print(f"\nSprite-area colors (Y={sprite_y_start}-{sprite_y_end}, excluding dominant background):")
        for (r, g, b), count in sprite_colors.most_common(10):
            desc = describe_color(r, g, b)
            print(f"  RGB({r:3},{g:3},{b:3}) = {desc:15} : {count:5} pixels")

if __name__ == "__main__":
    frames = [
        "dumps/ppu_frame_2195.ppm",  # Rope frame
        "dumps/ppu_frame_2200.ppm",  # Rope frame
        "dumps/ppu_frame_2394.ppm",  # Alligator frame
        "dumps/ppu_frame_2395.ppm",  # After alligator disappears
    ]
    
    for frame in frames:
        try:
            analyze_frame(frame)
        except Exception as e:
            print(f"\nError analyzing {frame}: {e}")
