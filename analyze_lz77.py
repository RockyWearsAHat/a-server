#!/usr/bin/env python3
import struct

# Read ROM
with open('OG-DK.gba', 'rb') as f:
    rom = f.read()

src = 0x5FF4
header = struct.unpack('<I', rom[src:src+4])[0]
decomp_size = header >> 8

# Manual LZ77 decompression for GBA
def decompress_lz77(data, src_offset):
    header = struct.unpack('<I', data[src_offset:src_offset+4])[0]
    size = header >> 8
    out = bytearray(size)
    
    s = src_offset + 4
    d = 0
    
    while d < size:
        flags = data[s]
        s += 1
        
        for i in range(7, -1, -1):
            if d >= size:
                break
                
            if flags & (1 << i):
                # Compressed block
                byte1 = data[s]
                byte2 = data[s + 1]
                s += 2
                
                length = ((byte1 >> 4) & 0xF) + 3
                offset = ((byte1 & 0xF) << 8) | byte2
                offset += 1
                
                for j in range(length):
                    if d >= size:
                        break
                    out[d] = out[d - offset]
                    d += 1
            else:
                # Literal byte
                out[d] = data[s]
                s += 1
                d += 1
    
    return out

decompressed = decompress_lz77(rom, src)
print(f'Decompressed {len(decompressed)} bytes')
print()
print('First 32 bytes (should match debug log):')
for i in range(0, 32, 4):
    val = struct.unpack('<I', decompressed[i:i+4])[0]
    print(f'  [0x{i:04X}]: 0x{val:08x}')

print()
print('Bytes at offset 0x10c (palette buffer area):')
for i in range(0x10c, min(0x10c+32, len(decompressed)), 4):
    if i + 4 <= len(decompressed):
        val = struct.unpack('<I', decompressed[i:i+4])[0]
    else:
        val = 0
    print(f'  [0x{i:04X}]: 0x{val:08x}')

print()
print(f'Total decompressed size: {len(decompressed)}')
print(f'Bytes from 0x10c onwards: {len(decompressed) - 0x10c} bytes available')
