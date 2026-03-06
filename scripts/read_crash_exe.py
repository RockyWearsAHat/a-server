#!/usr/bin/env python3
"""Read original instructions from the Crash Bandicoot PS-X EXE in the disc image."""
import struct

ROM = "/Users/alexwaldmann/Desktop/AIO Server/test_roms/Crash Bandicoot (USA).bin"

with open(ROM, "rb") as f:
    data = f.read()

exe_offset = data.find(b"PS-X EXE")
assert exe_offset >= 0, "PS-X EXE not found"
sector_of_exe = exe_offset // 2352
print(f"EXE at disc offset {exe_offset}, sector {sector_of_exe}")

dest = struct.unpack_from("<I", data, exe_offset + 0x18)[0]
size = struct.unpack_from("<I", data, exe_offset + 0x1C)[0]
dest_phys = dest & 0x1FFFFF
print(f"Dest=0x{dest:08X} Size=0x{size:08X}")


def read_exe_byte(file_off):
    """Read one byte from EXE file offset, handling raw sector format."""
    sector_idx = file_off // 2048
    offset_in_sector = file_off % 2048
    raw_off = (sector_of_exe + sector_idx) * 2352 + 24 + offset_in_sector
    return data[raw_off]


def read_exe_u32(addr):
    """Read a 32-bit word from virtual address in the EXE."""
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

    if op == 0:
        names = {0: "SLL", 2: "SRL", 3: "SRA", 4: "SLLV", 6: "SRLV", 7: "SRAV",
                 8: "JR", 9: "JALR", 0xC: "SYSCALL", 0xD: "BREAK",
                 0x10: "MFHI", 0x11: "MTHI", 0x12: "MFLO", 0x13: "MTLO",
                 0x18: "MULT", 0x19: "MULTU", 0x1A: "DIV", 0x1B: "DIVU",
                 0x20: "ADD", 0x21: "ADDU", 0x22: "SUB", 0x23: "SUBU",
                 0x24: "AND", 0x25: "OR", 0x26: "XOR", 0x27: "NOR",
                 0x2A: "SLT", 0x2B: "SLTU"}
        n = names.get(fn, f"SPECIAL_{fn:#04x}")
        if fn in (0, 2, 3):
            if rd == 0 and rt == 0 and sa == 0:
                return "NOP"
            return f"{n} r{rd},r{rt},{sa}"
        if fn in (8,):
            return f"{n} r{rs}"
        if fn in (9,):
            return f"{n} r{rd},r{rs}"
        return f"{n} r{rd},r{rs},r{rt}"
    elif op == 1:
        bcond = {0: "BLTZ", 1: "BGEZ", 0x10: "BLTZAL", 0x11: "BGEZAL"}
        n = bcond.get(rt, f"BCOND_{rt}")
        tgt = addr + 4 + (si << 2)
        return f"{n} r{rs},0x{tgt:08X}"
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
    elif op == 0xA:
        return f"SLTI r{rt},r{rs},{si}"
    elif op == 0xB:
        return f"SLTIU r{rt},r{rs},{si}"
    elif op == 0xC:
        return f"ANDI r{rt},r{rs},0x{imm:04X}"
    elif op == 0xD:
        return f"ORI r{rt},r{rs},0x{imm:04X}"
    elif op == 0xE:
        return f"XORI r{rt},r{rs},0x{imm:04X}"
    elif op == 0xF:
        return f"LUI r{rt},0x{imm:04X}"
    elif op == 0x10:
        return f"COP0 rs={rs} rt={rt} rd={rd}"
    elif op == 0x11:
        return "COP1 (no FPU!)"
    elif op == 0x12:
        return f"COP2/GTE rs={rs}"
    elif op == 0x20:
        return f"LB r{rt},{si}(r{rs})"
    elif op == 0x21:
        return f"LH r{rt},{si}(r{rs})"
    elif op == 0x22:
        return f"LWL r{rt},{si}(r{rs})"
    elif op == 0x23:
        return f"LW r{rt},{si}(r{rs})"
    elif op == 0x24:
        return f"LBU r{rt},{si}(r{rs})"
    elif op == 0x25:
        return f"LHU r{rt},{si}(r{rs})"
    elif op == 0x26:
        return f"LWR r{rt},{si}(r{rs})"
    elif op == 0x28:
        return f"SB r{rt},{si}(r{rs})"
    elif op == 0x29:
        return f"SH r{rt},{si}(r{rs})"
    elif op == 0x2A:
        return f"SWL r{rt},{si}(r{rs})"
    elif op == 0x2B:
        return f"SW r{rt},{si}(r{rs})"
    elif op == 0x2E:
        return f"SWR r{rt},{si}(r{rs})"
    else:
        return f"OP={op:#04x}"

print("\n--- Decompressor region (original EXE) ---")
for a in range(0x80013A40, 0x80013B10, 4):
    w = read_exe_u32(a)
    d = disasm(a, w)
    notes = []
    if a == 0x80013A58:
        notes.append("prev CpU")
    if a == 0x80013A9C:
        notes.append("RI here")
    if a == 0x80013AF4:
        notes.append("BNE loop back")
    note = f"  <-- {', '.join(notes)}" if notes else ""
    print(f"  0x{a:08X}: 0x{w:08X}  {d}{note}")
