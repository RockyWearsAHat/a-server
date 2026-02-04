#!/usr/bin/env python3
import struct

with open('/tmp/vram_raw.bin', 'rb') as f:
    vram = f.read()

# Check tile 184 (the fill tile at ty=24)
# charBase=1 means tiles start at 0x4000
# 4bpp: 32 bytes per tile
# tile 184 offset = 0x4000 + 184*32 = 0x4000 + 0x1700 = 0x5700

tile_184_offset = 0x4000 + 184 * 32
print(f"Tile 184 at offset 0x{tile_184_offset:04x}:")
for row in range(8):
    row_offset = tile_184_offset + row * 4
    row_data = vram[row_offset:row_offset+4]
    pixels = ""
    for byte in row_data:
        lo = byte & 0xF
        hi = (byte >> 4) & 0xF
        pixels += f"{lo:x}{hi:x}"
    print(f"  Row {row}: {row_data.hex()} -> pixels: {pixels}")

print()
print("Checking tile data at various tiles:")
for tile_idx in [0, 16, 53, 184, 247]:
    offset = 0x4000 + tile_idx * 32
    data = vram[offset:offset+8]
    print(f"  Tile {tile_idx:3d} at 0x{offset:04x}: {data.hex()}")

# Check if maybe the tile base is wrong - what if it's not at charBase=1?
print()
print("Looking for recognizable tile patterns at different bases:")
for base in [0, 0x4000, 0x8000, 0xC000]:
    print(f"  Base 0x{base:04x}:")
    for tile_idx in [0, 247]:
        offset = base + tile_idx * 32
        if offset + 32 <= len(vram):
            data = vram[offset:offset+8]
            nonzero = sum(1 for b in data if b != 0)
            print(f"    Tile {tile_idx}: {data.hex()} (nonzero bytes: {nonzero})")
