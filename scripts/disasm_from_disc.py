#!/usr/bin/env python3
"""Add a memory dump of the decompressor function to the emulator's HLE BIOS.
Instead, let's just read the function from the disc properly by locating it
within the game executable sectors."""

import struct

DISC = "/Users/alexwaldmann/Desktop/AIO Server/test_roms/Crash Bandicoot (USA).bin"

# The game's EXE is loaded at some address. Let's find it by reading the system.cnf
# which tells us the EXE filename, and then find the EXE in the disc.
# But for now, let's just search for the decompressor signature in the disc
# data (user data portions of sectors only).

def read_all_user_data(disc_path, max_sectors=300000):
    """Read all user data from 2352-byte raw sectors."""
    data = bytearray()
    with open(disc_path, "rb") as f:
        sector = 0
        while sector < max_sectors:
            raw = f.read(2352)
            if len(raw) < 2352:
                break
            # User data starts at offset 24 (12 sync + 4 header + 8 subheader for Mode2/Form1)
            # For Mode1: 12 sync + 4 header = 16, user data = 2048
            # For Mode2/Form1: 12 sync + 4 header + 8 subheader = 24, user data = 2048
            # PS1 uses Mode2/Form1 for data
            user_data = raw[24:24+2048]
            data.extend(user_data)
            sector += 1
    return bytes(data)

# We know the function is at KSEG0 0x80013A50. The EXE loads at 0x80010000 (typically).
# The function offset within the EXE data would be 0x80013A50 - 0x80010000 = 0x3A50.
# But the EXE file has a 2048-byte header, so the data offset = 0x3A50 + 2048 = 0x4250.
# The EXE is typically at sector 4 or so.

# Actually, let me search for the signature bytes.
# The decompressor starts with setup code and the first distinctive instruction
# would be ANDI $v0, $a0, 0x0080 which is 0x30820080.
# Let's search for this pattern near LBU + ANDI 0x80 + BEQ.

# From the original find_decompressor.py, it found the function with score 16.
# Let me search for the specific instruction sequence around the main loop.

# Actually, let me just add instrumentation to the emulator to dump the function.
# Or better yet, let me dump it from the existing /tmp/nsf_chunk_emulator.bin session.

# Wait — let me just instrument the emulator to print memory at 0x80013A50-0x80013B50.
# But that requires a rebuild. Let me instead check if I can read from the binary directly.

# The game executable BOOT = cdrom:\SCUS_944.00;1
# This file starts at a specific sector on the disc. For Crash Bandicoot US:
# Typically at LBA 24 or similar. Let me find it from the disc structure.

# Actually, let me parse the volume descriptor to find the file.
def find_exe_on_disc(disc_path):
    """Find the main executable on a PS1 disc."""
    with open(disc_path, "rb") as f:
        # Read SYSTEM.CNF from the root directory
        # Primary Volume Descriptor is at sector 16
        f.seek(16 * 2352 + 24)  # Skip to user data
        pvd = f.read(2048)
        
        # Root directory record is at offset 156 in PVD
        root_dr = pvd[156:156+34]
        root_lba = struct.unpack_from('<I', root_dr, 2)[0]
        root_size = struct.unpack_from('<I', root_dr, 10)[0]
        
        print(f"Root directory: LBA={root_lba}, size={root_size}")
        
        # Read root directory
        f.seek(root_lba * 2352 + 24)
        root_data = f.read(min(root_size, 4096))
        
        # Parse directory entries
        offset = 0
        exe_lba = None
        exe_size = None
        while offset < len(root_data):
            rec_len = root_data[offset]
            if rec_len == 0:
                break
            
            name_len = root_data[offset + 32]
            name = root_data[offset + 33:offset + 33 + name_len]
            file_lba = struct.unpack_from('<I', root_data, offset + 2)[0]
            file_size = struct.unpack_from('<I', root_data, offset + 10)[0]
            
            try:
                name_str = name.decode('ascii').rstrip(';1\x01')
            except:
                name_str = repr(name)
            
            if name_len > 2:
                print(f"  File: {name_str} LBA={file_lba} size={file_size}")
            
            # Look for the executable (SCUS_944.00 or similar)
            if b'SCUS' in name or b'PSX.EXE' in name or b'MAIN' in name:
                exe_lba = file_lba
                exe_size = file_size
                print(f"  ** Found EXE: {name_str} at LBA {file_lba}, size {file_size}")
            
            offset += rec_len
        
        if exe_lba is None:
            # Try SYSTEM.CNF
            for off2 in range(0, len(root_data)):
                r = root_data[off2]
                if r == 0:
                    continue
                    
        return exe_lba, exe_size

