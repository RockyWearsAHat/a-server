#!/usr/bin/env python3
"""Find the function containing 0x80013A40 and trace r14/r15 setup."""
import struct

ROM = "/Users/alexwaldmann/Desktop/AIO Server/test_roms/Crash Bandicoot (USA).bin"

with open(ROM, "rb") as f:
    data = f.read()

exe_offset = data.find(b"PS-X EXE")
sector_of_exe = exe_offset // 2352
dest = struct.unpack_from("<I", data, exe_offset + 0x18)[0]
dest_phys = dest & 0x1FFFFF
size = struct.unpack_from("<I", data, exe_offset + 0x1C)[0]

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
        names = {0:"SLL",2:"SRL",3:"SRA",8:"JR",9:"JALR",0xC:"SYSCALL",
                 0x20:"ADD",0x21:"ADDU",0x22:"SUB",0x23:"SUBU",
                 0x24:"AND",0x25:"OR",0x26:"XOR",0x27:"NOR",0x2A:"SLT",0x2B:"SLTU"}
        n = names.get(fn, f"fn{fn:#x}")
        if fn in (0,2,3):
            if rd==0 and rt==0 and sa==0: return "NOP"
            return f"{n} r{rd},r{rt},{sa}"
        if fn==8: return f"JR r{rs}"
        if fn==9: return f"JALR r{rd},r{rs}"
        return f"{n} r{rd},r{rs},r{rt}"
    elif op==2:
        tgt=((instr&0x3FFFFFF)<<2)|(addr&0xF0000000)
        return f"J 0x{tgt:08X}"
    elif op==3:
        tgt=((instr&0x3FFFFFF)<<2)|(addr&0xF0000000)
        return f"JAL 0x{tgt:08X}"
    elif op==4:
        return f"BEQ r{rs},r{rt},0x{addr+4+(si<<2):08X}"
    elif op==5:
        return f"BNE r{rs},r{rt},0x{addr+4+(si<<2):08X}"
    elif op==6:
        return f"BLEZ r{rs},0x{addr+4+(si<<2):08X}"
    elif op==7:
        return f"BGTZ r{rs},0x{addr+4+(si<<2):08X}"
    elif op==8:  return f"ADDI r{rt},r{rs},{si}"
    elif op==9:  return f"ADDIU r{rt},r{rs},{si}"
    elif op==0xA: return f"SLTI r{rt},r{rs},{si}"
    elif op==0xB: return f"SLTIU r{rt},r{rs},{si}"
    elif op==0xC: return f"ANDI r{rt},r{rs},0x{imm:04X}"
    elif op==0xD: return f"ORI r{rt},r{rs},0x{imm:04X}"
    elif op==0xE: return f"XORI r{rt},r{rs},0x{imm:04X}"
    elif op==0xF: return f"LUI r{rt},0x{imm:04X}"
    elif op==0x10:
        if rs==0: return f"MFC0 r{rt},r{rd}"
        if rs==4: return f"MTC0 r{rt},r{rd}"
        if rs==0x10: return "RFE"
        return f"COP0 rs={rs}"
    elif op==0x20: return f"LB r{rt},{si}(r{rs})"
    elif op==0x21: return f"LH r{rt},{si}(r{rs})"
    elif op==0x23: return f"LW r{rt},{si}(r{rs})"
    elif op==0x24: return f"LBU r{rt},{si}(r{rs})"
    elif op==0x25: return f"LHU r{rt},{si}(r{rs})"
    elif op==0x28: return f"SB r{rt},{si}(r{rs})"
    elif op==0x29: return f"SH r{rt},{si}(r{rs})"
    elif op==0x2B: return f"SW r{rt},{si}(r{rs})"
    elif op==1:
        bcond={0:"BLTZ",1:"BGEZ",0x10:"BLTZAL",0x11:"BGEZAL"}
        n=bcond.get(rt,f"BCOND_{rt}")
        return f"{n} r{rs},0x{addr+4+(si<<2):08X}"
    else:
        return f"OP={op:#x}"

# Search backwards from 0x80013A00 for function entry (look for ADDIU r29,r29,- or JR r31)
# Also look for JAL instructions that call something near this code
print("=== Searching for JAL to anywhere in 0x80013900-0x80013B30 range ===")
for a in range(dest, dest + size, 4):
    try:
        w = read_exe_u32(a)
    except:
        continue
    op = (w >> 26) & 0x3F
    if op == 3:  # JAL
        tgt = ((w & 0x3FFFFFF) << 2) | (a & 0xF0000000)
        if 0x80013900 <= tgt <= 0x80013B30:
            print(f"  JAL 0x{tgt:08X} at 0x{a:08X}")
    elif op == 2:  # J
        tgt = ((w & 0x3FFFFFF) << 2) | (a & 0xF0000000)
        if 0x80013900 <= tgt <= 0x80013B30:
            print(f"  J 0x{tgt:08X} at 0x{a:08X}")

# Disassemble 0x80013900-0x80013B30 to see the full function
print("\n=== Full function 0x80013900-0x80013B30 ===")
for a in range(0x80013900, 0x80013B30, 4):
    w = read_exe_u32(a)
    d = disasm(a, w)
    mark = ""
    if a == 0x80013A40: mark = " <-- decompressor start"
    if a == 0x80013AF4: mark = " <-- decompressor loop"
    print(f"  0x{a:08X}: 0x{w:08X}  {d}{mark}")
