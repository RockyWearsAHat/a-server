#!/usr/bin/env python3
"""Analyze VRAM to distinguish tile graphics from code data."""

import sys

def analyze_vram(vram_path):
    with open(vram_path, 'rb') as f:
        data = f.read()
    
    # Check charBase 1 (0x4000) for code vs tile data
    charBase = 0x4000
    
    # Sample tiles at different offsets
    sample_tiles = [0, 64, 128, 192, 256, 512, 768, 777, 800, 900, 1000]
    
    for tile_idx in sample_tiles:
        offset = charBase + tile_idx * 32
        if offset + 32 > len(data):
            continue
        tile_data = data[offset:offset+32]
        
        # Check for all zeros
        if all(b == 0 for b in tile_data):
            print(f"Tile {tile_idx:4d} (0x{offset:04x}): ALL ZEROS")
            continue
        
        # Check unique byte count
        unique = len(set(tile_data))
        
        # Show first 16 bytes
        hex_str = ' '.join(f'{b:02x}' for b in tile_data[:16])
        
        # Try to detect code patterns
        is_code = False
        if tile_data[0:4] == bytes([0x1e, 0xff, 0x2f, 0xe1]):
            is_code = True  # BX LR
        if len(tile_data) > 1 and tile_data[1] in [0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f]:
            is_code = True  # LDR Rn, [PC, #imm]
        if tile_data[0:2] in [bytes([0xf0, 0xb5]), bytes([0xb5, 0xf0])]:
            is_code = True  # PUSH
            
        data_type = "CODE" if is_code else ("TILE" if unique <= 6 else "???")
        print(f"Tile {tile_idx:4d} (0x{offset:04x}): {hex_str} | unique={unique:2d} | {data_type}")
    
    # Check where actual NES-style tile data is
    print("\n--- Checking charBase 0 (0x0000) ---")
    charBase0 = 0x0000
    for tile_idx in [0, 64, 128, 192, 200, 220, 240, 250, 255]:
        offset = charBase0 + tile_idx * 32
        if offset + 32 > len(data):
            continue
        tile_data = data[offset:offset+32]
        if all(b == 0 for b in tile_data):
            print(f"Tile {tile_idx:4d} (0x{offset:04x}): ALL ZEROS")
            continue
        unique = len(set(tile_data))
        hex_str = ' '.join(f'{b:02x}' for b in tile_data[:16])
        data_type = "TILE" if unique <= 8 else "???"
        print(f"Tile {tile_idx:4d} (0x{offset:04x}): {hex_str} | unique={unique:2d} | {data_type}")

if __name__ == '__main__':
    vram_path = sys.argv[1] if len(sys.argv) > 1 else '/tmp/vram_raw_f60.bin'
    analyze_vram(vram_path)
