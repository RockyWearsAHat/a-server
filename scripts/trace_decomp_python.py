#!/usr/bin/env python3
"""Trace the Python decompressor to see exactly what happens at the back-reference
that produces offset 0x5380, and what the emulator does differently."""
import struct

DISC = "/Users/alexwaldmann/Desktop/AIO Server/test_roms/Crash Bandicoot (USA).bin"
SECTOR_SIZE = 2352
DATA_OFFSET = 24

# Read the 20 sectors of the compressed chunk
START_LBA = 51342
with open(DISC, 'rb') as f:
    compressed = bytearray()
    for s in range(20):
        f.seek((START_LBA + s) * SECTOR_SIZE + DATA_OFFSET)
        compressed.extend(f.read(2048))

# Parse chunk header
magic = struct.unpack_from('<H', compressed, 0)[0]
decompressed_len = struct.unpack_from('<I', compressed, 4)[0]
skip_bytes = struct.unpack_from('<I', compressed, 8)[0]
print(f"Magic: 0x{magic:04X}, DecompLen: {decompressed_len}, Skip: {skip_bytes}")

# Decompress with detailed trace around offset 0x5380
src = 12  # skip header
dst = 0
output = bytearray(65536)
token_num = 0

TARGET_DST = 0x5370  # Start tracing a bit before the corruption point

while dst < decompressed_len:
    prefix = compressed[src]
    src += 1
    token_num += 1
    
    trace = (dst >= TARGET_DST - 0x20 and dst < TARGET_DST + 0x40)
    
    if prefix & 0x80:  # Back-reference
        seek_byte = compressed[src]
        src += 1
        
        # Combine: (prefix << 8) | seek_byte, then SRA >>3, mask 0xFFF
        combined = (prefix << 8) | seek_byte
        # SRA of a 16-bit value (could be up to 0xFFFF) shifted right 3
        # But on MIPS, prefix is only 8 bits, so combined is at most 0xFF?? = max 0xFFFF
        # SRA operates on 32-bit: combined is sign-extended if loaded as int, but ADDU is used
        # Actually, on MIPS: combined = (prefix << 8) + seek_byte using ADDU
        # prefix << 8 uses SLL which is 32-bit: prefix is already in a register as 0x000000XX
        # So SLL $v0, $a0, 8 → 0x0000XX00 (since prefix < 256)
        # ADDU $a0, $v0, $v1 → 0x0000XX00 + 0x000000YY = 0x0000XXYY
        # SRA $v0, $a0, 3 → 0x0000XXYY >> 3 arithmetic
        # Since bit 15 is 1 (prefix has bit 7 set), combined = 0x00008XXX to 0x0000FFYY
        # SRA treats it as a signed 32-bit value: 0x0000XXYY >> 3
        # Since bit 31 is 0, SRA == SRL for positive values!
        # Wait... ANDI $v0, $v0, 0x0FFF masks to 12 bits afterward anyway
        
        seek = ((prefix & 0x7F) << 5) | (seek_byte >> 3)
        span_bits = seek_byte & 7
        
        if span_bits == 7:
            span = 64
        else:
            span = span_bits + 3
        
        if trace:
            ref_addr = dst - seek
            print(f"\n  Token #{token_num}: BACKREF at src_off={src-2}, dst_off=0x{dst:04X}")
            print(f"    prefix=0x{prefix:02X}, seek_byte=0x{seek_byte:02X}")
            print(f"    combined=0x{combined:04X}, SRA>>3=0x{combined>>3:04X}, &0xFFF=0x{(combined>>3)&0xFFF:03X}")
            print(f"    seek={seek} (0x{seek:03X}), span={span}")
            print(f"    ref_addr=0x{ref_addr:04X} (dst 0x{dst:04X} - seek 0x{seek:03X})")
            print(f"    Copying {span} bytes from output[0x{ref_addr:04X}]:")
            for k in range(min(span, 20)):
                byte = output[ref_addr + k]
                print(f"      [{k}] output[0x{ref_addr+k:04X}] = 0x{byte:02X} → dst[0x{dst+k:04X}]")
        
        for k in range(span):
            output[dst] = output[dst - seek]
            dst += 1
    else:  # Literal
        count = prefix
        if trace:
            print(f"\n  Token #{token_num}: LITERAL at src_off={src-1}, dst_off=0x{dst:04X}, count={count}")
            for k in range(min(count, 20)):
                print(f"      [{k}] compressed[{src+k}] = 0x{compressed[src+k]:02X} → dst[0x{dst+k:04X}]")
        
        for k in range(count):
            output[dst] = compressed[src]
            src += 1
            dst += 1

print(f"\n\nFinal dst: 0x{dst:04X}")
print(f"Expected at offset 0x5380: 0x{output[0x5380]:02X}")

# Now let's check: emulator back-ref at entry #36 used v1=0x800B8774, which is
# offset 0x4660 from page buffer at 0x800B4114
# EMU seek = 0x5380 - 0x4660 = 0x0D20 — that's > 0xFFF which is the max 12-bit seek!
# But our Python seek is different. Let me check what seek should be:
print(f"\n=== Emulator's back-reference analysis ===")
emu_dst = 0x800B9494  # = PAGE_ADDR + 0x5380
emu_ref_src = 0x800B8774  # = PAGE_ADDR + 0x4660 (from v1 in trace, minus 1 since it's already incremented)
emu_seek = emu_dst - (emu_ref_src - 1)  # v1 already advanced by 1
print(f"Emulator dst: 0x{emu_dst:08X}")
print(f"Emulator ref src (v1-1): 0x{emu_ref_src-1:08X}")
# Actually v1 is 0x800B8775, so the read was from v1-1=0x800B8774
# Wait, in the copy loop: LBU $v0, 0($v1) then ADDIU $v1, $v1, 1
# So v1 in our trace is AFTER the increment
# The byte was read from v1 BEFORE increment... but our SB trace captures v1 AFTER the LBU's load delay is applied
# Let me recalculate: v1=0x800B8775 means the NEXT byte to read would be 0x800B8775
# The byte just read was from 0x800B8774
read_from = 0x800B8774
read_offset = read_from - 0x800B4114
emu_seek_actual = emu_dst - read_from
print(f"Read from: 0x{read_from:08X} (offset 0x{read_offset:04X})")
print(f"Seek distance: 0x{emu_seek_actual:04X} ({emu_seek_actual})")
print(f"Max allowed seek (12-bit): 0x0FFF ({0xFFF})")
if emu_seek_actual > 0xFFF:
    print(f"*** SEEK DISTANCE EXCEEDS 12-BIT LIMIT! 0x{emu_seek_actual:04X} > 0x0FFF ***")
    print(f"*** This means the seek calculation is WRONG in the emulator's CPU ***")
