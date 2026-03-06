#!/usr/bin/env python3
"""Generate a per-token trace matching the emulator's DECOMP_TOKEN format.
For each token, log: token#, prefix byte, a2 (remaining BEFORE subtraction),
t6 (src ptr), t7 (dst ptr), dst_off."""

import struct

DISC = "/Users/alexwaldmann/Desktop/AIO Server/test_roms/Crash Bandicoot (USA).bin"
LBA_START = 51342
SECTOR_COUNT = 20
DMA_BASE = 0x800BA114
PAGE_BASE = 0x800B4114

def read_compressed_data():
    with open(DISC, "rb") as f:
        data = bytearray()
        for i in range(SECTOR_COUNT):
            sector_offset = (LBA_START + i) * 2352
            f.seek(sector_offset + 24)
            data.extend(f.read(2048))
        return bytes(data)

def trace_tokens(compressed):
    magic = struct.unpack_from('<H', compressed, 0)[0]
    decompressed_length = struct.unpack_from('<I', compressed, 4)[0]
    skip = struct.unpack_from('<I', compressed, 8)[0]
    
    src = 12
    dst = 0
    remaining = decompressed_length
    token_num = 0
    
    while remaining > 0 and src < len(compressed):
        prefix = compressed[src]
        t6_before = DMA_BASE + src  # t6 points to prefix before read
        t7 = PAGE_BASE + dst
        dst_off = dst & 0xFFFF
        
        # At the ANDI instruction, a0 = prefix, a2 = remaining (not yet subtracted)
        print(f"DECOMP_TOKEN_PY: #{token_num} prefix=0x{prefix:02X} a2={remaining} "
              f"t6=0x{t6_before:08X} t7=0x{t7:08X} dst_off=0x{dst_off:04X}")
        
        src += 1
        
        if prefix & 0x80:
            seek_byte = compressed[src]
            src += 1
            combined = (prefix << 8) | seek_byte
            seek = (combined >> 3) & 0xFFF
            span_bits = combined & 7
            span = 64 if span_bits == 7 else span_bits + 3
            
            remaining -= span
            
            src_offset = dst - seek
            for i in range(span):
                dst += 1
        else:
            literal_count = prefix
            remaining -= literal_count
            
            for i in range(literal_count):
                src += 1
                dst += 1
                
            if literal_count == 0:
                pass  # Empty literal, just skip
        
        token_num += 1
    
    print(f"\n# Total tokens: {token_num}")
    print(f"# Final: src={src}, dst={dst}, remaining={remaining}")

if __name__ == "__main__":
    compressed = read_compressed_data()
    trace_tokens(compressed)
