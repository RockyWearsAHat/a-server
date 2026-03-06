#!/usr/bin/env python3
"""Generate expected SB trace for the corruption region from the correct Python decompression."""

# Read the correct decompressed data
with open('/tmp/nsf_chunk_python.bin', 'rb') as f:
    correct = f.read()

# Page buffer base address
PAGE_ADDR = 0x800B4114

# Show what bytes should be at the corruption region
# Corruption at offset 0x5380 = address 0x800B9494
# Our SB trace covers 0x000B9470 - 0x000B94C0 (physical)
# = 0x800B9470 - 0x800B94C0 (KSEG0)
# Offsets: 0x535C - 0x53AC

print("=== Expected bytes at corruption region ===")
print("(offset from page buffer, address, expected byte)")
for offset in range(0x5350, 0x53C0):
    addr = PAGE_ADDR + offset
    byte = correct[offset]
    print(f"  offset=0x{offset:04X} addr=0x{addr:08X} expected=0x{byte:02X}")
