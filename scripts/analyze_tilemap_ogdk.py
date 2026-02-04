#!/usr/bin/env python3
"""Analyze OG-DK tilemap from VRAM dump."""

import struct
import sys

def main():
    # Read VRAM
    try:
        with open('/tmp/vram_raw.bin', 'rb') as f:
            vram = f.read()
    except FileNotFoundError:
        print("Error: /tmp/vram_raw.bin not found. Run the emulator first.")
        return 1

    print(f"VRAM size: {len(vram)} bytes")
    
    # BG0: charBase=1, screenBase=13, size=2
    # charBase 1 starts at offset 0x4000 (16KB)
    # screenBase 13 = 13 * 2048 = 0x6800
    # size=2 means 256x512 (32x64 tiles) = 2 vertical screen blocks
    
    charbase_offset = 1 * 16384  # = 0x4000 = 16384
    
    print(f"\nBG0 Configuration:")
    print(f"  charBase=1 -> tile data at VRAM offset 0x{charbase_offset:04x}")
    print(f"  screenBase=13 -> tilemap at VRAM offset 0x6800")
    print(f"  size=2 -> 256x512 pixels (32x64 tiles, 2 vertical screen blocks)")
    
    # Check both screen blocks
    for block_num, sb in enumerate([13, 14]):
        tilemap_offset = sb * 2048
        print(f"\n=== Screen Block {block_num} (screenBase {sb} at 0x{tilemap_offset:04x}) ===")
        
        # Print first 5 rows
        for row in range(5):
            tiles = []
            for col in range(30):
                offset = tilemap_offset + (row * 32 + col) * 2
                if offset + 1 < len(vram):
                    entry = struct.unpack('<H', vram[offset:offset+2])[0]
                    tile_idx = entry & 0x3FF
                    tiles.append(tile_idx)
            print(' '.join(f'{t:3d}' for t in tiles[:15]) + ' ...')
    
    # Check key tiles
    print("\n=== Key Tile Data Analysis ===")
    key_tiles = [0, 1, 5, 8, 48, 86, 160, 162, 440, 768 % 512]
    
    for tile_idx in key_tiles:
        tile_offset = charbase_offset + tile_idx * 32
        if tile_offset + 32 <= len(vram):
            tile_data = vram[tile_offset:tile_offset + 32]
            non_zero = sum(1 for b in tile_data if b != 0)
            data_preview = ' '.join(f'{tile_data[i]:02x}' for i in range(8))
            print(f"  Tile {tile_idx:3d}: {non_zero:2d}/32 non-zero, data: {data_preview}...")
    
    # Check DMA target area (tiles 368-512)
    print("\n=== DMA Target Area (tiles 368-400) ===")
    for tile_idx in range(368, 400, 8):
        tile_offset = charbase_offset + tile_idx * 32
        if tile_offset + 32 <= len(vram):
            tile_data = vram[tile_offset:tile_offset + 32]
            non_zero = sum(1 for b in tile_data if b != 0)
            print(f"  Tile {tile_idx:3d}: {non_zero:2d}/32 non-zero bytes")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
