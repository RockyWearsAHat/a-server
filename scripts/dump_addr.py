#!/usr/bin/env python3
"""Dump MIPS instructions at a given virtual address range from a PS1 disc image."""
import struct
import sys

ROM_PATH = "test_roms/Crash Bandicoot (USA).bin"
EXE_CODE_SECTOR = 112019  # First code sector (after header)
RAM_BASE = 0x10000  # Physical dest address

def read_from_ram(f, phys_addr, length):
    offset = phys_addr - RAM_BASE
    result = bytearray()
    sector_idx = offset // 2048
    byte_in_sector = offset % 2048
    while len(result) < length:
        disc_sector = EXE_CODE_SECTOR + sector_idx
        f.seek(disc_sector * 2352 + 24)
        sector_data = f.read(2048)
        chunk = sector_data[byte_in_sector:]
        result += chunk[:length - len(result)]
        sector_idx += 1
        byte_in_sector = 0
    return bytes(result[:length])

def disasm(instr, addr):
    op = instr >> 26
    rs = (instr >> 21) & 0x1F
    rt = (instr >> 16) & 0x1F
    rd = (instr >> 11) & 0x1F
    sa = (instr >> 6) & 0x1F
    imm = instr & 0xFFFF
    simm = imm if imm < 0x8000 else imm - 0x10000
    target = instr & 0x3FFFFFF
    rn = lambda r: f"${r}"
    if instr == 0: return "nop"
    if op == 0:
        funct = instr & 0x3F
        if funct == 0x00 and sa != 0: return f"sll {rn(rd)}, {rn(rt)}, {sa}"
        if funct == 0x02: return f"srl {rn(rd)}, {rn(rt)}, {sa}"
        if funct == 0x03: return f"sra {rn(rd)}, {rn(rt)}, {sa}"
        if funct == 0x08: return f"jr {rn(rs)}"
        if funct == 0x09: return f"jalr {rn(rd)}, {rn(rs)}"
        if funct == 0x0C: return f"syscall"
        if funct == 0x20: return f"add {rn(rd)}, {rn(rs)}, {rn(rt)}"
        if funct == 0x21: return f"addu {rn(rd)}, {rn(rs)}, {rn(rt)}"
        if funct == 0x23: return f"subu {rn(rd)}, {rn(rs)}, {rn(rt)}"
        if funct == 0x24: return f"and {rn(rd)}, {rn(rs)}, {rn(rt)}"
        if funct == 0x25: return f"or {rn(rd)}, {rn(rs)}, {rn(rt)}"
        if funct == 0x26: return f"xor {rn(rd)}, {rn(rs)}, {rn(rt)}"
        if funct == 0x27: return f"nor {rn(rd)}, {rn(rs)}, {rn(rt)}"
        if funct == 0x2A: return f"slt {rn(rd)}, {rn(rs)}, {rn(rt)}"
        if funct == 0x2B: return f"sltu {rn(rd)}, {rn(rs)}, {rn(rt)}"
    if op == 1:
        if rt == 0: return f"bltz {rn(rs)}, 0x{addr + 4 + simm*4:08X}"
        if rt == 1: return f"bgez {rn(rs)}, 0x{addr + 4 + simm*4:08X}"
        if rt == 0x10: return f"bltzal {rn(rs)}, 0x{addr + 4 + simm*4:08X}"
        if rt == 0x11: return f"bgezal {rn(rs)}, 0x{addr + 4 + simm*4:08X}"
    if op == 2: return f"j 0x{(addr & 0xF0000000) | (target << 2):08X}"
    if op == 3: return f"jal 0x{(addr & 0xF0000000) | (target << 2):08X}"
    if op == 4: return f"beq {rn(rs)}, {rn(rt)}, 0x{addr + 4 + simm*4:08X}"
    if op == 5: return f"bne {rn(rs)}, {rn(rt)}, 0x{addr + 4 + simm*4:08X}"
    if op == 6: return f"blez {rn(rs)}, 0x{addr + 4 + simm*4:08X}"
    if op == 7: return f"bgtz {rn(rs)}, 0x{addr + 4 + simm*4:08X}"
    if op == 8: return f"addi {rn(rt)}, {rn(rs)}, {simm}"
    if op == 9: return f"addiu {rn(rt)}, {rn(rs)}, {simm}"
    if op == 0xA: return f"slti {rn(rt)}, {rn(rs)}, {simm}"
    if op == 0xB: return f"sltiu {rn(rt)}, {rn(rs)}, {simm}"
    if op == 0xC: return f"andi {rn(rt)}, {rn(rs)}, 0x{imm:04X}"
    if op == 0xD: return f"ori {rn(rt)}, {rn(rs)}, 0x{imm:04X}"
    if op == 0xF: return f"lui {rn(rt)}, 0x{imm:04X}"
    if op == 0x10:
        if rs == 0: return f"mfc0 {rn(rt)}, {rn(rd)}"
        if rs == 4: return f"mtc0 {rn(rt)}, {rn(rd)}"
        if (instr & 0x3F) == 0x10: return "rfe"
    if op == 0x20: return f"lb {rn(rt)}, {simm}({rn(rs)})"
    if op == 0x21: return f"lh {rn(rt)}, {simm}({rn(rs)})"
    if op == 0x23: return f"lw {rn(rt)}, {simm}({rn(rs)})"
    if op == 0x24: return f"lbu {rn(rt)}, {simm}({rn(rs)})"
    if op == 0x25: return f"lhu {rn(rt)}, {simm}({rn(rs)})"
    if op == 0x28: return f"sb {rn(rt)}, {simm}({rn(rs)})"
    if op == 0x29: return f"sh {rn(rt)}, {simm}({rn(rs)})"
    if op == 0x2B: return f"sw {rn(rt)}, {simm}({rn(rs)})"
    return f"??? (0x{instr:08X})"

if len(sys.argv) < 3:
    print(f"Usage: {sys.argv[0]} <start_vaddr> <end_vaddr>")
    sys.exit(1)

start = int(sys.argv[1], 16)
end = int(sys.argv[2], 16)
phys_start = start & 0x1FFFFFFF

with open(ROM_PATH, "rb") as f:
    data = read_from_ram(f, phys_start, end - start)
    for i in range(0, len(data), 4):
        instr = struct.unpack('<I', data[i:i+4])[0]
        virt = start + i
        print(f"  {virt:08X}: {instr:08X}  {disasm(instr, virt)}")
