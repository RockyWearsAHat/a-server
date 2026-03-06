#!/usr/bin/env python3
"""Compare compressed source pointer positions between emulator and Python decompressor."""
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

# The compressed data was DMA'd to 0x800BA114
# So compressed byte at file offset N maps to memory address 0x800BA114 + N
DMA_BASE = 0x800BA114

print(f"=== Compressed data layout ===")
print(f"DMA base: 0x{DMA_BASE:08X}")
print(f"Compressed data header at DMA_BASE:")
print(f"  Magic: 0x{struct.unpack_from('<H', compressed, 0)[0]:04X}")
print(f"  DecompLen: {struct.unpack_from('<I', compressed, 4)[0]}")
print(f"  Skip: {struct.unpack_from('<I', compressed, 8)[0]}")

# The emulator's $t6 values from the SB trace:
# At entry #35 (last correct, offset 0x537F): t6=0x800BEE87
# At entry #36 (first wrong, offset 0x5380): t6=0x800BEE89

# Convert t6 to compressed data offset
t6_at_35 = 0x800BEE87
t6_at_36 = 0x800BEE89

offset_35 = t6_at_35 - DMA_BASE
offset_36 = t6_at_36 - DMA_BASE

print(f"\n=== Emulator source pointer ($t6) analysis ===")
print(f"Entry #35 (last correct): t6=0x{t6_at_35:08X}, compressed offset={offset_35} (0x{offset_35:04X})")
print(f"Entry #36 (first wrong):  t6=0x{t6_at_36:08X}, compressed offset={offset_36} (0x{offset_36:04X})")
print(f"Gap between: {offset_36 - offset_35} bytes")

# In the Python trace, token #1033 is a literal of 73 bytes starting at compressed offset 19818
# The prefix byte for token #1033 is at compressed offset 19818, which is 0x49 (=73)
# Let me calculate: src_off=19818 means the prefix byte was at compressed[19818]
# The actual data starts at compressed[19819]
# dst_off=0x5378 is where the literal starts writing

# At dst_off 0x537F (entry #35), the emulator has written byte #7 of the literal run
# (0x537F - 0x5378 = 7, zero-based)
# So the next compressed byte to read would be at compressed[19819 + 8] = compressed[19827] = 0xE9
# The Python src pointer after writing dst 0x537F would be at compressed[19827]

py_src_at_5380 = 19827  # compressed offset when about to write to dst 0x5380
print(f"\nPython src at dst=0x5380: compressed offset {py_src_at_5380} (0x{py_src_at_5380:04X})")
print(f"Byte at that offset: 0x{compressed[py_src_at_5380]:02X}")

# Now what does the emulator read?
# Entry #36: t6=0x800BEE89 = DMA_BASE + 0x4D75
emu_src_at_5380 = offset_36
print(f"\nEmulator src at dst=0x5380: compressed offset {emu_src_at_5380} (0x{emu_src_at_5380:04X})")
print(f"Byte at that offset: 0x{compressed[emu_src_at_5380]:02X}")

print(f"\n=== SOURCE POINTER DRIFT ===")
drift = emu_src_at_5380 - py_src_at_5380
print(f"Emulator src - Python src = {drift} bytes")
print(f"Emulator is reading from {abs(drift)} bytes {'ahead' if drift > 0 else 'behind'} the correct position")

# Let me look at what the emulator is reading
print(f"\nEmulator reads (around 0x{emu_src_at_5380:04X}):")
for i in range(-4, 8):
    off = emu_src_at_5380 + i
    marker = " <<<" if i == 0 else ""
    if 0 <= off < len(compressed):
        print(f"  compressed[0x{off:04X}] = 0x{compressed[off]:02X}{marker}")

print(f"\nPython reads (around 0x{py_src_at_5380:04X}):")
for i in range(-4, 8):
    off = py_src_at_5380 + i
    marker = " <<<" if i == 0 else ""
    if 0 <= off < len(compressed):
        print(f"  compressed[0x{off:04X}] = 0x{compressed[off]:02X}{marker}")

# Now let's check: entry #35 is in a literal run. Let me check what token the emulator 
# was in at that point.
# Entry #35: PC=0x80013AE8 (literal copy), t6=0x800BEE87, t7=0x800B9493
# Python token #1033 literal starts at compressed[19818] (prefix) + [19819] data
# Entry #35 writes byte at dst 0x537F. In token #1033, that's compressed[19826] = 0x20
# After the literal path reads compressed[19826], src becomes 19827.
# But entry #35 t6=0x800BEE87 → offset = 0x4D73

