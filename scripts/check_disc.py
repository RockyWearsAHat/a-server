#!/usr/bin/env python3
"""Check disc image format and extract chunk data."""
import struct
import sys

BIN_PATH = "test_roms/Crash Bandicoot (USA).bin"

with open(BIN_PATH, "rb") as f:
    # Check disc format
    f.seek(0)
    first = f.read(16)
    print(f"First 16 bytes: {first.hex()}")
    
    SYNC = bytes([0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00])
    is_raw = (first[:12] == SYNC)
    print(f"Has CD sync pattern at offset 0: {is_raw}")
    
    import os
    sz = os.path.getsize(BIN_PATH)
    print(f"File size: {sz} (0x{sz:X})")
    print(f"Size / 2352 = {sz / 2352:.2f}")
    print(f"Size / 2336 = {sz / 2336:.2f}")
    print(f"Size / 2048 = {sz / 2048:.2f}")
    
    # Read raw sector at LBA 51337
    lba = 51337
    for sector_size in [2352, 2336, 2048]:
        raw_off = lba * sector_size
        f.seek(raw_off)
        sector = f.read(48)
        print(f"\nSector size {sector_size}: LBA {lba} at offset 0x{raw_off:X}")
        print(f"  First 16 bytes: {sector[:16].hex()}")
        print(f"  Next 16 bytes:  {sector[16:32].hex()}")
        
    # If raw 2352, check the actual sector header for sector 0
    f.seek(0)
    s0 = f.read(2352)
    print(f"\nSector 0 sync: {s0[:12].hex()}")
    print(f"Sector 0 header (MSF+mode): {s0[12:16].hex()}")
    
    # Check sector 1
    f.seek(2352)
    s1 = f.read(16)
    print(f"Sector 1 sync: {s1[:12].hex()}")
    print(f"Sector 1 header: {s1[12:16].hex()}")
    
    # Find NSF magic in the disc
    # Search for 0x1235 (compressed NSF chunk) near our target LBA
    print("\n--- Searching for NSF chunks near LBA 51337 ---")
    for test_lba in range(51320, 51360):
        raw_off = test_lba * 2352 + 24  # data starts after sync+header+subheader
        f.seek(raw_off)
        d = f.read(4)
        if len(d) >= 2:
            magic = struct.unpack_from("<H", d, 0)[0]
            if magic in (0x1234, 0x1235):
                print(f"  Found NSF magic 0x{magic:04X} at LBA {test_lba} (offset 0x{raw_off:X})")
