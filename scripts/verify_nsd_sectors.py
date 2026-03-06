#!/usr/bin/env python3
"""Verify NSD sector data correctness by examining raw disc sectors."""
import struct

DISC_PATH = "test_roms/Crash Bandicoot (USA).bin"
RAW_SECTOR_SIZE = 2352

def main():
    mm, ss, ff = 11, 26, 42
    lba = (mm * 60 + ss) * 75 + ff - 150
    disc_offset = lba * RAW_SECTOR_SIZE
    
    print(f"NSD SetLoc: {mm:02d}:{ss:02d}:{ff:02d}  LBA={lba}  disc_offset=0x{disc_offset:X}")
    
    with open(DISC_PATH, "rb") as f:
        # Show raw sector header for first sector
        f.seek(disc_offset)
        raw = f.read(RAW_SECTOR_SIZE)
        
        print(f"\n--- Raw sector header (first 32 bytes) ---")
        for i in range(0, 32):
            print(f"  byte[{i:2d}] = 0x{raw[i]:02X}", end="")
            if i < 12:
                print("  (sync)" if raw[i] in (0x00, 0xFF) else "  (sync?)")
            elif i < 16:
                labels = ["minute", "second", "sector", "mode"]
                print(f"  ({labels[i-12]})")
            elif i < 24:
                print(f"  (sub-header)")
            else:
                print(f"  (user data)")
        
        # Check sector mode
        sector_mode = raw[15]
        print(f"\nSector mode: {sector_mode}")
        if sector_mode == 1:
            print("  Mode 1: 2048 bytes user data at offset 16")
            user_data_offset = 16
        elif sector_mode == 2:
            print("  Mode 2: user data at offset 24 (Form 1) or 16 (raw)")
            # Sub-header at bytes 16-23
            submode = raw[18]
            print(f"  Sub-mode byte: 0x{submode:02X}")
            print(f"    bit 5 (Form 2): {bool(submode & 0x20)}")
            user_data_offset = 24
        else:
            print(f"  Unknown mode!")
            user_data_offset = 24
        
        # Show first 64 bytes of user data at different offsets
        for skip in [12, 16, 24]:
            print(f"\n--- User data with headerSkip={skip} (first 32 words) ---")
            for i in range(0, 128, 4):
                word = struct.unpack_from("<I", raw, skip + i)[0]
                print(f"  +0x{i:03X}: 0x{word:08X}", end="")
                if i == 0 and skip == 24:
                    # First word that game DMA'd to 0xBA114
                    print(f"  (DMA'd to 0x800BA114)")
                else:
                    print()
        
        # Now check what the game expects at 0x800BA114
        # The log showed "DMA CDROM->RAM: ... first word=0x00001235"
        print(f"\n--- Verification: looking for 0x00001235 ---")
        for skip in [12, 16, 24]:
            word = struct.unpack_from("<I", raw, skip)[0]
            match = " ✓ MATCH" if word == 0x00001235 else ""
            print(f"  headerSkip={skip}: first word = 0x{word:08X}{match}")
        
        # Check second sector too
        print(f"\n--- Second sector (LBA {lba+1}) ---")
        f.seek((lba + 1) * RAW_SECTOR_SIZE)
        raw2 = f.read(RAW_SECTOR_SIZE)
        print(f"  Mode: {raw2[15]}")
        for skip in [24]:
            print(f"  headerSkip={skip} first 8 words:")
            for i in range(0, 32, 4):
                word = struct.unpack_from("<I", raw2, skip + i)[0]
                print(f"    +0x{i:03X}: 0x{word:08X}")
        
        # Multi-sector NSD: read concatenated user data with skip=24
        print(f"\n--- Full NSD (20 sectors, skip=24) important offsets ---")
        nsd_data = bytearray()
        for s in range(20):
            f.seek((lba + s) * RAW_SECTOR_SIZE + 24)
            nsd_data.extend(f.read(2048))
        
        # Show what's at the key offsets
        for off in [0x000, 0x004, 0x008, 0x00C, 0x010,
                    0x400, 0x404, 0x408, 0x40C, 0x410, 0x414, 0x418, 0x41C, 0x420,
                    0xF58, 0xF5C, 0xF60, 0xF64, 0xF68, 0xF6C, 0xF70, 0xF74, 0xF78, 0xF7C]:
            if off + 4 <= len(nsd_data):
                word = struct.unpack_from("<I", nsd_data, off)[0]
                print(f"  NSD+0x{off:04X}: 0x{word:08X}")
        
        # Also check with headerSkip=16 (Mode 2 without sub-header skip)
        print(f"\n--- Full NSD (20 sectors, skip=16) important offsets ---")
        nsd_data16 = bytearray()
        for s in range(20):
            f.seek((lba + s) * RAW_SECTOR_SIZE + 16)
            nsd_data16.extend(f.read(2048))
        
        # Check if NSD header makes more sense with skip=16
        for off in [0x000, 0x004, 0x400, 0x404, 0x408]:
            if off + 4 <= len(nsd_data16):
                word = struct.unpack_from("<I", nsd_data16, off)[0]
                print(f"  NSD+0x{off:04X}: 0x{word:08X}")

if __name__ == "__main__":
    main()
