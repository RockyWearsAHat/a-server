#!/usr/bin/env python3
"""Dump specific tile data from VRAM (extracted from ROM after execution)"""
import sys

# From ogdk_overlap_trace.txt:
# tileBase=0x4000, tileIdx=510, tileAddr=0x7fc0
# Tile 510 is at offset 0x7FC0 - 0x6000000 = 0x7FC0 in VRAM
# For 4bpp tiles, each tile is 32 bytes

# The tile data should be in VRAM after the game initializes
# Let's check what the RGB(248, 0, 0) red color would look like

# GBA color format: xBBBBBGGGGGRRRRR (15-bit)
# Red = R=31, G=0, B=0 = 0b0000000000011111 = 0x001F
# RGB(248, 0, 0) on GBA with <<3 expansion: R=31
# So the red palette entry should be 0x001F

print("Analyzing red color in GBA format:")
print("RGB(248, 0, 0) = R=31, G=0, B=0 (after >>3)")
print("GBA color word: 0x001F (xBBBBBGGGGGRRRRR)")

print("\nAnalyzing yellow color in GBA format:")
print("RGB(248, 248, 0) = R=31, G=31, B=0 (after >>3)")
print("GBA color word: 0x03FF (0_00000_11111_11111)")

# So the issue is: which palette entry contains red/yellow?
# If colorIndex+8 offset is working, the actual data should be at indices 8+
# Let's check the VRAM dump if available
import os
if os.path.exists("/tmp/ogdk_vram.txt"):
    print("\n=== /tmp/ogdk_vram.txt contents ===")
    with open("/tmp/ogdk_vram.txt") as f:
        print(f.read())