py_src_during_entry35 = 19826 + 1  # After reading compressed[19826]
emu_src_during_entry35 = offset_35 
print(f"\n=== At entry #35 (last correct write) ===")
print(f"Python src after this write: {py_src_during_entry35} (0x{py_src_during_entry35:04X})")
print(f"Emulator t6 after this write: {emu_src_during_entry35} (0x{emu_src_during_entry35:04X})")
print(f"Drift at entry #35: {emu_src_during_entry35 - py_src_during_entry35}")

# But wait, t6 in the SB trace is captured BEFORE the literal copy loop increments it for the next byte.
# Actually, the SB instruction at 0x80013AE8 is:
#   0x80013AE8: A1E20000  SB $v0, 0($t7)
# And the loop is:
#   0x80013AE0C: 91C20000  LBU $v0, 0($t6)    ; read literal
#   0x80013AE10: 25CE0001  ADDIU $t6, $t6, 1   ; src++
#   0x80013AE14: 2484FFFF  ADDIU $a0, $a0, -1  ; count--
#   0x80013AE18: A1E20000  SB $v0, 0($t7)      ; write literal << TRACE POINT
#   0x80013AE1C: 1483FFFB  BNE $a0, $v1, loop
#   0x80013AE20: 25EF0001  ADDIU $t7, $t7, 1   ; dst++ (delay slot)
# Wait, the actual addresses from our disassembly are different. Let me use the correct addresses.
# From the disassembly: 
#   0x1ADD4E0C: 91C20000  LBU $v0, 0($t6)
#   0x1ADD4E10: 25CE0001  ADDIU $t6, $t6, 1
#   0x1ADD4E14: 2484FFFF  ADDIU $a0, $a0, -1
#   0x1ADD4E18: A1E20000  SB $v0, 0($t7)
# But the SB trace shows PC=0x80013AE8. That's the KSEG0 address.
# 0x80013AE8 - 0x80010000 = 0x3AE8 offset in EXE

# The literal SB is at PC=0x80013AE8 and the backref SB is at PC=0x80013AB4
# Let me verify these against the disassembly (using KSEG0 base 0x80010000):
# From the disassembly dump, the decompressor's KSEG0 addresses (subtracting the disc mirror offset):
# If code at 0x1ADD4D8C is really at 0x80013D8C... no wait
# The J targets tell us: J 0x10013A98 = target address 0x00013A98
# So code at disc offset that contained J 0x10013A98 is the same code at KSEG0 0x80013A98
# Let me map the disc addresses to KSEG0:
# 0x1ADD4D84 -> ? Let me see the J targets more carefully.
# J 0x10013AF4 at disc addr 0x1ADD4DF0 means this instruction's canon addr is:
# target = (PC & 0xF0000000) | (instr_target << 2) 
# 0x10013AF4 means instr_target = 0x0004EBD << 2 = 0x13AF4
# With PC upper nibble 0x1 → 0x10013AF4
# With PC upper nibble 0x8 → 0x80013AF4
# So the instruction at 0x1ADD4DF0 maps to 0x80013B20... wait that doesn't add up.
# Let me figure it out: the disc mirror address minus some offset = KSEG0 address
# From J targets: disc 0x1ADD4DBC → J 0x10013A98
# If the code at 0x1ADD4DBC is supposed to be at 0x80013DBC... no.
# The offset from disc 0x1ADD4D8C to KSEG0 0x80013D8C? That would be 0x1ADD4D8C - 0x80013D8C = weird.

# I know the function is at KSEG0 0x80013A50-0x80013B28. 
# The disc dump showed ANDI 0x80 at 0x1ADD4D8C.
# Offset in KSEG0 function: 0x80013D8C - 0x80013A50 = 0x33C? That's way past the function end.
# There must be a constant offset: 0x1ADD4D8C - X = 0x80013A50 → X = 0x1ADC1D3C? Weird.
# Let me just calculate from J targets: 
# At 0x1ADD4DBC: J target = (0x1ADD4DC0 & 0xF0000000) | (0x0004EA6 << 2) = 0x10000000 | 0x00013A98 = 0x10013A98
# The target 0x10013A98 in KSEG0 would be 0x80013A98.
# The source instruction is at 0x1ADD4DBC.
# Difference: 0x1ADD4DBC - 0x10013A98 = 0x0ADC1324. Hmm.
# Actually, I just need to map my disc dump to KSEG0. The disc dump found the code at some 
# wrong offset because the EXE reading was buggy. Let me just use the KSEG0 addresses from
# the SB trace PC values: PC=0x80013AE8 (literal SB) and PC=0x80013AB4 (backref SB).

