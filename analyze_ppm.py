#!/usr/bin/env python3
import sys

def analyze_ppm(filename):
    with open(filename, 'rb') as f:
        # Read PPM P6 header
        header = f.readline().decode().strip()
        if header != 'P6':
            print(f'Not a P6 PPM file: {header}')
            return
        
        dims = f.readline().decode().strip()
        maxval = f.readline().decode().strip()
        w, h = map(int, dims.split())
        print(f'Image: {w}x{h}')
        
        # Read pixel data
        pixels = f.read()
        print(f'Pixel bytes: {len(pixels)} (expected: {w*h*3})')
        
        # Count all unique colors
        colors = {}
        for i in range(w*h):
            r, g, b = pixels[i*3], pixels[i*3+1], pixels[i*3+2]
            color = (r, g, b)
            colors[color] = colors.get(color, 0) + 1
        
        print(f'Unique colors: {len(colors)}')
        print('Top 15 colors by frequency:')
        for c, count in sorted(colors.items(), key=lambda x: -x[1])[:15]:
            pct = 100.0 * count / (w*h)
            print(f'  RGB{c}: {count} ({pct:.1f}%)')
        
        # Check for specific corruption indicators
        magenta_count = 0
        black_count = 0
        for i in range(w*h):
            r, g, b = pixels[i*3], pixels[i*3+1], pixels[i*3+2]
            if r > 200 and g < 50 and b > 200:
                magenta_count += 1
            if r == 0 and g == 0 and b == 0:
                black_count += 1
        
        print(f'\nMagenta-ish pixels (corruption indicator): {magenta_count}')
        print(f'Black pixels: {black_count}')
        
        # Check if the image looks like it's rendering something
        if len(colors) > 10 and black_count < w*h*0.9:
            print('\nImage appears to have content (not just black screen)')
        else:
            print('\nImage may be mostly black or corrupt')

if __name__ == '__main__':
    filename = sys.argv[1] if len(sys.argv) > 1 else 'current_ogdk.ppm'
    analyze_ppm(filename)
