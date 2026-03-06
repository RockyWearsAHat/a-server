#!/usr/bin/env python3
"""Disassemble the Crash Bandicoot decompressor function from the disc image."""
import struct

DISC = "/Users/alexwaldmann/Desktop/AIO Server/test_roms/Crash Bandicoot (USA).bin"
SECTOR_SIZE = 2352
DATA_OFFSET = 24  # skip sync + header + subheader

# EXE starts at LBA 23, text section at file offset 0x800 within EXE
# Destination address is 0x80010000, size is 0x46800
EXE_START_LBA = 23
EXE_TEXT_FILE_OFFSET = 0x800
EXE_DEST_ADDR = 0x80010000
EXE_SIZE = 0x46800

# Decompressor is around 0x80013A50 - 0x80013B30
FUNC_START = 0x80013A00
FUNC_END = 0x80013B40

def read_exe_data():
    """Read the EXE text section from the disc image."""
    data = bytearray()
    with open(DISC, 'rb') as f:
        sector = EXE_START_LBA
        # First read EXE header (2048 bytes = 1 sector)
        exe_header = bytearray()
        f.seek(sector * SECTOR_SIZE + DATA_OFFSET)
        exe_header = f.read(2048)
        sector += 1
        
        # Then read text section: 0x46800 bytes = ceil(0x46800/2048) = 142 sectors
        remaining = EXE_SIZE
        while remaining > 0:
            f.seek(sector * SECTOR_SIZE + DATA_OFFSET)
            chunk = f.read(min(2048, remaining))
            data.extend(chunk)
            remaining -= len(chunk)
            sector += 1
    return bytes(data)

