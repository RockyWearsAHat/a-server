#!/usr/bin/env python3
"""Count high vs low tile indices in tilemap."""
import sys

vram_file = sys.argv[1] if len(sys.argv) > 1 else '/tmp/vram_raw_f60.bin'
print(f"Analyzing: {vram_file}")
with open(vram_file, 'rb') as f:
    data = f.read()

screenBase = 0x6800
size2_entries = 32 * 64  # size=2 is 32x64 tiles
high_tiles = []
valid_tiles = []

for i in range(size2_entries):
    entry_offset = screenBase + i * 2
    if entry_offset + 2 > len(data):
        break
    entry = data[entry_offset] | (data[entry_offset + 1] << 8)
    tile = entry & 0x3FF
    if tile >= 256:
        high_tiles.append((i, tile, entry))
    else:
        valid_tiles.append((i, tile, entry))

print(f'Total entries: {len(valid_tiles) + len(high_tiles)}')
print(f'Valid tiles (0-255): {len(valid_tiles)}')
print(f'High tiles (256+): {len(high_tiles)}')
print()
print('First 30 high tile entries:')
for i, (idx, tile, entry) in enumerate(high_tiles[:30]):
    x = idx % 32
    y = idx // 32
    pal = (entry >> 12) & 0xF
    print(f'  [{x:2d},{y:2d}] entry=0x{entry:04x} tile={tile:3d} pal={pal}')
