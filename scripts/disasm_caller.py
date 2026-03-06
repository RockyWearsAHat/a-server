#!/usr/bin/env python3
"""Disassemble the code around the decompressor to find r14/r15 setup."""
import struct

ROM = "/Users/alexwaldmann/Desktop/AIO Server/test_roms/Crash Bandicoot (USA).bin"

with open(ROM, "rb") as f:
    data = f.read()

exe_offset = data.find(b"PS-X EXE")
sector_of_exe = exe_offset // 2352
dest = struct.unpack_from("<I", data, exe_offset + 0x18)[0]
size = struct.unpack_from("<I", data, exe_offset + 0x1C)[0]
pc_entry = struct.unpack_from("<I", data, exe_offset + 0x10)[0]
dest_phys = dest & 0x1FFFFF

print(f"EXE: Dest=0x{dest:08X} Size=0x{size:08X} PC=0x{pc_entry:08X}")
print(f"Code range: 0x{dest:08X} - 0x{dest+size:08X}")


def read_exe_byte(file_off):
    sector_idx = file_off // 2048
    offset_in_sector = file_off % 2048
    raw_off = (sector_of_exe + sector_idx) * 2352 + 24 + offset_in_sector
    return data[raw_off]


def read_exe_u32(addr):
    phys = addr & 0x1FFFFF
    file_off = 0x800 + (phys - dest_phys)
    b = bytes([read_exe_byte(file_off + i) for i in range(4)])
    return struct.unpack("<I", b)[0]


def disasm(addr, instr):
    op = (instr >> 26) & 0x3F
    rs = (instr >> 21) & 0x1F
    rt = (instr >> 16) & 0x1F
    rd = (instr >> 11) & 0x1F
    sa = (instr >> 6) & 0x1F
    fn = instr & 0x3F
    imm = instr & 0xFFFF
    si = imm if imm < 0x8000 else imm - 0x10000

    if instr == 0:
        return "NOP"
    if op == 0:
        names = {0: "SLL", 2: "SRL", 3: "SRA", 8: "JR", 9: "JALR",
                 0xC: "SYSCALL", 0x20: "ADD", 0x21: "ADDU", 0x23: "SUBU",
                 0x24: "AND", 0x25: "OR", 0x26: "XOR", 0x2A: "SLT", 0x2B: "SLTU"}
        n = names.get(fn, f"SPECIAL_{fn:#x}")
        if fn in (0, 2, 3):
            return f"{n} r{rd},r{rt},{sa}"
        if fn == 8:
            return f"JR r{rs}"
        if fn == 9:
            return f"JALR r{rd},r{rs}"
        return f"{n} r{rd},r{rs},r{rt}"
    elif op == 2:
        tgt = ((instr & 0x3FFFFFF) << 2) | (addr & 0xF0000000)
        return f"J 0x{tgt:08X}"
    elif op == 3:
        tgt = ((instr & 0x3FFFFFF) << 2) | (addr & 0xF0000000)
        return f"JAL 0x{tgt:08X}"
    elif op == 4:
        tgt = addr + 4 + (si << 2)
        return f"BEQ r{rs},r{rt},0x{tgt:08X}"
    elif op == 5:
        tgt = addr + 4 + (si << 2)
        return f"BNE r{rs},r{rt},0x{tgt:08X}"
    elif op == 6:
        tgt = addr + 4 + (si << 2)
        return f"BLEZ r{rs},0x{tgt:08X}"
    elif op == 7:
        tgt = addr + 4 + (si << 2)
        return f"BGTZ r{rs},0x{tgt:08X}"
    elif op == 8:
        return f"ADDI r{rt},r{rs},{si}"
    elif op == 9:
        return f"ADDIU r{rt},r{rs},{si}"
    elif op == 0xC:
        return f"ANDI r{rt},r{rs},0x{imm:04X}"
    elif op == 0xD:
        return f"ORI r{rt},r{rs},0x{imm:04X}"
    elif op == 0xF:
        return f"LUI r{rt},0x{imm:04X}"
    elif op == 0x10:
        if rs == 0:
            return f"MFC0 r{rt},r{rd}"
        elif rs == 4:
            return f"MTC0 r{rt},r{rd}"
        elif rs == 0x10:
            return "RFE"
        return f"COP0 rs={rs} rt={rt} rd={rd}"
    elif op == 0x20:
        return f"LB r{rt},{si}(r{rs})"
    elif op == 0x23:
        return f"LW r{rt},{si}(r{rs})"
    elif op == 0x24:
        return f"LBU r{rt},{si}(r{rs})"
    elif op == 0x28:
        return f"SB r{rt},{si}(r{rs})"
    elif op == 0x2B:
        return f"SW r{rt},{si}(r{rs})"
    elif op == 1:
        bcond = {0: "BLTZ", 1: "BGEZ", 0x10: "BLTZAL", 0x11: "BGEZAL"}
        n = bcond.get(rt, f"BCOND_{rt}")
        tgt = addr + 4 + (si << 2)
        return f"{n} r{rs},0x{tgt:08X}"
    else:
        return f"OP={op:#x} raw=0x{instr:08X}"

# Disassemble before the decompressor to find r14/r15 setup
print("\n=== Code before decompressor (0x80013A00-0x80013A44) ===")
for a in range(0x80013A00, 0x80013A44, 4):
    w = read_exe_u32(a)
    print(f"  0x{a:08X}: 0x{w:08X}  {disasm(a, w)}")

# Also look for JAL/J to 0x80013A40 to find callers
print("\n=== Searching for calls/jumps to 0x80013A40 ===")
target_j = (0x80013A40 >> 2) & 0x3FFFFFF
for a in range(dest, dest + size, 4):
    try:
        w = read_exe_u32(a)
    except:
        continue
    op = (w >> 26) & 0x3F
    if op == 3:  # JAL
        tgt = ((w & 0x3FFFFFF) << 2) | (a & 0xF0000000)
        if tgt == 0x80013A40:
            print(f"  JAL at 0x{a:08X}")
    elif op == 2:  # J
        tgt = ((w & 0x3FFFFFF) << 2) | (a & 0xF0000000)
        if tgt == 0x80013A40:
            print(f"  J at 0x{a:08X}")

# Disassemble around entry point
print(f"\n=== Entry point area (0x{pc_entry:08X}) ===")
for a in range(pc_entry, pc_entry + 0x80, 4):
    w = read_exe_u32(a)
    print(f"  0x{a:08X}: 0x{w:08X}  {disasm(a, w)}")