def decode_mips(word, addr):
    """Decode a MIPS instruction word into readable assembly."""
    opcode = word >> 26
    rs = (word >> 21) & 0x1F
    rt = (word >> 16) & 0x1F
    rd = (word >> 11) & 0x1F
    shamt = (word >> 6) & 0x1F
    funct = word & 0x3F
    imm16 = word & 0xFFFF
    imm16s = imm16 if imm16 < 0x8000 else imm16 - 0x10000
    target = word & 0x3FFFFFF
    
    regnames = ['$zero','$at','$v0','$v1','$a0','$a1','$a2','$a3',
                '$t0','$t1','$t2','$t3','$t4','$t5','$t6','$t7',
                '$s0','$s1','$s2','$s3','$s4','$s5','$s6','$s7',
                '$t8','$t9','$k0','$k1','$gp','$sp','$fp','$ra']
    
    def R(i): return regnames[i]
    
    if opcode == 0:  # SPECIAL
        if funct == 0x00: return f"SLL    {R(rd)}, {R(rt)}, {shamt}"
        if funct == 0x02: return f"SRL    {R(rd)}, {R(rt)}, {shamt}"
        if funct == 0x03: return f"SRA    {R(rd)}, {R(rt)}, {shamt}"
        if funct == 0x04: return f"SLLV   {R(rd)}, {R(rt)}, {R(rs)}"
        if funct == 0x06: return f"SRLV   {R(rd)}, {R(rt)}, {R(rs)}"
        if funct == 0x07: return f"SRAV   {R(rd)}, {R(rt)}, {R(rs)}"
        if funct == 0x08: return f"JR     {R(rs)}"
        if funct == 0x09: return f"JALR   {R(rd)}, {R(rs)}"
        if funct == 0x0C: return f"SYSCALL"
        if funct == 0x0D: return f"BREAK"
        if funct == 0x10: return f"MFHI   {R(rd)}"
        if funct == 0x12: return f"MFLO   {R(rd)}"
        if funct == 0x18: return f"MULT   {R(rs)}, {R(rt)}"
        if funct == 0x19: return f"MULTU  {R(rs)}, {R(rt)}"
        if funct == 0x1A: return f"DIV    {R(rs)}, {R(rt)}"
        if funct == 0x1B: return f"DIVU   {R(rs)}, {R(rt)}"
        if funct == 0x20: return f"ADD    {R(rd)}, {R(rs)}, {R(rt)}"
        if funct == 0x21: return f"ADDU   {R(rd)}, {R(rs)}, {R(rt)}"
        if funct == 0x22: return f"SUB    {R(rd)}, {R(rs)}, {R(rt)}"
        if funct == 0x23: return f"SUBU   {R(rd)}, {R(rs)}, {R(rt)}"
        if funct == 0x24: return f"AND    {R(rd)}, {R(rs)}, {R(rt)}"
        if funct == 0x25: return f"OR     {R(rd)}, {R(rs)}, {R(rt)}"
        if funct == 0x26: return f"XOR    {R(rd)}, {R(rs)}, {R(rt)}"
        if funct == 0x27: return f"NOR    {R(rd)}, {R(rs)}, {R(rt)}"
        if funct == 0x2A: return f"SLT    {R(rd)}, {R(rs)}, {R(rt)}"
        if funct == 0x2B: return f"SLTU   {R(rd)}, {R(rs)}, {R(rt)}"
        return f"SPECIAL funct=0x{funct:02X}"
    
    if opcode == 0x01:
        if rt == 0: return f"BLTZ   {R(rs)}, 0x{addr + 4 + imm16s*4:08X}"
        if rt == 1: return f"BGEZ   {R(rs)}, 0x{addr + 4 + imm16s*4:08X}"
        if rt == 0x10: return f"BLTZAL {R(rs)}, 0x{addr + 4 + imm16s*4:08X}"
        if rt == 0x11: return f"BGEZAL {R(rs)}, 0x{addr + 4 + imm16s*4:08X}"
        return f"BCONDZ rt={rt}"
    
    if opcode == 0x02: return f"J      0x{(addr & 0xF0000000) | (target << 2):08X}"
    if opcode == 0x03: return f"JAL    0x{(addr & 0xF0000000) | (target << 2):08X}"
    if opcode == 0x04: return f"BEQ    {R(rs)}, {R(rt)}, 0x{addr + 4 + imm16s*4:08X}"
    if opcode == 0x05: return f"BNE    {R(rs)}, {R(rt)}, 0x{addr + 4 + imm16s*4:08X}"
    if opcode == 0x06: return f"BLEZ   {R(rs)}, 0x{addr + 4 + imm16s*4:08X}"
    if opcode == 0x07: return f"BGTZ   {R(rs)}, 0x{addr + 4 + imm16s*4:08X}"
    if opcode == 0x08: return f"ADDI   {R(rt)}, {R(rs)}, {imm16s}"
    if opcode == 0x09: return f"ADDIU  {R(rt)}, {R(rs)}, {imm16s}"
    if opcode == 0x0A: return f"SLTI   {R(rt)}, {R(rs)}, {imm16s}"
    if opcode == 0x0B: return f"SLTIU  {R(rt)}, {R(rs)}, {imm16s}"
    if opcode == 0x0C: return f"ANDI   {R(rt)}, {R(rs)}, 0x{imm16:04X}"
    if opcode == 0x0D: return f"ORI    {R(rt)}, {R(rs)}, 0x{imm16:04X}"
    if opcode == 0x0E: return f"XORI   {R(rt)}, {R(rs)}, 0x{imm16:04X}"
    if opcode == 0x0F: return f"LUI    {R(rt)}, 0x{imm16:04X}"
    
    if opcode == 0x20: return f"LB     {R(rt)}, {imm16s}({R(rs)})"
    if opcode == 0x21: return f"LH     {R(rt)}, {imm16s}({R(rs)})"
    if opcode == 0x22: return f"LWL    {R(rt)}, {imm16s}({R(rs)})"
    if opcode == 0x23: return f"LW     {R(rt)}, {imm16s}({R(rs)})"
    if opcode == 0x24: return f"LBU    {R(rt)}, {imm16s}({R(rs)})"
    if opcode == 0x25: return f"LHU    {R(rt)}, {imm16s}({R(rs)})"
    if opcode == 0x26: return f"LWR    {R(rt)}, {imm16s}({R(rs)})"
    if opcode == 0x28: return f"SB     {R(rt)}, {imm16s}({R(rs)})"
    if opcode == 0x29: return f"SH     {R(rt)}, {imm16s}({R(rs)})"
    if opcode == 0x2A: return f"SWL    {R(rt)}, {imm16s}({R(rs)})"
    if opcode == 0x2B: return f"SW     {R(rt)}, {imm16s}({R(rs)})"
    if opcode == 0x2E: return f"SWR    {R(rt)}, {imm16s}({R(rs)})"
    
    return f"??? op=0x{opcode:02X} raw=0x{word:08X}"

exe_data = read_exe_data()
print(f"EXE data size: 0x{len(exe_data):X} bytes")

# Disassemble the function
for addr in range(FUNC_START, FUNC_END, 4):
    offset = addr - EXE_DEST_ADDR
    if offset < 0 or offset + 4 > len(exe_data):
        continue
    word = struct.unpack_from('<I', exe_data, offset)[0]
    disasm = decode_mips(word, addr)
    print(f"  0x{addr:08X}: {word:08X}  {disasm}")
