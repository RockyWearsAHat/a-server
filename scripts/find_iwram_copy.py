#!/usr/bin/env python3
"""Find what copies data to IWRAM 0x03001000 in OG-DK.gba"""
import struct

with open("test_roms/OG-DK.gba", "rb") as f:
    rom = f.read()

print("=== OG-DK IWRAM 0x03001000 Investigation ===\n")

# LZ77 header at 0x5FF4
header = struct.unpack('<I', rom[0x5FF4:0x5FF8])[0]
decomp_size = header >> 8
print(f"LZ77 decompress: ROM 0x08005FF4 -> IWRAM 0x03007400")
print(f"  Size: {decomp_size} bytes (range: 0x03007400 - 0x{0x03007400 + decomp_size:08X})")

# The mysterious values at 0x03001000
print(f"\n=== Values found at IWRAM 0x03001000 at first DMA ===")
values = [0xF928F000, 0xD0AF2801, 0x8130481A, 0xF0002000]
for i, v in enumerate(values):
    print(f"  0x0300100{i*4:X}: 0x{v:08X}")
    # Search for this value in ROM
    v_bytes = struct.pack('<I', v)
    pos = rom.find(v_bytes)
    if pos != -1:
        print(f"    -> Found at ROM offset 0x{pos:X} (addr 0x{0x08000000+pos:08X})")

# Search for 0x03000000 references
print(f"\n=== ROM references to IWRAM base addresses ===")
for addr in [0x03000000, 0x03001000, 0x03000100]:
    addr_bytes = struct.pack('<I', addr)
    positions = []
    pos = 0
    while True:
        pos = rom.find(addr_bytes, pos)
        if pos == -1:
            break
        positions.append(pos)
        pos += 1
    if positions:
        print(f"0x{addr:08X}: found at {len(positions)} ROM locations")
        for p in positions[:4]:
            print(f"  ROM 0x{p:X} (0x{0x08000000+p:08X})")

# Look for potential copy loop patterns
# THUMB: LDMIA/STMIA patterns
print(f"\n=== Looking for bulk copy patterns ===")

# Find 0xF928F000 in ROM
v = 0xF928F000
v_bytes = struct.pack('<I', v)
pos = rom.find(v_bytes)
if pos != -1:
    print(f"Value 0x{v:08X} found at ROM 0x{pos:X}")
    # Show surrounding context
    print(f"ROM context at 0x{pos:X}:")
    for i in range(-8, 24, 4):
        offset = pos + i
        if offset >= 0 and offset + 4 <= len(rom):
            w = struct.unpack('<I', rom[offset:offset+4])[0]
            mark = " <-- target" if i == 0 else ""
            print(f"  0x{offset:X}: 0x{w:08X}{mark}")
