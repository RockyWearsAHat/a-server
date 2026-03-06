#!/usr/bin/env python3
"""Extract and examine the compressed NSF chunk from the Crash Bandicoot disc image.

The compressed chunk is read in the second CmdReadN batch:
  SetLoc(11:26:42) → CmdReadN → 20 sectors (42 through 61)
  LBA = (11*60 + 26)*75 + 42 - 150 = 51342
  Disc offset = 51342 * 2352 = 0x7329898, + 24 header skip = 0x73298B0

The disc is a raw 2352-byte-per-sector BIN file.
"""
import struct

BIN_PATH = "test_roms/Crash Bandicoot (USA).bin"


def main():
    with open(BIN_PATH, "rb") as f:
        data = f.read()

    mm, ss, ff = 11, 26, 42
    lba = (mm * 60 + ss) * 75 + ff - 150
    num_sectors = 20

    print(f"Reading {num_sectors} sectors from LBA {lba} (MSF {mm}:{ss:02d}:{ff:02d})")

    chunk_data = bytearray()
    for i in range(num_sectors):
        sector_lba = lba + i
        offset = sector_lba * 2352 + 24
        chunk_data.extend(data[offset:offset + 2048])

    print(f"Total raw data: {len(chunk_data)} bytes (0x{len(chunk_data):X})")

    magic = struct.unpack_from("<H", chunk_data, 0)[0]
    print(f"Magic: 0x{magic:04X}")

    if magic != 0x1235:
        print(f"Not compressed! First 32 bytes: {chunk_data[:32].hex()}")
        if magic == 0x1234:
            print("Chunk is UNCOMPRESSED")
            analyze_chunk(chunk_data)
        return

    print("Chunk is COMPRESSED (0x1235)")

    comp_len = struct.unpack_from("<I", chunk_data, 4)[0]
    skip = struct.unpack_from("<I", chunk_data, 8)[0]
    print(f"Compressed length: {comp_len} (0x{comp_len:X})")
    print(f"Skip (uncompressed tail): {skip} (0x{skip:X})")
    print(f"First 48 bytes: {chunk_data[:48].hex()}")

    decompressed = decompress_nsf(chunk_data)
    if decompressed is None:
        print("Decompression failed!")
        return

    print(f"\nDecompressed: {len(decompressed)} bytes")
    analyze_chunk(decompressed)


def decompress_nsf(compressed):
    """Decompress NSF chunk using the Crash Bandicoot algorithm.

    Based on CrashEdit source (NSF.cs ReadChunk).

    Format:
      Bytes 0-1:  magic 0x1235
      Bytes 2-3:  padding
      Bytes 4-7:  length (decompressed byte count of LZ region)
      Bytes 8-11: skip (uncompressed tail byte count)

    Prefix-byte scheme (NOT flag-byte/bitfield):
      - Read prefix byte
      - If bit 7 set: back-reference
        - Read second byte (seek_byte)
        - seek = ((prefix & 0x7F) << 5) | (seek_byte >> 3)  — 12-bit relative distance
        - span = (seek_byte & 7) + 3  (or 64 if (seek_byte & 7) == 7)
        - Copy span bytes from (dst - seek) — byte-by-byte for overlap
      - If bit 7 clear: literal run
        - Copy prefix raw bytes from input to output

    After LZ region: skip bytes of uncompressed data appended to end of 64KB buffer.
    """
    magic = struct.unpack_from("<H", compressed, 0)[0]
    if magic != 0x1235:
        return None

    length = struct.unpack_from("<I", compressed, 4)[0]
    skip = struct.unpack_from("<I", compressed, 8)[0]

    output = bytearray(0x10000)
    src = 12
    dst = 0

    while dst < length:
        if src >= len(compressed):
            print(f"  WARNING: ran out of compressed data at src={src}, dst={dst}")
            break

        prefix = compressed[src]
        src += 1

        if prefix & 0x80:
            # Back-reference
            if src >= len(compressed):
                break
            seek_byte = compressed[src]
            src += 1

            seek = ((prefix & 0x7F) << 5) | (seek_byte >> 3)
            span_bits = seek_byte & 7
            span = 64 if span_bits == 7 else span_bits + 3

            if seek == 0:
                print(f"  WARNING: seek=0 at dst={dst}, src={src}")
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
            # Literal run: copy prefix bytes
            if prefix == 0:
                continue
            for j in range(prefix):
                if dst >= 0x10000 or src >= len(compressed):
                    break
                output[dst] = compressed[src]
                src += 1
                dst += 1

    # Copy uncompressed tail to end of 64KB buffer
    # Skip some padding bytes in input, then copy `skip` raw bytes
    skip_src = src
    skip_dst = 0x10000 - skip
    for i in range(skip):
        if skip_src + i < len(compressed) and skip_dst + i < 0x10000:
            output[skip_dst + i] = compressed[skip_src + i]

    print(f"  Decompression: src={src - 12} bytes consumed, dst={dst} bytes written, "
          f"length={length}, skip={skip}")
    return output


