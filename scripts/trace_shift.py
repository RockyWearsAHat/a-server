#!/usr/bin/env python3
"""Trace the shift pattern throughout the corrupted region."""
import struct

with open("/tmp/nsf_chunk_python.bin", "rb") as f:
    python_data = bytearray(f.read())

with open("/tmp/nsf_chunk_emulator.bin", "rb") as f:
    emu_data = bytearray(f.read())

# For each position from 0x5380 to 0x6B3C, find the best local shift
# by looking at which shift gives the best match in a small window
window = 32

print("Shift pattern from 0x5380 to 0x6B3C:")
print(f"{'Offset':>8}  {'Best Shift':>10}  {'Match%':>8}  Notes")

prev_shift = 0
for pos in range(0x5370, 0x6B40, 16):
    best_shift = 0
    best_matches = 0
    for shift in range(-8, 9):
        matches = 0
        checked = 0
        for i in range(pos, min(pos + window, len(python_data))):
            em_i = i + shift
            if 0 <= em_i < len(emu_data):
                checked += 1
                if python_data[i] == emu_data[em_i]:
                    matches += 1
        if checked > 0 and matches > best_matches:
            best_matches = matches
            best_shift = shift
    
    pct = 100 * best_matches / window if window > 0 else 0
    changed = " <-- SHIFT CHANGE" if best_shift != prev_shift else ""
    print(f"  0x{pos:04X}     {best_shift:+d}       {pct:5.1f}%  {changed}")
    prev_shift = best_shift

# Also look for the SECOND shift point — when does it go from one shift to another?
print("\n\nByte-by-byte shift tracking (checking shift 0 and shift -3):")
print(f"{'Offset':>8}  py  emu  py==emu?  emu[+3]==py?  emu[-3]==py?")
for i in range(0x5370, 0x53B0):
    py = python_data[i]
    em = emu_data[i]
    em_p3 = emu_data[i + 3] if i + 3 < len(emu_data) else 0xFF
    em_m3 = emu_data[i - 3] if i - 3 >= 0 else 0xFF
    eq = "YES" if py == em else "   "
    eqp3 = "YES" if py == em_p3 else "   "
    eqm3 = "YES" if py == em_m3 else "   "
    print(f"  0x{i:04X}  {py:02X}  {em:02X}   {eq}       {eqp3}          {eqm3}")
