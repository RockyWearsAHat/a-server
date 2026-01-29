#!/usr/bin/env python3
"""Analyze palette from DMA log."""

# From the DMA log, the palette transfer to 0x05000000 from VRAM 0x0600095c:
# First 32 bytes: [0]=0, [4]=0, [8]=0, [12]=0, 
# [16]: 0x35ad0000, [20]: 0x7ff04e73, [24]: 0x03ff7fff, [28]: 0x0000001f

# This is LITTLE ENDIAN so:
# Offset 0-3:   word 0x00000000 -> colors 0,1 = 0x0000, 0x0000
# Offset 4-7:   word 0x00000000 -> colors 2,3 = 0x0000, 0x0000
# ... 
# Offset 16-19: word 0x35ad0000 -> colors 8,9 = 0x0000, 0x35ad
# Offset 20-23: word 0x7ff04e73 -> colors 10,11 = 0x4e73, 0x7ff0
# Offset 24-27: word 0x03ff7fff -> colors 12,13 = 0x7fff, 0x03ff
# Offset 28-31: word 0x0000001f -> colors 14,15 = 0x001f, 0x0000

def decode_color(c):
    """Decode GBA BGR555 to RGB."""
    r = (c & 0x1F) << 3
    g = ((c >> 5) & 0x1F) << 3
    b = ((c >> 10) & 0x1F) << 3
    return (r, g, b)

# Raw palette data as 16-bit colors (little-endian from 32-bit words)
colors = [
    0x0000, 0x0000,  # 0, 1
    0x0000, 0x0000,  # 2, 3
    0x0000, 0x0000,  # 4, 5
    0x0000, 0x0000,  # 6, 7
    0x0000, 0x35ad,  # 8, 9
    0x4e73, 0x7ff0,  # 10, 11  <- 0x7FF0 = CYAN!
    0x7fff, 0x03ff,  # 12, 13
    0x001f, 0x0000,  # 14, 15
]

print("Palette colors from DMA to 0x05000000:")
for i, c in enumerate(colors):
    r, g, b = decode_color(c)
    print(f"  Index {i:2d}: 0x{c:04x} = RGB({r:3d},{g:3d},{b:3d})")

print()
print("Key observations:")
print("  Index 11 = 0x7FF0 = RGB(128, 248, 248) = CYAN")
print("  This is where ci=3 + 8 = 11 maps to")
print()
print("The palette itself has CYAN at index 11.")
print("This means the DMA source data at 0x0600095c contains wrong colors!")
