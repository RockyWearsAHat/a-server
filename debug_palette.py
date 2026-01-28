#!/usr/bin/env python3
"""Dump palette by running emulator and extracting from PPM color analysis"""
import subprocess
import sys

# We can also check /tmp/ogdk_vram.txt if it exists
# The palette dump showed:
# Palette (first 64 bytes - banks 0-1): 
#   00 00 ad 35 73 4e f0 7f ff 7f ff 03 1f 00 00 00 
#   00 00 ff 7f 00 00 ff 7f 00 00 ff 7f 00 00 ff 7f

# Let me decode what's in bank 0 indices 0-15:
# Each color is 2 bytes (little-endian BGR555)

bank0 = bytes([
    0x00, 0x00, 0xad, 0x35, 0x73, 0x4e, 0xf0, 0x7f, 
    0xff, 0x7f, 0xff, 0x03, 0x1f, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xff, 0x7f, 0x00, 0x00, 0xff, 0x7f, 
    0x00, 0x00, 0xff, 0x7f, 0x00, 0x00, 0xff, 0x7f
])

def decode_bgr555(b0, b1):
    val = b0 | (b1 << 8)
    r = (val & 0x1F) << 3
    g = ((val >> 5) & 0x1F) << 3
    b = ((val >> 10) & 0x1F) << 3
    return r, g, b

print("Bank 0 colors (indices 0-15):")
for i in range(16):
    r, g, b = decode_bgr555(bank0[i*2], bank0[i*2+1])
    print(f"  Index {i:2d}: RGB({r:3d}, {g:3d}, {b:3d})")

# The NES palette in Classic NES games uses indices 8+ for actual colors
# because indices 0-7 are cleared to black
# So:
#   Index 8 = transparent (black)
#   Index 9 = gray (104, 104, 104) 
#   Index 10 = light gray (152, 152, 152)
#   Index 11 = cyan (128, 248, 248)
#   Index 12 = white (248, 248, 248)
#   Index 13 = yellow (248, 248, 0)
#   Index 14 = red (248, 0, 0)
#   Index 15 = black (0, 0, 0)

# With our +8 color offset:
#   Tile colorIndex 0 -> effectiveIndex 8 (black/transparent)
#   Tile colorIndex 1 -> effectiveIndex 9 (gray)
#   ...
#   Tile colorIndex 6 -> effectiveIndex 14 (RED)
#   Tile colorIndex 7 -> effectiveIndex 15 (black)

# So for 20%+ red, we need many tiles with colorIndex 6!
# But we only see 0.3% red... meaning colorIndex 6 is rare in the tiles

print("\n\nIf the game expects 20% red, tiles should use colorIndex=6 frequently")
print("Current output has only 0.3% red, suggesting tile data or palette mapping is wrong")
