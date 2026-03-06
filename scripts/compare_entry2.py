#!/usr/bin/env python3
"""Compare entry data at offset 0x6B3C (entry[3]) between Python and emulator,
and find the exact point where decompressed data diverges (ignoring relocated offsets)."""
import struct

with open("/tmp/nsf_chunk_python.bin", "rb") as f:
    python_data = bytearray(f.read())

with open("/tmp/nsf_chunk_emulator.bin", "rb") as f:
    emu_data = bytearray(f.read())

# The data outside of offset tables should match
# Let's look at a large block that should be actual data (not offsets)
# Entry[2] starts at 0x850, has type=4 (SortList), items=13
# Its data should be large — 25324 bytes up to 0x6B3C

# Let me find where WITHIN entry[2] the data starts to diverge
# Entry[2] header: magic(4) + EID(4) + type(4) + itemCount(4) = 16 bytes
# Then item offsets: (itemCount+1) * 4 bytes = 14 * 4 = 56 bytes
# Then actual item data

entry2_start = 0x0850

# Check entry[2] header first
print("Entry[2] header (0x850-0x860):")
for off in range(entry2_start, entry2_start + 16, 4):
    py_w = struct.unpack_from("<I", python_data, off)[0]
    em_w = struct.unpack_from("<I", emu_data, off)[0]
    match = "OK" if py_w == em_w else "DIFF"
    print(f"  0x{off:04X}: py=0x{py_w:08X} emu=0x{em_w:08X} [{match}]")

# Item offsets (relocated in emulator)
item_count = struct.unpack_from("<I", python_data, entry2_start + 12)[0]
print(f"\nEntry[2] item count: {item_count}")

items_table_start = entry2_start + 16
items_table_end = items_table_start + (item_count + 1) * 4

print(f"Item offset table (0x{items_table_start:04X}-0x{items_table_end:04X}):")
base = 0x800B4114
for i in range(item_count + 1):
    off = items_table_start + i * 4
    py_w = struct.unpack_from("<I", python_data, off)[0]
    em_w = struct.unpack_from("<I", emu_data, off)[0]
    # Check if emulator = python + base
    is_relocated = (em_w == py_w + base)
    print(f"  item[{i}] 0x{off:04X}: py=0x{py_w:08X} emu=0x{em_w:08X} "
          f"{'relocated' if is_relocated else 'MISMATCH!'}")

# Now find where actual data (after item table) diverges
data_start = items_table_end
print(f"\nScanning data from 0x{data_start:04X} to 0x6B3C:")

# Find first difference in actual data region
first_diff = None
for i in range(data_start, 0x6B3C):
    if python_data[i] != emu_data[i]:
        first_diff = i
        break

if first_diff is None:
    print("  Data region is IDENTICAL up to entry[3]!")
else:
    print(f"  First data diff at 0x{first_diff:04X}")
    # Show context
    start = max(data_start, first_diff - 32)
    end = min(0x6B3C, first_diff + 64)
    for off in range(start, end, 16):
        py_hex = python_data[off:off+16].hex()
        em_hex = emu_data[off:off+16].hex()
        marker = " <--" if first_diff >= off and first_diff < off + 16 else ""
        print(f"    0x{off:04X}: py={py_hex}")
        print(f"            emu={em_hex}{marker}")

# Also check if maybe there are MORE relocated offsets inside the entry data
# SortList entries might have sub-item offsets that are also relocated
# Let me dump the first 256 bytes of data after item table
print(f"\nFirst 64 bytes of actual item data (0x{data_start:04X}):")
for off in range(data_start, min(data_start + 64, 0x6B3C), 4):
    py_w = struct.unpack_from("<I", python_data, off)[0]
    em_w = struct.unpack_from("<I", emu_data, off)[0]
    match = "OK" if py_w == em_w else "DIFF"
    relocated = "relo" if em_w == py_w + base else ""
    print(f"  0x{off:04X}: py=0x{py_w:08X} emu=0x{em_w:08X} [{match}] {relocated}")
