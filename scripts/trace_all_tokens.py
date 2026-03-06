#!/usr/bin/env python3
"""Trace every token in the decompressor and log each SB write with register state.
Produces output comparable to the emulator's SB_TRACE log."""

import struct, sys

CHUNK_FILE = "/tmp/nsf_chunk_emulator.bin"  # We use the raw compressed chunk from disc
DMA_BASE = 0x800BA114
PAGE_BASE = 0x800B4114

# Read the compressed chunk from the emulator's memory dump
# Actually, we need the raw compressed data. Let me read from the disc image.
DISC = "/Users/alexwaldmann/Desktop/AIO Server/test_roms/Crash Bandicoot (USA).bin"
LBA_START = 51342
SECTOR_COUNT = 20

def read_compressed_data():
    """Read 20 sectors from the disc image starting at LBA 51342."""
    with open(DISC, "rb") as f:
        data = bytearray()
        for i in range(SECTOR_COUNT):
            sector_offset = (LBA_START + i) * 2352
            f.seek(sector_offset + 24)  # Skip sync + header + subheader
            data.extend(f.read(2048))   # Read user data
        return bytes(data)

def decompress_and_trace(compressed):
    """Decompress using the prefix-byte algorithm, logging every SB write."""
    
    # Parse header
    magic = struct.unpack_from('<H', compressed, 0)[0]
    decompressed_length = struct.unpack_from('<I', compressed, 4)[0]
    skip = struct.unpack_from('<I', compressed, 8)[0]
    
    print(f"# Header: magic=0x{magic:04X}, decompLen={decompressed_length}, skip={skip}")
    
    output = bytearray(65536)  # 64KB page buffer
    src = 12  # Skip 12-byte header
    dst = 0
    remaining = decompressed_length
    write_count = 0
    token_num = 0
    
    while remaining > 0 and src < len(compressed):
        prefix = compressed[src]
        token_src = src
        src += 1
        
        if prefix & 0x80:
            # Back-reference
            seek_byte = compressed[src]
            src += 1
            combined = (prefix << 8) | seek_byte
            seek = (combined >> 3) & 0xFFF
            span_bits = combined & 7
            
            if span_bits == 7:
                span = 64
            else:
                span = span_bits + 3
            
            remaining -= span
            
            src_offset = dst - seek
            for i in range(span):
                byte_val = output[src_offset + i]
                
                # Log this write
                emu_addr = PAGE_BASE + dst
                emu_t6 = DMA_BASE + src
                emu_t7 = PAGE_BASE + dst
                offset = dst
                
                # Sparse logging: every 256th write, plus dense near corruption (offset >= 0x5350)
                near_corruption = offset >= 0x5350
                if near_corruption or (write_count % 256 == 0):
                    print(f"SB_TRACE_PY: #{write_count} off=0x{offset:04X} val=0x{byte_val:02X} "
                          f"type=BREF token={token_num} a2=0x{remaining & 0xFFFF:04X}({remaining}) "
                          f"t6_off={src} span={span} seek={seek}")
                
                output[dst] = byte_val
                dst += 1
                write_count += 1
        else:
            # Literal run
            literal_count = prefix
            if literal_count == 0:
                token_num += 1
                continue
            
            remaining -= literal_count
            
            for i in range(literal_count):
                byte_val = compressed[src]
                
                offset = dst
                near_corruption = offset >= 0x5350
                if near_corruption or (write_count % 256 == 0):
                    print(f"SB_TRACE_PY: #{write_count} off=0x{offset:04X} val=0x{byte_val:02X} "
                          f"type=LIT  token={token_num} a2=0x{remaining & 0xFFFF:04X}({remaining}) "
                          f"t6_off={src} count={literal_count}")
                
                output[dst] = byte_val
                dst += 1
                src += 1
                write_count += 1
        
        token_num += 1
    
    # Now handle skip bytes
    print(f"\n# LZ done: dst={dst}, remaining={remaining}, src={src}, tokens={token_num}")
    print(f"# Total SB writes during LZ: {write_count}")
    
    # Copy skip bytes
    skip_start = 65536 - skip
    for i in range(skip):
        output[skip_start + i] = compressed[src + i]
    
    # Save output
    with open("/tmp/nsf_chunk_python_trace.bin", "wb") as f:
        f.write(output)
    
    return output

if __name__ == "__main__":
    compressed = read_compressed_data()
    print(f"# Compressed data: {len(compressed)} bytes")
    output = decompress_and_trace(compressed)
    
    # Compare with known-good Python output
    with open("/tmp/nsf_chunk_python.bin", "rb") as f:
        expected = f.read()
    
    if output == bytearray(expected):
        print("\n# ✅ Output matches known-good Python decompression")
    else:
        for i in range(min(len(output), len(expected))):
            if output[i] != expected[i]:
                print(f"\n# ❌ First difference at offset 0x{i:04X}: got 0x{output[i]:02X}, expected 0x{expected[i]:02X}")
                break
