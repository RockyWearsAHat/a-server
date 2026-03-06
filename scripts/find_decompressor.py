#!/usr/bin/env python3
"""Find the decompressor function in Crash Bandicoot's EXE by scanning for key instruction patterns."""
import struct

DISC = "/Users/alexwaldmann/Desktop/AIO Server/test_roms/Crash Bandicoot (USA).bin"
SECTOR_SIZE = 2352
DATA_OFFSET = 24

EXE_START_LBA = 23

def read_sectors(start_lba, count):
    data = bytearray()
    with open(DISC, 'rb') as f:
        for s in range(count):
            f.seek((start_lba + s) * SECTOR_SIZE + DATA_OFFSET)
            data.extend(f.read(2048))
    return bytes(data)

header = read_sectors(EXE_START_LBA, 1)
print(f"EXE magic: {header[:8]}")
dest_addr = struct.unpack_from('<I', header, 0x18)[0]
exe_size = struct.unpack_from('<I', header, 0x1C)[0]
print(f"Destination: 0x{dest_addr:08X}, Size: 0x{exe_size:X}")

num_sectors = (exe_size + 2047) // 2048
text = read_sectors(EXE_START_LBA + 1, num_sectors)
text = text[:exe_size]

regnames = ['$zero','$at','$v0','$v1','$a0','$a1','$a2','$a3',
            '$t0','$t1','$t2','$t3','$t4','$t5','$t6','$t7',
            '$s0','$s1','$s2','$s3','$s4','$s5','$s6','$s7',
            '$t8','$t9','$k0','$k1','$gp','$sp','$fp','$ra']

def decode(w, a):
    op = w >> 26
    rs = (w >> 21) & 0x1F; rt = (w >> 16) & 0x1F; rd = (w >> 11) & 0x1F
    sh = (w >> 6) & 0x1F; fn = w & 0x3F
    imm = w & 0xFFFF; imms = imm if imm < 0x8000 else imm - 0x10000
    R = lambda i: regnames[i]
    if op == 0:
        if fn == 0: return f"SLL    {R(rd)},{R(rt)},{sh}"
        if fn == 2: return f"SRL    {R(rd)},{R(rt)},{sh}"
        if fn == 3: return f"SRA    {R(rd)},{R(rt)},{sh}"
        if fn == 4: return f"SLLV   {R(rd)},{R(rt)},{R(rs)}"
        if fn == 6: return f"SRLV   {R(rd)},{R(rt)},{R(rs)}"
        if fn == 7: return f"SRAV   {R(rd)},{R(rt)},{R(rs)}"
        if fn == 8: return f"JR     {R(rs)}"
        if fn == 9: return f"JALR   {R(rd)},{R(rs)}"
        if fn == 0x0C: return f"SYSCALL"
        if fn == 0x0D: return f"BREAK"
        if fn == 0x20: return f"ADD    {R(rd)},{R(rs)},{R(rt)}"
        if fn == 0x21: return f"ADDU   {R(rd)},{R(rs)},{R(rt)}"
        if fn == 0x22: return f"SUB    {R(rd)},{R(rs)},{R(rt)}"
        if fn == 0x23: return f"SUBU   {R(rd)},{R(rs)},{R(rt)}"
        if fn == 0x24: return f"AND    {R(rd)},{R(rs)},{R(rt)}"
        if fn == 0x25: return f"OR     {R(rd)},{R(rs)},{R(rt)}"
        if fn == 0x26: return f"XOR    {R(rd)},{R(rs)},{R(rt)}"
        if fn == 0x27: return f"NOR    {R(rd)},{R(rs)},{R(rt)}"
        if fn == 0x2A: return f"SLT    {R(rd)},{R(rs)},{R(rt)}"
        if fn == 0x2B: return f"SLTU   {R(rd)},{R(rs)},{R(rt)}"
        if fn == 0x10: return f"MFHI   {R(rd)}"
        if fn == 0x12: return f"MFLO   {R(rd)}"
        return f"SPECIAL fn=0x{fn:02X}"
    if op == 1:
        names = {0:'BLTZ',1:'BGEZ',0x10:'BLTZAL',0x11:'BGEZAL'}
        n = names.get(rt, f'BCOND{rt}')
        return f"{n:<7}{R(rs)},0x{a+4+imms*4:08X}"
    if op == 2: return f"J      0x{(a & 0xF0000000)|((w&0x3FFFFFF)<<2):08X}"
    if op == 3: return f"JAL    0x{(a & 0xF0000000)|((w&0x3FFFFFF)<<2):08X}"
    if op == 4: return f"BEQ    {R(rs)},{R(rt)},0x{a+4+imms*4:08X}"
    if op == 5: return f"BNE    {R(rs)},{R(rt)},0x{a+4+imms*4:08X}"
    if op == 6: return f"BLEZ   {R(rs)},0x{a+4+imms*4:08X}"
    if op == 7: return f"BGTZ   {R(rs)},0x{a+4+imms*4:08X}"
    if op == 8: return f"ADDI   {R(rt)},{R(rs)},{imms}"
    if op == 9: return f"ADDIU  {R(rt)},{R(rs)},{imms}"
    if op == 0xA: return f"SLTI   {R(rt)},{R(rs)},{imms}"
    if op == 0xB: return f"SLTIU  {R(rt)},{R(rs)},{imms}"
    if op == 0xC: return f"ANDI   {R(rt)},{R(rs)},0x{imm:04X}"
    if op == 0xD: return f"ORI    {R(rt)},{R(rs)},0x{imm:04X}"
    if op == 0xE: return f"XORI   {R(rt)},{R(rs)},0x{imm:04X}"
    if op == 0xF: return f"LUI    {R(rt)},0x{imm:04X}"
    if op == 0x20: return f"LB     {R(rt)},{imms}({R(rs)})"
    if op == 0x21: return f"LH     {R(rt)},{imms}({R(rs)})"
    if op == 0x22: return f"LWL    {R(rt)},{imms}({R(rs)})"
    if op == 0x23: return f"LW     {R(rt)},{imms}({R(rs)})"
    if op == 0x24: return f"LBU    {R(rt)},{imms}({R(rs)})"
    if op == 0x25: return f"LHU    {R(rt)},{imms}({R(rs)})"
    if op == 0x26: return f"LWR    {R(rt)},{imms}({R(rs)})"
    if op == 0x28: return f"SB     {R(rt)},{imms}({R(rs)})"
    if op == 0x29: return f"SH     {R(rt)},{imms}({R(rs)})"
    if op == 0x2A: return f"SWL    {R(rt)},{imms}({R(rs)})"
    if op == 0x2B: return f"SW     {R(rt)},{imms}({R(rs)})"
    if op == 0x2E: return f"SWR    {R(rt)},{imms}({R(rs)})"
    return f"??? op=0x{op:02X} 0x{w:08X}"

