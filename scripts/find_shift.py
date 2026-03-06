#!/usr/bin/env python3
"""Find the exact shift at the first divergence point (0x5380)."""
import struct

with open("/tmp/nsf_chunk_python.bin", "rb") as f:
    python_data = bytearray(f.read())

with open("/tmp/nsf_chunk_emulator.bin", "rb") as f:
    emu_data = bytearray(f.read())

# First divergence at 0x5380
# Show 32 bytes before and after
start = 0x5370
end = 0x53C0

print("Byte-by-byte comparison around 0x5380:")
print(f"{'Offset':>8}  {'Python':>4}  {'Emulator':>4}  Match")
for i in range(start, end):
    py = python_data[i]
    em = emu_data[i]
    match = "OK" if py == em else "**"
    print(f"  0x{i:04X}    0x{py:02X}    0x{em:02X}    {match}")

# Try to find where emulator data == python data with various shifts
print("\nShift analysis at divergence point:")
for shift in range(-8, 9):
    matches = 0
    checked = 0
    for i in range(0x5380, 0x5400):
        em_i = i + shift
        if 0 <= em_i < len(emu_data):
            checked += 1
            if python_data[i] == emu_data[em_i]:
                matches += 1
    if checked > 0:
        pct = 100 * matches / checked
        print(f"  py[x] == emu[x{shift:+d}]: {matches}/{checked} ({pct:.1f}%)")

# Check a BIGGER range with the winning shift
print("\nLarge-range shift analysis (0x5380-0x6B3C):")
for shift in range(-8, 9):
    matches = 0
    checked = 0
    for i in range(0x5380, 0x6B3C):
        em_i = i + shift
        if 0 <= em_i < len(emu_data):
            checked += 1
            if python_data[i] == emu_data[em_i]:
                matches += 1
    if checked > 0:
        pct = 100 * matches / checked
        if pct > 85:
            print(f"  py[x] == emu[x{shift:+d}]: {matches}/{checked} ({pct:.1f}%) ***")
        else:
            print(f"  py[x] == emu[x{shift:+d}]: {matches}/{checked} ({pct:.1f}%)")

# Now check if the emulator data starting at 0x5380 can be found in Python data
# by searching for the emulator byte sequence
print("\nSearching for emulator bytes 0x5380-0x5390 in Python data:")
emu_pattern = emu_data[0x5380:0x5390]
print(f"  Emulator pattern: {emu_pattern.hex()}")
for search_start in range(0x5370, 0x53A0):
    py_slice = python_data[search_start:search_start + 16]
    if py_slice == emu_pattern:
        print(f"  Found at Python offset 0x{search_start:04X}!")
    # Also check partial matches
    match_count = sum(1 for a, b in zip(py_slice, emu_pattern) if a == b)
    if match_count >= 12:
        print(f"  Partial match ({match_count}/16) at Python 0x{search_start:04X}: {py_slice.hex()}")
