#!/usr/bin/env python3
"""Dump Python-decompressed bytes around the corruption point for comparison with emulator."""
import struct

BIN_PATH = "test_roms/Crash Bandicoot (USA).bin"

with open(BIN_PATH, "rb") as f:
    data = f.read()

lba = 51342
chunk_data = bytearray()
for i in range(20):
    offset = (lba + i) * 2352 + 24
    chunk_data.extend(data[offset:offset + 2048])

length = struct.unpack_from("<I", chunk_data, 4)[0]
skip = struct.unpack_from("<I", chunk_data, 8)[0]

output = bytearray(0x10000)
src = 12
dst = 0

while dst < length:
    if src >= len(chunk_data):
        break
    prefix = chunk_data[src]
    src += 1
    if prefix & 0x80:
        if src >= len(chunk_data):
            break
        seek_byte = chunk_data[src]
        src += 1
        seek = ((prefix & 0x7F) << 5) | (seek_byte >> 3)
        span_bits = seek_byte & 7
        span = 64 if span_bits == 7 else span_bits + 3
        if seek == 0:
            break
        for j in range(span):
            if dst >= 0x10000:
                break
            ref_pos = dst - seek
            if ref_pos < 0:
                output[dst] = 0
            else:
                output[dst] = output[ref_pos]
            dst += 1
    else:
        if prefix == 0:
            continue
        for j in range(prefix):
            if dst >= 0x10000 or src >= len(chunk_data):
                break
            output[dst] = chunk_data[src]
            src += 1
            dst += 1

skip_src = src
skip_dst = 0x10000 - skip
for i in range(skip):
    if skip_src + i < len(chunk_data) and skip_dst + i < 0x10000:
        output[skip_dst + i] = chunk_data[skip_src + i]

# Save the full decompressed output for comparison
with open("/tmp/nsf_chunk_python.bin", "wb") as f:
    f.write(output)

print(f"Saved {len(output)} bytes to /tmp/nsf_chunk_python.bin")

# Dump bytes around entry[3] offset (0x6B3C)
print("\nPython decompressed bytes 0x6B30-0x6B50:")
for off in range(0x6B30, 0x6B50, 4):
    w = struct.unpack_from("<I", output, off)[0]
    print(f"  0x{off:04X}: {output[off:off+4].hex()} (0x{w:08X})")

# Entry magic should be at 0x6B3C
magic_at_3 = struct.unpack_from("<I", output, 0x6B3C)[0]
print(f"\nEntry[3] magic at 0x6B3C: 0x{magic_at_3:08X}")
print(f"  Expected: 0x0100FFFF")
print(f"  Match: {magic_at_3 == 0x0100FFFF}")

# Show what the EMULATOR had at 0x6B3C (it was at 0x800BAC50 = base + 0x6B3C)
# Emulator had magic=0x67CCFB01 at 0x6B3C
# But found FF FF 00 01 at 0x6B39 (3 bytes earlier)
print(f"\nEmulator had at 0x6B3C: 0x67CCFB01")
print(f"Emulator found magic at 0x6B39 (3 bytes earlier)")
print(f"\nPython byte at 0x6B39: 0x{output[0x6B39]:02X}")
print(f"Python bytes 0x6B39-0x6B3C: {output[0x6B39:0x6B3D].hex()}")
