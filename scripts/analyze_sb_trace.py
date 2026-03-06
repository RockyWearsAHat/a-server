#!/usr/bin/env python3
"""Compare SB trace from emulator with expected decompressed bytes."""
import re

# Read expected bytes from the correct Python decompression
with open('/tmp/nsf_chunk_python.bin', 'rb') as f:
    correct = f.read()

PAGE_ADDR = 0x800B4114

# Parse SB trace
with open('/tmp/ps1_sbtrace.log') as f:
    lines = f.readlines()

sb_entries = []
for line in lines:
    m = re.search(r'SB_TRACE: addr=0x([0-9A-Fa-f]+) val=0x([0-9A-Fa-f]+) PC=0x([0-9A-Fa-f]+) '
                  r'v0=0x([0-9A-Fa-f]+) v1=0x([0-9A-Fa-f]+) a0=0x([0-9A-Fa-f]+) '
                  r'a1=0x([0-9A-Fa-f]+) t6=0x([0-9A-Fa-f]+) t7=0x([0-9A-Fa-f]+) '
                  r'a2=0x([0-9A-Fa-f]+)', line)
    if m:
        addr = int(m.group(1), 16)
        val = int(m.group(2), 16)
        pc = int(m.group(3), 16)
        v0 = int(m.group(4), 16)
        v1 = int(m.group(5), 16)
        a0 = int(m.group(6), 16)
        a1 = int(m.group(7), 16)
        t6 = int(m.group(8), 16)
        t7 = int(m.group(9), 16)
        a2 = int(m.group(10), 16)
        sb_entries.append({
            'addr': addr, 'val': val, 'pc': pc,
            'v0': v0, 'v1': v1, 'a0': a0, 'a1': a1,
            't6': t6, 't7': t7, 'a2': a2
        })

print(f"Total SB trace entries: {len(sb_entries)}")
print()

# Compare each SB write with expected value
print(f"{'#':>3} {'Address':>10} {'Offset':>6} {'Actual':>6} {'Expect':>6} {'Match':>5} {'PC':>10} {'Type':>8}")
print("-" * 70)

first_mismatch = None
for i, entry in enumerate(sb_entries):
    offset = entry['addr'] - PAGE_ADDR
    expected = correct[offset] if 0 <= offset < len(correct) else None
    match = "✓" if expected == entry['val'] else "✗"
    
    # Determine if this is a literal copy (PC=0x80013AE8) or back-ref copy (PC=0x80013AB4)
    if entry['pc'] == 0x80013AE8:
        copy_type = "literal"
    elif entry['pc'] == 0x80013AB4:
        copy_type = "backref"
    else:
        copy_type = f"?{entry['pc']:08X}"
    
    if match == "✗" and first_mismatch is None:
        first_mismatch = i
    
    # Print context around mismatches
    show = (first_mismatch is not None and i >= first_mismatch - 5) or match == "✗"
    if show or i < 5 or (first_mismatch is not None and i <= first_mismatch + 20):
        marker = " <<<<" if match == "✗" and i == first_mismatch else ""
        print(f"{i:3d} 0x{entry['addr']:08X} 0x{offset:04X} 0x{entry['val']:02X}   0x{expected:02X}   {match}    0x{entry['pc']:08X} {copy_type}{marker}")

# Show the transition between last good literal and first bad write
if first_mismatch is not None:
    print(f"\n=== FIRST MISMATCH at entry #{first_mismatch} ===")
    entry = sb_entries[first_mismatch]
    offset = entry['addr'] - PAGE_ADDR
    expected = correct[offset]
    print(f"  Address: 0x{entry['addr']:08X} (offset 0x{offset:04X})")
    print(f"  Written: 0x{entry['val']:02X}, Expected: 0x{expected:02X}")
    print(f"  PC: 0x{entry['pc']:08X}")
    print(f"  v0=0x{entry['v0']:08X} v1=0x{entry['v1']:08X}")
    print(f"  a0=0x{entry['a0']:08X} a1=0x{entry['a1']:08X}")
    print(f"  t6=0x{entry['t6']:08X} (src ptr)")
    print(f"  t7=0x{entry['t7']:08X} (dst ptr)")
    print(f"  a2=0x{entry['a2']:08X} (remaining bytes)")
    
    # Show previous entry too
    if first_mismatch > 0:
        prev = sb_entries[first_mismatch - 1]
        prev_offset = prev['addr'] - PAGE_ADDR
        print(f"\n  Previous write (entry #{first_mismatch-1}):")
        print(f"    Address: 0x{prev['addr']:08X} (offset 0x{prev_offset:04X})")
        print(f"    Written: 0x{prev['val']:02X}, Expected: 0x{correct[prev_offset]:02X}")
        print(f"    PC: 0x{prev['pc']:08X}")
        print(f"    t6=0x{prev['t6']:08X} t7=0x{prev['t7']:08X}")

    # Check if the back-reference source is reading from correct location
    if entry['pc'] == 0x80013AB4:
        # v1 is the back-ref source pointer (already incremented by 1)
        ref_src = entry['v1'] - 1  # point to byte that was just read
        ref_offset = ref_src - PAGE_ADDR
        if 0 <= ref_offset < len(correct):
            print(f"\n  Back-ref reading from: 0x{ref_src:08X} (offset 0x{ref_offset:04X})")
            print(f"  Byte at that location in correct data: 0x{correct[ref_offset]:02X}")
            print(f"  Byte actually read by emulator (v0): 0x{entry['v0']:02X}")
            if correct[ref_offset] != entry['v0']:
                print(f"  *** MISMATCH: emulator read 0x{entry['v0']:02X} but correct data has 0x{correct[ref_offset]:02X}")
                print(f"  This means the source data (already written) is already corrupt!")
