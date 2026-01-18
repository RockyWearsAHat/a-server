#!/usr/bin/env python3
"""Find frames with pink pixels in PPM dumps."""

import struct
import sys
from collections import Counter

def analyze_frame(frame_num, ppm_path='dumps'):
    try:
        with open(f'{ppm_path}/ppu_frame_{frame_num}.ppm', 'rb') as f:
            f.readline(); f.readline(); f.readline()
            colors = [struct.unpack('BBB', f.read(3)) for _ in range(240*160)]
        
        counter = Counter(colors)
        pink_pixels = [(r,g,b,cnt) for (r,g,b), cnt in counter.items() 
                       if r > 150 and g < 150 and b > 120]
        pink_count = sum(cnt for _,_,_,cnt in pink_pixels)
        
        if pink_count > 0:
            print(f'Frame {frame_num}: {pink_count:5} PINK pixels ({pink_count/len(colors)*100:5.2f}%)')
            for r,g,b,cnt in sorted(pink_pixels, key=lambda x: -x[3])[:3]:
                print(f'  RGB({r},{g},{b}): {cnt} pixels')
            return True
        return False
    except FileNotFoundError:
        return None

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: find_pink_frames.py <start_frame> <end_frame>")
        sys.exit(1)
    
    start = int(sys.argv[1])
    end = int(sys.argv[2])
    
    pink_frames = []
    for frame_num in range(start, end + 1):
        result = analyze_frame(frame_num)
        if result:
            pink_frames.append(frame_num)
    
    if pink_frames:
        print(f"\nFrames with pink pixels: {pink_frames}")
    else:
        print("\nNo pink pixels found in range.")