# Back to the main analysis: 
# In the literal loop, by the time SB executes, t6 has already been incremented past 
# the byte that was read. So t6 at the SB trace point = address of NEXT byte to read.

# Entry #35: t6=0x800BEE87 → next byte to read is at compressed offset 0x4D73
# After this literal finishes (if it's the last byte), the decompressor reads the next prefix.
# The next prefix byte would be at compressed offset 0x4D73.

# For the CORRECT decompressor (Python), after writing to dst 0x537F:
# We're in token #1033, a 73-byte literal. dst 0x537F is byte index 7 (0x537F - 0x5378).
# So 73 - 8 = 65 more bytes to copy. src pointer would be at compressed[19827].
# 0x800BA114 + 19827 = 0x800BA114 + 0x4D63 = 0x800BEE77

emu_t6_at_entry35 = 0x800BEE87
py_t6_at_entry35 = DMA_BASE + 19827  # = 0x800BEE77

print(f"\n=== SOURCE POINTER COMPARISON at entry #35 ===")
print(f"Emulator t6 (next byte to read): 0x{emu_t6_at_entry35:08X}")
print(f"Python equivalent pointer:       0x{py_t6_at_entry35:08X}")
print(f"Difference: {emu_t6_at_entry35 - py_t6_at_entry35} bytes")
print(f"*** Emulator's source pointer is {emu_t6_at_entry35 - py_t6_at_entry35} bytes AHEAD ***")
print(f"*** This means the emulator consumed {emu_t6_at_entry35 - py_t6_at_entry35} more compressed bytes ***")
print(f"*** even though the output is identical up to this point! ***")

# This is the key! The emulator has written all the correct bytes up to dst 0x537F,
# but its source pointer is 16 bytes ahead. This means at some earlier point,
# the decompressor consumed 16 extra bytes from the compressed stream while still
# producing the correct output. This could happen if:
# 1. A back-reference read 2 extra bytes (prefix + seek_byte) but produced 
#    the same output by reading from the right location
# Wait, that can't produce correct output with wrong source pointer.
# 
# OR: The token boundaries are different. The emulator is interpreting the compressed
# data with different token boundaries but producing the same output bytes.
# This would happen if the emulator's decompressor loop counter ($a2, remaining bytes)
# is counting differently, causing it to finish the LZ decompression 16 bytes too early
# and start the skip-byte copy 16 bytes too early...
# But that would affect where the output ends, not the source pointer.
#
# Actually, the most likely explanation: there were earlier back-references or literals
# that consumed different amounts of source data but produced identical output.

# Let me check: is $a2 (remaining bytes) also different?
# Entry #35: a2=0x0000A55D
# Entry #36: a2=0x0000A556
# Python at dst 0x537F: remaining = decompressed_len - 0x537F = 63774 - 0x537F 
py_remaining_at_537F = 63774 - 0x537F
print(f"\nRemaining bytes comparison at dst=0x537F:")
print(f"  Python remaining: {py_remaining_at_537F} (0x{py_remaining_at_537F:04X})")
print(f"  Emulator a2 at entry #35: 0x{0x0000A55D:04X} ({0x0000A55D})")
# Wait, 0xA55D is 42333, and 63774 - 0x5380 = 63774 - 21376 = 42398
# 42398 - 42333 = 65. Hmm interesting.
# Actually entry #35 writes to dst 0x537F. Its a2=0xA55D.
# But a2 is decremented at the START of the literal token, not per byte.
# Let me check: at entry #35, a0=0 means this is the LAST byte of the current literal run.
# The a2 was decremented by the full literal count before the loop started.
# So a2 = remaining_after_this_token.

# How many bytes remain after dst 0x537F? That's 63774 - 0x5380 = 42398 (0xA59E)
# But emulator a2 = 0xA55D = 42333
# Difference: 42398 - 42333 = 65
print(f"\nRemaining AFTER dst=0x537F written:")
print(f"  Python: {63774 - 0x5380} (0x{63774 - 0x5380:04X})")
print(f"  Emulator a2: {0xA55D} (0x{0xA55D:04X})")
print(f"  Difference: {(63774 - 0x5380) - 0xA55D} bytes")
print(f"  *** Emulator thinks {(63774 - 0x5380) - 0xA55D} FEWER bytes remain ***")
