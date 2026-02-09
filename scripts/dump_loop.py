#!/usr/bin/env python3
"""Dump MIPS instructions at the addresses where the game seems stuck."""
import struct

ROM_PATH = "test_roms/Crash Bandicoot (USA).bin"
EXE_CODE_SECTOR = 112019  # First code sector (after header)
RAM_BASE = 0x10000  # Physical dest address

def read_from_ram(f, phys_addr, length):
    """Read 'length' bytes from the EXE code as loaded in RAM at phys_addr."""
    offset = phys_addr - RAM_BASE
    result = bytearray()
    sector_idx = offset // 2048
    byte_in_sector = offset % 2048
    
    while len(result) < length:
        disc_sector = EXE_CODE_SECTOR + sector_idx
        f.seek(disc_sector * 2352 + 24)  # skip sync+header (24 bytes)
        sector_data = f.read(2048)
        chunk = sector_data[byte_in_sector:]
        result += chunk[:length - len(result)]
        sector_idx += 1
        byte_in_sector = 0
    
    return bytes(result[:length])

def disasm_basic(instr, addr):
    """Very basic MIPS disassembler for common instructions."""
    op = instr >> 26
    rs = (instr >> 21) & 0x1F
    rt = (instr >> 16) & 0x1F
    rd = (instr >> 11) & 0x1F
    imm = instr & 0xFFFF
    simm = imm if imm < 0x8000 else imm - 0x10000
    target = instr & 0x3FFFFFF
    
    rn = lambda r: f"${r}" if r < 10 else f"${r}"
    
    if instr == 0: return "nop"
    if op == 0:  # SPECIAL
        funct = instr & 0x3F
        if funct == 0x08: return f"jr {rn(rs)}"
        if funct == 0x09: return f"jalr {rn(rd)}, {rn(rs)}"
        if funct == 0x21: return f"addu {rn(rd)}, {rn(rs)}, {rn(rt)}"
        if funct == 0x25: return f"or {rn(rd)}, {rn(rs)}, {rn(rt)}"
        if funct == 0x2B: return f"sltu {rn(rd)}, {rn(rs)}, {rn(rt)}"
        if funct == 0x0C: return f"syscall"
    if op == 0x02: return f"j 0x{(addr & 0xF0000000) | (target << 2):08X}"
    if op == 0x03: return f"jal 0x{(addr & 0xF0000000) | (target << 2):08X}"
    if op == 0x04: return f"beq {rn(rs)}, {rn(rt)}, 0x{addr + 4 + simm*4:08X}"
    if op == 0x05: return f"bne {rn(rs)}, {rn(rt)}, 0x{addr + 4 + simm*4:08X}"
    if op == 0x06: return f"blez {rn(rs)}, 0x{addr + 4 + simm*4:08X}"
    if op == 0x07: return f"bgtz {rn(rs)}, 0x{addr + 4 + simm*4:08X}"
    if op == 0x08: return f"addi {rn(rt)}, {rn(rs)}, {simm}"
    if op == 0x09: return f"addiu {rn(rt)}, {rn(rs)}, {simm}"
    if op == 0x0A: return f"slti {rn(rt)}, {rn(rs)}, {simm}"
    if op == 0x0B: return f"sltiu {rn(rt)}, {rn(rs)}, {simm}"
    if op == 0x0C: return f"andi {rn(rt)}, {rn(rs)}, 0x{imm:04X}"
    if op == 0x0D: return f"ori {rn(rt)}, {rn(rs)}, 0x{imm:04X}"
    if op == 0x0F: return f"lui {rn(rt)}, 0x{imm:04X}"
    if op == 0x23: return f"lw {rn(rt)}, {simm}({rn(rs)})"
    if op == 0x2B: return f"sw {rn(rt)}, {simm}({rn(rs)})"
    if op == 0x24: return f"lbu {rn(rt)}, {simm}({rn(rs)})"
    if op == 0x25: return f"lhu {rn(rt)}, {simm}({rn(rs)})"
    if op == 0x28: return f"sb {rn(rt)}, {simm}({rn(rs)})"
    if op == 0x29: return f"sh {rn(rt)}, {simm}({rn(rs)})"
    if op == 0x20: return f"lb {rn(rt)}, {simm}({rn(rs)})"
    if op == 0x10:  # COP0
        if rs == 0: return f"mfc0 {rn(rt)}, {rn(rd)}"
        if rs == 4: return f"mtc0 {rn(rt)}, {rn(rd)}"
    return f"??? (0x{instr:08X})"

with open(ROM_PATH, "rb") as f:
    # Dump area 0x80044850-0x80044940
    print("=== 0x80044850 - 0x80044940 ===")
    phys = 0x44850
    data = read_from_ram(f, phys, 0xF0)
    for i in range(0, len(data), 4):
        instr = struct.unpack('<I', data[i:i+4])[0]
        virt = 0x80000000 + phys + i
        print(f"  {virt:08X}: {instr:08X}  {disasm_basic(instr, virt)}")
    
    # Dump area 0x8003E4F0-0x8003E640
    print("\n=== 0x8003E4F0 - 0x8003E640 ===")
    phys2 = 0x3E4F0
    data2 = read_from_ram(f, phys2, 0x150)
    for i in range(0, len(data2), 4):
        instr = struct.unpack('<I', data2[i:i+4])[0]
        virt = 0x80000000 + phys2 + i
        print(f"  {virt:08X}: {instr:08X}  {disasm_basic(instr, virt)}")
