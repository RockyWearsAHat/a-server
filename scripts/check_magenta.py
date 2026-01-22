#!/usr/bin/env python3
with open('dumps/dkc_frame_2645.ppm', 'rb') as f:
    f.readline()
    f.readline()
    f.readline()
    pixels = f.read()

# Find ALL pink/magenta-ish pixels (high red+blue, low green)
pink_pos = []
for y in range(160):
    for x in range(240):
        i = (y * 240 + x) * 3
        r, g, b = pixels[i], pixels[i+1], pixels[i+2]
        # Pink: high R and B, low G
        if r > 100 and b > 100 and g < 150 and r > g and b > g:
            pink_pos.append((x, y, r, g, b))

print(f'Pink pixels: {len(pink_pos)}')
if pink_pos:
    print('First 10 with RGB:')
    for x, y, r, g, b in pink_pos[:10]:
        print(f'  ({x}, {y}) RGB({r},{g},{b})')
    y_vals = [y for x,y,_,_,_ in pink_pos]
    print(f'Y range: {min(y_vals)}-{max(y_vals)}')
    
    # Show unique colors
    from collections import Counter
    colors = Counter([(r,g,b) for _,_,r,g,b in pink_pos])
    print('Unique pink colors:')
    for (r,g,b), count in colors.most_common(10):
        print(f'  RGB({r},{g},{b}): {count} pixels')
