#!/usr/bin/env python3
"""Compare the Python-decompressed and emulator-decompressed NSF chunks byte by byte."""

with open("/tmp/nsf_chunk_python.bin", "rb") as f:
    python_data = f.read()

with open("/tmp/nsf_chunk_emulator.bin", "rb") as f:
    emu_data = f.read()

print(f"Python size: {len(python_data)}")
print(f"Emulator size: {len(emu_data)}")

# Find first difference
first_diff = None
for i in range(min(len(python_data), len(emu_data))):
    if python_data[i] != emu_data[i]:
        first_diff = i
        break

if first_diff is None:
    print("Files are IDENTICAL!")
else:
    print(f"\nFirst difference at offset 0x{first_diff:04X} ({first_diff})")
    print(f"  Python:  0x{python_data[first_diff]:02X}")
    print(f"  Emulator: 0x{emu_data[first_diff]:02X}")

    # Show context around the first difference
    start = max(0, first_diff - 16)
    end = min(len(python_data), first_diff + 48)

    print(f"\nContext (offset 0x{start:04X}-0x{end:04X}):")
    print("  Offset   Python                            Emulator")
    for off in range(start, end, 16):
        py_hex = python_data[off:off+16].hex()
        em_hex = emu_data[off:off+16].hex()
        marker = " <--" if first_diff >= off and first_diff < off + 16 else ""
        print(f"  0x{off:04X}: {py_hex}  {em_hex}{marker}")

    # Count total differences
    diff_count = sum(1 for i in range(len(python_data)) if python_data[i] != emu_data[i])
    print(f"\nTotal differing bytes: {diff_count}")

    # Check if data is shifted — try to find the emulator data in python data
    # Look for a consistent shift
    for shift in range(-10, 11):
        if shift == 0:
            continue
        matches = 0
        mismatches = 0
        check_start = max(0, first_diff)
        check_end = min(len(python_data), first_diff + 1000)
        for i in range(check_start, check_end):
            emu_idx = i
            py_idx = i + shift
            if 0 <= py_idx < len(python_data) and 0 <= emu_idx < len(emu_data):
                if python_data[py_idx] == emu_data[emu_idx]:
                    matches += 1
                else:
                    mismatches += 1
        total = matches + mismatches
        if total > 0 and matches / total > 0.9:
            print(f"\n  Shift {shift:+d}: {matches}/{total} matches "
                  f"({100*matches/total:.1f}%) — EMU is Python shifted by {shift}")

    # Look for the shift at entry[3] area (0x6B3C)
    print(f"\nAround entry[3] offset (0x6B3C):")
    for off in range(0x6B30, 0x6B50, 4):
        import struct
        py_w = struct.unpack_from("<I", python_data, off)[0]
        em_w = struct.unpack_from("<I", emu_data, off)[0]
        match = "OK" if py_w == em_w else "DIFF"
        print(f"  0x{off:04X}: py=0x{py_w:08X} emu=0x{em_w:08X} [{match}]")