def analyze_chunk(chunk_data):
    """Analyze a decompressed NSF chunk."""
    dec_magic = struct.unpack_from("<H", chunk_data, 0)[0]
    chunk_type = struct.unpack_from("<H", chunk_data, 2)[0]
    cid = struct.unpack_from("<I", chunk_data, 4)[0]
    entry_count = struct.unpack_from("<I", chunk_data, 8)[0]
    checksum = struct.unpack_from("<I", chunk_data, 0x0C)[0]

    print(f"  Magic: 0x{dec_magic:04X}")
    print(f"  Type: {chunk_type}")
    print(f"  CID: {cid}")
    print(f"  Entry count: {entry_count}")
    print(f"  Checksum: 0x{checksum:08X}")

    if entry_count > 20:
        print("  ERROR: too many entries")
        return

    offsets = []
    for i in range(entry_count + 1):
        off = struct.unpack_from("<I", chunk_data, 0x10 + i * 4)[0]
        offsets.append(off)

    print("\n  Entry offset table:")
    for i, off in enumerate(offsets):
        label = f"entry[{i}]" if i < entry_count else "end"
        print(f"    {label}: 0x{off:04X}")

    print("\n  Entry details:")
    for i in range(entry_count):
        off = offsets[i]
        end = offsets[i + 1] if i + 1 < len(offsets) else len(chunk_data)
        size = end - off

        if off + 16 > len(chunk_data):
            print(f"    entry[{i}]: offset 0x{off:04X} out of range!")
            continue

        entry_magic = struct.unpack_from("<I", chunk_data, off)[0]
        entry_eid = struct.unpack_from("<I", chunk_data, off + 4)[0]
        entry_type = struct.unpack_from("<I", chunk_data, off + 8)[0]
        entry_items = struct.unpack_from("<I", chunk_data, off + 12)[0]

        valid = "OK" if entry_magic == 0x0100FFFF else "BAD"
        print(f"    entry[{i}] 0x{off:04X}-0x{end:04X} ({size}B): "
              f"magic=0x{entry_magic:08X} EID=0x{entry_eid:08X} "
              f"type={entry_type} items={entry_items} [{valid}]")

        if entry_magic != 0x0100FFFF:
            for delta in range(-8, 9):
                test_off = off + delta
                if 0 <= test_off + 4 <= len(chunk_data):
                    test_val = struct.unpack_from("<I", chunk_data, test_off)[0]
                    if test_val == 0x0100FFFF:
                        print(f"      Found magic at offset+{delta} (0x{test_off:04X})")

            # Show raw bytes around the entry
            start = max(0, off - 8)
            end_show = min(len(chunk_data), off + 24)
            raw = chunk_data[start:end_show]
            print(f"      Raw bytes [{start:#06x}-{end_show:#06x}]: {raw.hex()}")


if __name__ == "__main__":
    main()
