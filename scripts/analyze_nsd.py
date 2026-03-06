#!/usr/bin/env python3
"""Analyze NSD file structure from Crash Bandicoot disc image."""
import struct
import sys

DISC_PATH = "test_roms/Crash Bandicoot (USA).bin"
RAW_SECTOR_SIZE = 2352
HEADER_SKIP = 24
DATA_SIZE = 2048

def read_sector_data(f, lba):
    """Read 2048 bytes of user data from a sector."""
    offset = lba * RAW_SECTOR_SIZE + HEADER_SKIP
    f.seek(offset)
    return f.read(DATA_SIZE)

def main():
    # NSD loaded from 11:26:42, 20 sectors to RAM 0xBA114-0xC3914
    mm, ss, ff = 11, 26, 42
    lba = (mm * 60 + ss) * 75 + ff - 150
    
    print(f"NSD start: {mm:02d}:{ss:02d}:{ff:02d}  LBA={lba}  disc_offset=0x{lba*RAW_SECTOR_SIZE:X}")
    print(f"20 sectors = {20*2048} bytes = 0x{20*2048:X}")
    
    with open(DISC_PATH, "rb") as f:
        # Read all 20 sectors of NSD data
        nsd_data = bytearray()
        for s in range(20):
            sector = read_sector_data(f, lba + s)
            nsd_data.extend(sector)
        
        print(f"\nTotal NSD data: {len(nsd_data)} bytes (0x{len(nsd_data):X})")
        
        # NSD structure (per cbhacks wiki for "Old NSD" format):
        # 0x000-0x3FF: Chunk-to-sector lookup (256 entries * 4 bytes)
        # 0x400: spawn_point_count (uint32)
        # 0x404: entry_hash_table_entry_count (uint32) 
        # 0x408-0x417: loading_screen_EIDS (4 * uint32)
        # 0x418-0x41F: compressed_chunk_count_per_type (2 * uint32)
        # 0x420-0x51F: spawn_point_table (variable)
        # 0x520+: entry_hash_table (s entries * 8 bytes)
        # After hash table: exec_eid_map (64 entries * 4 bytes)
        
        spawn_count = struct.unpack_from("<I", nsd_data, 0x400)[0]
        entry_count = struct.unpack_from("<I", nsd_data, 0x404)[0]
        
        print(f"\n--- NSD Header ---")
        print(f"Spawn point count: {spawn_count} (0x{spawn_count:X})")
        print(f"Entry hash table count: {entry_count} (0x{entry_count:X})")
        
        # Loading screen EIDs
        for i in range(4):
            eid = struct.unpack_from("<I", nsd_data, 0x408 + i*4)[0]
            print(f"Loading screen EID[{i}]: 0x{eid:08X}")
        
        # Compressed chunk counts
        for i in range(2):
            cc = struct.unpack_from("<I", nsd_data, 0x418 + i*4)[0]
            print(f"Compressed chunk count[{i}]: {cc} (0x{cc:X})")
        
        # Spawn point table at 0x420, each entry is 12 bytes (3 * uint32)
        spawn_table_offset = 0x420
        spawn_table_size = spawn_count * 12  # approx
        print(f"\nSpawn point table at NSD+0x{spawn_table_offset:X}, size ~0x{spawn_table_size:X}")
        
        # Entry hash table starts after spawn table
        hash_table_offset = spawn_table_offset + spawn_table_size
        hash_table_size = entry_count * 8
        print(f"Entry hash table at NSD+0x{hash_table_offset:X} ({entry_count} entries * 8 bytes = 0x{hash_table_size:X})")
        
        # Exec EID map (64 entries) follows hash table
        exec_map_offset = hash_table_offset + hash_table_size
        print(f"Exec EID map at NSD+0x{exec_map_offset:X}")
        
        print(f"\n--- First 20 entries of hash table ---")
        for i in range(min(20, entry_count)):
            off = hash_table_offset + i * 8
            chunk_id = struct.unpack_from("<I", nsd_data, off)[0]
            entry_id = struct.unpack_from("<I", nsd_data, off + 4)[0]
            print(f"  Hash[{i:3d}]: chunk=0x{chunk_id:08X} entry=0x{entry_id:08X}")
        
        # Now show what the game loads into memory
        # DMA target was 0xBA114, NSD data is at 0xBA114+0 in RAM
        # The struct at 0x800BB070 is at RAM offset 0xBB070 - 0xBA114 = 0xF5C from the DMA start
        # Wait, addresses are 0x800BA114 etc, so offset = 0x800BB070 - 0x800BA114 = 0xF5C
        ram_offset = 0xF5C
        print(f"\n--- Data at RAM offset 0x{ram_offset:X} (0x800BB070) ---")
        print(f"This is NSD file offset 0x{ram_offset:X}")
        for i in range(8):
            off = ram_offset + i * 4
            if off + 4 <= len(nsd_data):
                word = struct.unpack_from("<I", nsd_data, off)[0]
                print(f"  NSD+0x{off:04X}: 0x{word:08X}")
        
        # Look for 0x100 value in the NSD data
        print(f"\n--- Searching for 0x00000100 in NSD data ---")
        for i in range(0, len(nsd_data) - 3, 4):
            word = struct.unpack_from("<I", nsd_data, i)[0]
            if word == 0x100:
                print(f"  Found at NSD+0x{i:04X}")
        
        # Show NSD data around offset 0xF5C
        print(f"\n--- NSD data near 0xF5C (±64 bytes) ---")
        start = max(0, ram_offset - 64)
        for i in range(start, min(len(nsd_data) - 3, ram_offset + 96), 4):
            word = struct.unpack_from("<I", nsd_data, i)[0]
            marker = " <-- 0x800BB070" if i == ram_offset else ""
            print(f"  NSD+0x{i:04X}: 0x{word:08X}{marker}")

if __name__ == "__main__":
    main()
