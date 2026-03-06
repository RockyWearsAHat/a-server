#!/usr/bin/env python3
"""Read the decompressor function from the disc and disassemble it properly,
using the KSEG0 addresses computed from J instruction targets."""

import struct

DISC = "/Users/alexwaldmann/Desktop/AIO Server/test_roms/Crash Bandicoot (USA).bin"

# From find_decompressor.py, the function was found at disc address 0x1ADD4D8C
# The J target at offset +0x30 (0x1ADD4DBC) targets 0x10013A98
# So the mapping: disc 0x1ADD4DBC → KSEG0 target for J = 0x80013A98
# But the J instruction itself is at some KSEG0 address. From the function:
# 0x1ADD4D8C is the function entry point.
# We know from the emulator trace that the function is at 0x80013A50.
# Disc offset for 0x80013A50 = 0x1ADD4D8C
# So: KSEG0 = disc_addr - 0x1ADD4D8C + 0x80013A50 = disc_addr - 0x1ADC133C

DISC_BASE = 0x1ADD4D8C
KSEG0_BASE = 0x80013A50

# Read ~256 bytes of the function
FUNC_SIZE = 256  # Should cover the entire function

# MIPS instruction decoder
REGISTER_NAMES = {
    0: '$zero', 1: '$at', 2: '$v0', 3: '$v1',
    4: '$a0', 5: '$a1', 6: '$a2', 7: '$a3',
    8: '$t0', 9: '$t1', 10: '$t2', 11: '$t3',
    12: '$t4', 13: '$t5', 14: '$t6', 15: '$t7',
    16: '$s0', 17: '$s1', 18: '$s2', 19: '$s3',
    20: '$s4', 21: '$s5', 22: '$s6', 23: '$s7',
    24: '$t8', 25: '$t9', 26: '$k0', 27: '$k1',
    28: '$gp', 29: '$sp', 30: '$fp', 31: '$ra'
}

def reg(n):
    return REGISTER_NAMES.get(n, f'$r{n}')

def sign_extend_16(val):
    if val & 0x8000:
        return val - 0x10000
    return val

def disasm(word, addr):
    op = (word >> 26) & 0x3F
    rs = (word >> 21) & 0x1F
    rt = (word >> 16) & 0x1F
    rd = (word >> 11) & 0x1F
    shamt = (word >> 6) & 0x1F
    funct = word & 0x3F
    imm = word & 0xFFFF
    imm_se = sign_extend_16(imm)
    target = word & 0x3FFFFFF
    
    if word == 0:
        return "NOP"
    
    if op == 0:  # SPECIAL
        if funct == 0:
            return f"SLL {reg(rd)}, {reg(rt)}, {shamt}"
        elif funct == 2:
            return f"SRL {reg(rd)}, {reg(rt)}, {shamt}"
        elif funct == 3:
            return f"SRA {reg(rd)}, {reg(rt)}, {shamt}"
        elif funct == 8:
            return f"JR {reg(rs)}"
        elif funct == 0x21:
            return f"ADDU {reg(rd)}, {reg(rs)}, {reg(rt)}"
        elif funct == 0x23:
            return f"SUBU {reg(rd)}, {reg(rs)}, {reg(rt)}"
        elif funct == 0x24:
            return f"AND {reg(rd)}, {reg(rs)}, {reg(rt)}"
        elif funct == 0x25:
            return f"OR {reg(rd)}, {reg(rs)}, {reg(rt)}"
        elif funct == 0x2B:
            return f"SLTU {reg(rd)}, {reg(rs)}, {reg(rt)}"
        else:
            return f"SPECIAL funct=0x{funct:02X} {reg(rd)},{reg(rs)},{reg(rt)}"
    elif op == 2:
        jtarget = (addr & 0xF0000000) | (target << 2)
        return f"J 0x{jtarget:08X}"
    elif op == 3:
        jtarget = (addr & 0xF0000000) | (target << 2)
        return f"JAL 0x{jtarget:08X}"
    elif op == 4:
        btarget = addr + 4 + (imm_se << 2)
        return f"BEQ {reg(rs)}, {reg(rt)}, 0x{btarget:08X}"
    elif op == 5:
        btarget = addr + 4 + (imm_se << 2)
        return f"BNE {reg(rs)}, {reg(rt)}, 0x{btarget:08X}"
    elif op == 8:
        return f"ADDI {reg(rt)}, {reg(rs)}, {imm_se}"
    elif op == 9:
        return f"ADDIU {reg(rt)}, {reg(rs)}, {imm_se}"
    elif op == 0xC:
        return f"ANDI {reg(rt)}, {reg(rs)}, 0x{imm:04X}"
    elif op == 0xD:
        return f"ORI {reg(rt)}, {reg(rs)}, 0x{imm:04X}"
    elif op == 0xF:
        return f"LUI {reg(rt)}, 0x{imm:04X}"
    elif op == 0x20:
        return f"LB {reg(rt)}, {imm_se}({reg(rs)})"
    elif op == 0x24:
        return f"LBU {reg(rt)}, {imm_se}({reg(rs)})"
    elif op == 0x23:
        return f"LW {reg(rt)}, {imm_se}({reg(rs)})"
    elif op == 0x28:
        return f"SB {reg(rt)}, {imm_se}({reg(rs)})"
    elif op == 0x2B:
        return f"SW {reg(rt)}, {imm_se}({reg(rs)})"
    elif op == 0x29:
        return f"SH {reg(rt)}, {imm_se}({reg(rs)})"
    elif op == 0x25:
        return f"LHU {reg(rt)}, {imm_se}({reg(rs)})"
    else:
        return f"??? op=0x{op:02X} raw=0x{word:08X}"

def main():
    # Read the function bytes from disc
    with open(DISC, "rb") as f:
        # The disc address includes the sector structure
        # Actually, disc_addr for the decompressor might be in the EXE data section
        # The EXE is loaded to RAM, so let me compute the sector position
        
        # The entry point 0x80013A50 is in the game's executable
        # EXE loads at 0x80010000 typically
        # Offset within EXE data = 0x80013A50 - 0x80010000 = 0x3A50
        # The EXE is at the beginning of the disc (after the system area)
        # System area = 16 sectors, then executable
        
        # Actually, let me just search for the function signature in memory
        # We know from find_decompressor.py it was at disc byte offset 0x1ADD4D8C
        # In a 2352-byte sector format:
        # sector_num = offset // 2352
        # offset_in_sector = offset % 2352
        
        # But disc_addr 0x1ADD4D8C is the raw file offset for 2352-byte sectors
        f.seek(DISC_BASE)
        raw_data = f.read(FUNC_SIZE)
    
    print(f"Decompressor function at KSEG0 0x{KSEG0_BASE:08X}")
    print(f"Read from disc offset 0x{DISC_BASE:08X}")
    print(f"{'='*70}")
    print()
    
    for i in range(0, len(raw_data), 4):
        if i + 4 > len(raw_data):
            break
        word = struct.unpack_from('<I', raw_data, i)[0]
        addr = KSEG0_BASE + i
        instr_str = disasm(word, addr)
        print(f"  0x{addr:08X}: {word:08X}  {instr_str}")
    
    # Also identify key points
    print()
    print("Key addresses from SB trace:")
    print(f"  Literal SB:  0x80013AE8")
    print(f"  Backref SB:  0x80013AB4")

if __name__ == "__main__":
    main()
