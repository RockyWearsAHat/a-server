#!/usr/bin/env python3
"""Analyze PPM image to describe visible content"""

import sys

def analyze_ppm_content(filename):
    with open(filename, 'rb') as f:
        # Read PPM header
        magic = f.readline().strip()
        if magic != b'P6':
            print(f"Not a P6 PPM file: {magic}")
            return
        
        # Skip comments
        while True:
            line = f.readline()
            if not line.startswith(b'#'):
                break
        
        # Parse dimensions
        width, height = map(int, line.split())
        maxval = int(f.readline().strip())
        
        # Read pixel data
        pixels = list(f.read())
        
        print(f"Image: {width}x{height}")
        print(f"\n=== Analyzing visible content ===\n")
        
        # Analyze each row to find horizontal patterns
        print("Row-by-row analysis (first 20 rows):")
        for row in range(min(20, height)):
            row_data = []
            for x in range(width):
                idx = (row * width + x) * 3
                r, g, b = pixels[idx], pixels[idx+1], pixels[idx+2]
                row_data.append((r, g, b))
            
            # Count colors in this row
            color_counts = {}
            for c in row_data:
                color_counts[c] = color_counts.get(c, 0) + 1
            
            # Describe row
            dominant = max(color_counts, key=color_counts.get)
            non_dominant = sum(1 for c in row_data if c != dominant)
            
            desc = []
            if dominant == (0, 0, 0):
                desc.append(f"mostly black ({color_counts[dominant]}/{width})")
            elif dominant == (128, 248, 248):
                desc.append(f"mostly cyan")
            else:
                desc.append(f"mostly {dominant}")
            
            if non_dominant > 0:
                desc.append(f"+{non_dominant} other pixels")
            
            print(f"  Row {row:3d}: {', '.join(desc)}")
        
        # Find rows with red/yellow (likely text)
        print("\n\nRows with red/yellow pixels (likely text):")
        for row in range(height):
            for x in range(width):
                idx = (row * width + x) * 3
                r, g, b = pixels[idx], pixels[idx+1], pixels[idx+2]
                if r > 200 and g < 50 and b < 50:  # Red
                    print(f"  Row {row}: RED at x={x}")
                    break
                if r > 200 and g > 200 and b < 50:  # Yellow
                    print(f"  Row {row}: YELLOW at x={x}")
                    break
        
        # Find horizontal spans of non-black pixels
        print("\n\nHorizontal spans of non-black pixels (first 20 rows):")
        for row in range(min(20, height)):
            spans = []
            start = None
            for x in range(width):
                idx = (row * width + x) * 3
                r, g, b = pixels[idx], pixels[idx+1], pixels[idx+2]
                is_black = (r == 0 and g == 0 and b == 0)
                
                if not is_black and start is None:
                    start = x
                elif is_black and start is not None:
                    spans.append((start, x-1))
                    start = None
            
            if start is not None:
                spans.append((start, width-1))
            
            if spans:
                span_strs = [f"{s}-{e}" for s, e in spans if (e-s) > 2]
                if span_strs:
                    print(f"  Row {row:3d}: {', '.join(span_strs)}")

if __name__ == "__main__":
    analyze_ppm_content(sys.argv[1] if len(sys.argv) > 1 else "current_ogdk.ppm")