def main():
    exe_lba, exe_size = find_exe_on_disc(DISC)
    
    if exe_lba is None:
        print("Could not find EXE on disc!")
        return
    
    # Read the EXE
    with open(DISC, "rb") as f:
        # Read EXE header (first 2048 bytes)
        f.seek(exe_lba * 2352 + 24)
        header = f.read(2048)
        
        magic = header[:8]
        print(f"\nEXE magic: {magic}")
        
        # PS-X EXE header format:
        # 0x00: "PS-X EXE" (8 bytes)
        # 0x10: initial PC (4 bytes)
        # 0x18: text start addr (4 bytes)
        # 0x1C: text size (4 bytes)
        
        text_start = struct.unpack_from('<I', header, 0x18)[0]
        text_size = struct.unpack_from('<I', header, 0x1C)[0]
        initial_pc = struct.unpack_from('<I', header, 0x10)[0]
        
        print(f"Initial PC: 0x{initial_pc:08X}")
        print(f"Text start: 0x{text_start:08X}")
        print(f"Text size: {text_size} (0x{text_size:X})")
        
        # The decompressor is at 0x80013A50
        func_offset = 0x80013A50 - text_start
        print(f"\nDecompressor offset in EXE data: 0x{func_offset:X}")
        
        # Read the EXE data (starts at sector exe_lba + 1, since header is 1 sector)
        # Actually, PS-X EXE header is always 2048 bytes (1 sector of user data)
        # The text data starts immediately after
        
        # Calculate which sector contains our function
        data_sector_offset = func_offset // 2048
        data_byte_offset = func_offset % 2048
        
        target_sector = exe_lba + 1 + data_sector_offset  # +1 for header sector
        
        print(f"Function at disc sector {target_sector}, offset {data_byte_offset}")
        
        # Read the function data
        f.seek(target_sector * 2352 + 24 + data_byte_offset)
        func_data = f.read(256)
        
        # But if the function crosses a sector boundary, we need to handle that
        # For safety, let me read sector by sector
        func_data = bytearray()
        bytes_to_read = 256
        current_offset = func_offset
        
        while bytes_to_read > 0:
            sector_num = current_offset // 2048
            byte_in_sector = current_offset % 2048
            
            f.seek((exe_lba + 1 + sector_num) * 2352 + 24 + byte_in_sector)
            chunk_size = min(bytes_to_read, 2048 - byte_in_sector)
            func_data.extend(f.read(chunk_size))
            
            bytes_to_read -= chunk_size
            current_offset += chunk_size
        
        print(f"\nDecompressor disassembly (0x80013A50 - 0x80013B4F):")
        print(f"{'='*70}")
        
        for i in range(0, len(func_data), 4):
            if i + 4 > len(func_data):
                break
            word = struct.unpack_from('<I', func_data, i)[0]
            addr = 0x80013A50 + i
            
            # Disassemble
            instr_str = disasm_mips(word, addr)
            
            # Mark key addresses
            marker = ""
            if addr == 0x80013AE8:
                marker = "  ← LITERAL SB"
            elif addr == 0x80013AB4:
                marker = "  ← BACKREF SB"
            elif addr == 0x80013A84:
                marker = "  ← MAIN LOOP: LBU prefix"
            
            print(f"  0x{addr:08X}: {word:08X}  {instr_str}{marker}")


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

