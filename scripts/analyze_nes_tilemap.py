#!/usr/bin/env python3
"""Analyze Classic NES Series tilemap data format from VRAM dump"""

import struct
import sys

def main():
    vram_file = "/tmp/ogdk_vram.bin"
    try:
        with open(vram_file, 'rb') as f:
            vram = f.read()
    except FileNotFoundError:
        print(f"VRAM dump not found: {vram_file}")
        return

    print(f"VRAM size: {len(vram)} bytes")

    # mapBase = screenBase(13) * 0x800 = 0x6800
    # Row 24 (VOFS=192/8=24) with 2-byte entries at offset 24*64 = 0x600
    # Total: 0x6800 + 0x600 = 0x6E00
    map_offset = 0x6E00

    print("\n=== Row 24 tilemap entries (standard 2-byte GBA format) ===")
    for i in range(16):
        entry = struct.unpack_from('<H', vram, map_offset + i*2)[0]
        tile10 = entry & 0x3FF
        tile8 = entry & 0xFF
        hflip = (entry >> 10) & 1
        vflip = (entry >> 11) & 1
        pal = (entry >> 12) & 0xF
        print(f"  [{i:2d}] 0x{entry:04x}: tile10={tile10:3d} tile8={tile8:3d} pal={pal:2d} h={hflip} v={vflip}")

    # Check what tiles look like
    print("\n=== Tile data samples ===")
    
    # Tile 53 (first tile in row 24)
    print("Tile 53 data (charBase=1 -> 0x4000 + 53*32):")
    tile_offset = 0x4000 + 53 * 32
    tile_data = vram[tile_offset:tile_offset+32]
    print(f"  Hex: {tile_data.hex(' ')}")
    
    # Tile 0 (should be blank for BG transparency)
    print("\nTile 0 data (charBase=1 -> 0x4000):")
    tile_data = vram[0x4000:0x4000+32]
    print(f"  Hex: {tile_data.hex(' ')}")
    all_zero = all(b == 0 for b in tile_data)
    print(f"  All zeros: {all_zero}")

    # Let's also look at what entry 0x8800 refers to
    # 0x8800 = tile 0, pal 8, no flips
    print("\n=== Analysis of common entries ===")
    print("Entry 0x8800: tile=0 (transparent bg) with pal=8")
    print("Entry 0x4835: tile=53 with pal=4")
    
    # Look at the tilemap pattern - is there a structure?
    print("\n=== Full visible rows (24-27) ===")
    for row in range(24, 28):
        row_offset = 0x6800 + row * 64  # 32 tiles * 2 bytes
        print(f"Row {row}:")
        tiles = []
        for col in range(32):
            entry = struct.unpack_from('<H', vram, row_offset + col*2)[0]
            tile8 = entry & 0xFF
            tiles.append(tile8)
        # Print as 8 groups of 4 tiles
        for g in range(4):
            group = tiles[g*8:(g+1)*8]
            print(f"  cols {g*8:2d}-{g*8+7:2d}: {' '.join(f'{t:3d}' for t in group)}")

if __name__ == "__main__":
    main()
