#!/usr/bin/env python3
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: ppm_ascii.py <file.ppm>")
        return
    
    with open(sys.argv[1], 'rb') as f:
        data = f.read()
    
    # Skip header (P6\n240 160\n255\n)
    idx = data.find(b'255\n') + 4
    pixels = data[idx:]
    w, h = 240, 160
    
    color_chars = {
        (0, 0, 0): ' ',           # Black = space
        (104, 104, 104): '.',     # Dark gray = dot
        (128, 248, 248): '#',     # Cyan = hash
        (152, 152, 152): ':',     # Medium gray = colon
        (248, 248, 248): 'O',     # White = O
        (248, 0, 0): 'R',         # Red = R
        (248, 248, 0): 'Y',       # Yellow = Y
    }
    
    print('ASCII preview (1:4 scale):')
    for y in range(0, h, 4):
        line = ''
        for x in range(0, w, 4):
            i = (y * w + x) * 3
            rgb = (pixels[i], pixels[i+1], pixels[i+2])
            line += color_chars.get(rgb, '?')
        print(line)

if __name__ == '__main__':
    main()