def disasm_mips(word, addr):
    if word == 0:
        return "NOP"
    
    op = (word >> 26) & 0x3F
    rs = (word >> 21) & 0x1F
    rt = (word >> 16) & 0x1F
    rd = (word >> 11) & 0x1F
    shamt = (word >> 6) & 0x1F
    funct = word & 0x3F
    imm = word & 0xFFFF
    imm_se = sign_extend_16(imm)
    target = word & 0x3FFFFFF
    
    if op == 0:  # SPECIAL
        if funct == 0: return f"SLL {reg(rd)}, {reg(rt)}, {shamt}"
        elif funct == 2: return f"SRL {reg(rd)}, {reg(rt)}, {shamt}"
        elif funct == 3: return f"SRA {reg(rd)}, {reg(rt)}, {shamt}"
        elif funct == 4: return f"SLLV {reg(rd)}, {reg(rt)}, {reg(rs)}"
        elif funct == 6: return f"SRLV {reg(rd)}, {reg(rt)}, {reg(rs)}"
        elif funct == 7: return f"SRAV {reg(rd)}, {reg(rt)}, {reg(rs)}"
        elif funct == 8: return f"JR {reg(rs)}"
        elif funct == 9: return f"JALR {reg(rd)}, {reg(rs)}"
        elif funct == 0x10: return "MFHI"
        elif funct == 0x12: return "MFLO"
        elif funct == 0x18: return f"MULT {reg(rs)}, {reg(rt)}"
        elif funct == 0x1A: return f"DIV {reg(rs)}, {reg(rt)}"
        elif funct == 0x1B: return f"DIVU {reg(rs)}, {reg(rt)}"
        elif funct == 0x20: return f"ADD {reg(rd)}, {reg(rs)}, {reg(rt)}"
        elif funct == 0x21: return f"ADDU {reg(rd)}, {reg(rs)}, {reg(rt)}"
        elif funct == 0x22: return f"SUB {reg(rd)}, {reg(rs)}, {reg(rt)}"
        elif funct == 0x23: return f"SUBU {reg(rd)}, {reg(rs)}, {reg(rt)}"
        elif funct == 0x24: return f"AND {reg(rd)}, {reg(rs)}, {reg(rt)}"
        elif funct == 0x25: return f"OR {reg(rd)}, {reg(rs)}, {reg(rt)}"
        elif funct == 0x26: return f"XOR {reg(rd)}, {reg(rs)}, {reg(rt)}"
        elif funct == 0x27: return f"NOR {reg(rd)}, {reg(rs)}, {reg(rt)}"
        elif funct == 0x2A: return f"SLT {reg(rd)}, {reg(rs)}, {reg(rt)}"
        elif funct == 0x2B: return f"SLTU {reg(rd)}, {reg(rs)}, {reg(rt)}"
        else: return f"SPECIAL funct=0x{funct:02X}"
    elif op == 1:
        if rt == 0: return f"BLTZ {reg(rs)}, 0x{addr + 4 + (imm_se << 2):08X}"
        elif rt == 1: return f"BGEZ {reg(rs)}, 0x{addr + 4 + (imm_se << 2):08X}"
        elif rt == 0x10: return f"BLTZAL {reg(rs)}, 0x{addr + 4 + (imm_se << 2):08X}"
        elif rt == 0x11: return f"BGEZAL {reg(rs)}, 0x{addr + 4 + (imm_se << 2):08X}"
        else: return f"REGIMM rt={rt}"
    elif op == 2:
        jtarget = (addr & 0xF0000000) | (target << 2)
        return f"J 0x{jtarget:08X}"
    elif op == 3:
        jtarget = (addr & 0xF0000000) | (target << 2)
        return f"JAL 0x{jtarget:08X}"
    elif op == 4:
        return f"BEQ {reg(rs)}, {reg(rt)}, 0x{addr + 4 + (imm_se << 2):08X}"
    elif op == 5:
        return f"BNE {reg(rs)}, {reg(rt)}, 0x{addr + 4 + (imm_se << 2):08X}"
    elif op == 6:
        return f"BLEZ {reg(rs)}, 0x{addr + 4 + (imm_se << 2):08X}"
    elif op == 7:
        return f"BGTZ {reg(rs)}, 0x{addr + 4 + (imm_se << 2):08X}"
    elif op == 8: return f"ADDI {reg(rt)}, {reg(rs)}, {imm_se}"
    elif op == 9: return f"ADDIU {reg(rt)}, {reg(rs)}, {imm_se}"
    elif op == 0xA: return f"SLTI {reg(rt)}, {reg(rs)}, {imm_se}"
    elif op == 0xB: return f"SLTIU {reg(rt)}, {reg(rs)}, {imm_se}"
    elif op == 0xC: return f"ANDI {reg(rt)}, {reg(rs)}, 0x{imm:04X}"
    elif op == 0xD: return f"ORI {reg(rt)}, {reg(rs)}, 0x{imm:04X}"
    elif op == 0xE: return f"XORI {reg(rt)}, {reg(rs)}, 0x{imm:04X}"
    elif op == 0xF: return f"LUI {reg(rt)}, 0x{imm:04X}"
    elif op == 0x20: return f"LB {reg(rt)}, {imm_se}({reg(rs)})"
    elif op == 0x21: return f"LH {reg(rt)}, {imm_se}({reg(rs)})"
    elif op == 0x23: return f"LW {reg(rt)}, {imm_se}({reg(rs)})"
    elif op == 0x24: return f"LBU {reg(rt)}, {imm_se}({reg(rs)})"
    elif op == 0x25: return f"LHU {reg(rt)}, {imm_se}({reg(rs)})"
    elif op == 0x28: return f"SB {reg(rt)}, {imm_se}({reg(rs)})"
    elif op == 0x29: return f"SH {reg(rt)}, {imm_se}({reg(rs)})"
    elif op == 0x2B: return f"SW {reg(rt)}, {imm_se}({reg(rs)})"
    else: return f"??? op=0x{op:02X} raw=0x{word:08X}"

if __name__ == "__main__":
    main()