# Search for ANDI rt, rs, 0x0080
print("\n=== ANDI reg, reg, 0x0080 candidates ===")
candidates = []
for i in range(0, len(text) - 4, 4):
    word = struct.unpack_from('<I', text, i)[0]
    if (word & 0xFC00FFFF) == 0x30000080:
        addr = dest_addr + i
        rs = (word >> 21) & 0x1F
        rt = (word >> 16) & 0x1F
        
        # Check vicinity for decompressor patterns
        score = 0
        details = []
        for j in range(-30, 30):
            idx = i + j * 4
            if idx < 0 or idx + 4 > len(text): continue
            w = struct.unpack_from('<I', text, idx)[0]
            op2 = w >> 26; fn2 = w & 0x3F; sh2 = (w >> 6) & 0x1F; imm2 = w & 0xFFFF
            
            if op2 == 0 and fn2 in (2,3) and sh2 == 3:  # SRL/SRA by 3
                score += 2; details.append(f"shift>>3 at +{j}")
            if op2 == 0 and fn2 == 0 and sh2 == 8:  # SLL by 8
                score += 2; details.append(f"SLL<<8 at +{j}")
            if (w & 0xFC00FFFF) == 0x30000007:  # ANDI 0x0007
                score += 2; details.append(f"ANDI 0x7 at +{j}")
            if (w & 0xFC00FFFF) == 0x30000FFF:  # ANDI 0x0FFF
                score += 2; details.append(f"ANDI 0xFFF at +{j}")
            if op2 == 0x24:  # LBU
                score += 1
            if op2 == 0x28:  # SB
                score += 1
        
        if score >= 6:
            candidates.append((addr, i, score, details))
            print(f"\n*** 0x{addr:08X}: ANDI ${regnames[rt]},${regnames[rs]},0x0080 (score={score})")
            print(f"    Patterns: {', '.join(details)}")

# Disassemble top candidate(s) with full context
if candidates:
    candidates.sort(key=lambda x: -x[2])
    best_addr, best_offset, best_score, _ = candidates[0]
    
    print(f"\n\n{'='*70}")
    print(f"BEST MATCH: 0x{best_addr:08X} (score={best_score})")
    print(f"{'='*70}")
    
    # Disassemble ±40 instructions
    for j in range(-40, 50):
        idx = best_offset + j * 4
        if idx < 0 or idx + 4 > len(text): continue
        w = struct.unpack_from('<I', text, idx)[0]
        a = dest_addr + idx
        marker = " <<<< ANDI 0x80" if j == 0 else ""
        print(f"  0x{a:08X}: {w:08X}  {decode(w, a)}{marker}")
