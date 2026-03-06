#!/usr/bin/env python3
import struct

path = "test_roms/Crash Bandicoot (USA).bin"
base_lba = (11*60 + 26) * 75 + 42 - 150

# The struct at 0x800BB070 is at offset 0xF5C from DMA start (0x800BA114)
struct_off_from_dma = 0xF5C
sector_idx = struct_off_from_dma // 2048  # sector 1
byte_in_sector = struct_off_from_dma % 2048  # 0x75C

sector_lba = base_lba + sector_idx
disc_offset = sector_lba * 2352 + 24 + byte_in_sector

print(f"struct_off_from_dma = 0x{struct_off_from_dma:X}")
print(f"sector_idx = {sector_idx}, byte_in_sector = 0x{byte_in_sector:X}")
print(f"disc_offset = 0x{disc_offset:X}")

with open(path, "rb") as f:
    f.seek(disc_offset)
    data = f.read(32)
    print(f"\nDisc data at 0x{disc_offset:X} (should match RAM at 0x800BB070):")
    for i in range(0, len(data), 4):
        word = struct.unpack_from("<I", data, i)[0]
        print(f"  +{i:02X}: {word:08X}")

print("\nEmulator RAM (from log):")
print("  +00: 800BB070  (self-pointer)")
print("  +04: 02130300")
print("  +08: 00000100  (index = 256)")

# Also dump the first 64 bytes of the first sector for verification
print("\n--- First sector data (first 64 bytes) ---")
first_sector_offset = base_lba * 2352 + 24
with open(path, "rb") as f:
    f.seek(first_sector_offset)
    data = f.read(64)
    print(f"Disc offset 0x{first_sector_offset:X}:")
    for i in range(0, len(data), 4):
        word = struct.unpack_from("<I", data, i)[0]
        print(f"  +{i:02X}: {word:08X}")
    print(f"\nDMA log first word: 00001235")
    print(f"Disc first word:    {struct.unpack_from('<I', data, 0)[0]:08X}")
