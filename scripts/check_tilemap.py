#!/usr/bin/env python3
import struct

with open('/tmp/vram_raw.bin', 'rb') as f:
    vram = f.read()

mapBase = 0x6800
bgvofs = 192

ty = (bgvofs // 8)  # = 24
print(f"Visible starts at ty={ty}")

blockY = 1 if ty >= 32 else 0
ty_local = ty & 31

print(f"blockY={blockY}, ty_local={ty_local}")

# With 4-byte stride, row 24 offset
blockOffset_4byte = blockY * 4096
rowOffset_4byte = ty_local * 32 * 4
print(f"4-byte: total offset from mapBase = 0x{blockOffset_4byte + rowOffset_4byte:x}")

print()
print("Row 24 with 4-byte stride:")
for col in range(8):
    offset = mapBase + blockOffset_4byte + ty_local * 32 * 4 + col * 4
    entry = struct.unpack_from('<H', vram, offset)[0]
    tile = entry & 0xFF
    print(f"  [{col}] offset=0x{offset:04x} tile={tile}")

print()
print("Standard 2-byte stride (row 24):")
for col in range(8):
    offset = mapBase + ty_local * 32 * 2 + col * 2
    entry = struct.unpack_from('<H', vram, offset)[0]
    tile = entry & 0xFF
    print(f"  [{col}] offset=0x{offset:04x} entry=0x{entry:04x} tile={tile}")

# The screen width is 240 pixels = 30 tiles, but NES uses 32 tiles per row
# Check if maybe only 16 tiles use 4-byte, then 16 use different?
print()
print("Full row 0 analysis - looking for pattern:")
print("Every 2-byte entry:")
for col in range(32):
    offset = mapBase + col * 2
    entry = struct.unpack_from('<H', vram, offset)[0]
    if col < 16:
        print(f"  [{col:2d}] 0x{entry:04x}", end="")
        if col == 15:
            print()
