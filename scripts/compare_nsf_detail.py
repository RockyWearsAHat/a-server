#!/usr/bin/env python3
"""Find the first byte where ENTRY DATA diverges (ignoring the offset table relocation)."""
import struct

with open("/tmp/nsf_chunk_python.bin", "rb") as f:
    python_data = bytearray(f.read())

with open("/tmp/nsf_chunk_emulator.bin", "rb") as f:
    emu_data = bytearray(f.read())

# The header is 0x30 bytes (magic+type+CID+entryCount+checksum + 8 offsets)
# Offsets 0x10-0x2F are relocated (absolute vs raw) — skip those
# Entry data starts at 0x30 in both

print("Checking header (0x00-0x0F):")
for off in range(0, 0x10, 4):
    py_w = struct.unpack_from("<I", python_data, off)[0]
    em_w = struct.unpack_from("<I", emu_data, off)[0]
    match = "OK" if py_w == em_w else "DIFF"
    print(f"  0x{off:04X}: py=0x{py_w:08X} emu=0x{em_w:08X} [{match}]")

print("\nOffset table (0x10-0x2F) — expected to differ (relocation):")
base = 0x800B4114
for i in range(8):
    off = 0x10 + i * 4
    py_w = struct.unpack_from("<I", python_data, off)[0]
    em_w = struct.unpack_from("<I", emu_data, off)[0]
    relocated = py_w + base if py_w != 0 else 0
    match_reloc = "OK" if em_w == relocated else "DIFF"
    print(f"  0x{off:04X}: py=0x{py_w:08X} emu=0x{em_w:08X} "
          f"(py+base=0x{relocated:08X}) [{match_reloc}]")

print("\nEntry data (starting at 0x30):")
first_data_diff = None
for i in range(0x30, len(python_data)):
    if python_data[i] != emu_data[i]:
        first_data_diff = i
        break

if first_data_diff is None:
    print("  Entry data is IDENTICAL!")
else:
    print(f"  First data difference at offset 0x{first_data_diff:04X}")
    print(f"    Python:  0x{python_data[first_data_diff]:02X}")
    print(f"    Emulator: 0x{emu_data[first_data_diff]:02X}")
    
    # Show context
    start = max(0x30, first_data_diff - 32)
    end = min(len(python_data), first_data_diff + 64)
    print(f"\n  Context around first data difference:")
    for off in range(start, end, 16):
        py_hex = python_data[off:off+16].hex()
        em_hex = emu_data[off:off+16].hex()
        marker = ""
        if first_data_diff >= off and first_data_diff < off + 16:
            marker = " <-- FIRST DIFF"
        print(f"    0x{off:04X}: py={py_hex}")
        print(f"           emu={em_hex}{marker}")
    
    # Check shift hypothesis
    print(f"\n  Checking if emulator data is shifted relative to Python:")
    for shift in range(-8, 9):
        if shift == 0:
            continue
        matches = 0
        total = 0
        for i in range(first_data_diff, min(first_data_diff + 2000, len(python_data))):
            py_idx = i
            em_idx = i + shift
            if 0 <= em_idx < len(emu_data):
                total += 1
                if python_data[py_idx] == emu_data[em_idx]:
                    matches += 1
        if total > 0:
            pct = 100 * matches / total
            if pct > 50:
                print(f"    Shift {shift:+d}: {matches}/{total} ({pct:.1f}%)")
    
    # Count bytes that match with -3 shift from first_data_diff
    print(f"\n  Detailed shift=-3 check:")
    run_match = 0
    run_start = first_data_diff
    for i in range(first_data_diff, min(first_data_diff + 200, len(python_data))):
        py_byte = python_data[i]
        em_idx = i - 3
        if 0 <= em_idx < len(emu_data):
            em_byte = emu_data[em_idx]
            match = py_byte == em_byte
            if match:
                run_match += 1
            if i < first_data_diff + 40:
                print(f"    py[0x{i:04X}]=0x{py_byte:02X}  "
                      f"emu[0x{em_idx:04X}]=0x{em_byte:02X}  "
                      f"{'MATCH' if match else 'DIFF'}")
    print(f"  Total matches with shift=-3: {run_match}/200")
