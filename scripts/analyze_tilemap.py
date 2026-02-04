#!/usr/bin/env python3
import struct
import sys

def analyze_tilemap():
    with open('/tmp/vram_raw.bin', 'rb') as f:
        vram = f.read()

    print('=== Testing 4-byte stride hypothesis ===')
    print('Row 0 with 4-byte stride:')
    for col in range(16):
        offset = 0x6800 + col * 4
        entry = struct.unpack_from('<H', vram, offset)[0]
        tile = entry & 0xFF
        attr = (entry >> 8) & 0xFF
        print(f'  [{col:2d}] entry=0x{entry:04x} tile={tile:3d} attr=0x{attr:02x}')

    print()
    print('With 4-byte stride, row 24 (bgvofs=192) would be:')
    # 128 bytes per row (32 tiles * 4 bytes)
    for col in range(8):
        offset = 0x6800 + 24 * 128 + col * 4
        if offset < len(vram):
            entry = struct.unpack_from('<H', vram, offset)[0]
            tile = entry & 0xFF
            print(f'  [{col}] offset=0x{offset:04x} tile={tile:3d}')
        else:
            print(f'  [{col}] OUT OF BOUNDS (offset 0x{offset:04x})')

    print()
    print('=== Testing standard 2-byte stride but only even indices ===')
    print('This means skip every other entry, effective 4-byte stride')
    print('Row 0:')
    for col in range(16):
        offset = 0x6800 + col * 2 * 2  # double the stride
        entry = struct.unpack_from('<H', vram, offset)[0]
        tile = entry & 0xFF
        print(f'  [{col:2d}] offset=0x{offset:04x} tile={tile:3d}')

if __name__ == '__main__':
    analyze_tilemap()
