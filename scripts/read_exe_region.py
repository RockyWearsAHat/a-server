#!/usr/bin/env python3
import struct
import sys

rom_path = sys.argv[1] if len(sys.argv) > 1 else "test_roms/Crash Bandicoot (USA).bin"

with open(rom_path, "rb") as f:
    f.seek(0)
    magic = f.read(8)
    print("Magic:", magic)
    if magic != b"PS-X EXE":
        print("Not a PS-X EXE")
        sys.exit(1)

    f.seek(0x10)
    pc = struct.unpack("<I", f.read(4))[0]
    f.seek(0x18)
    dest = struct.unpack("<I", f.read(4))[0]
    f.seek(0x1C)
    size = struct.unpack("<I", f.read(4))[0]
    print(f"PC=0x{pc:08X} Dest=0x{dest:08X} Size=0x{size:08X}")

    text_start = 0x800
    dest_start = dest & 0x1FFFFF

    # Read instructions from 0x80013A54 to 0x80013B00
    for addr in range(0x80013A40, 0x80013B10, 4):
        phys = addr & 0x1FFFFF
        offset = text_start + (phys - dest_start)
        f.seek(offset)
        instr = struct.unpack("<I", f.read(4))[0]
        opcode = (instr >> 26) & 0x3F
        rs = (instr >> 21) & 0x1F
        rt = (instr >> 16) & 0x1F
        rd = (instr >> 11) & 0x1F
        imm = instr & 0xFFFF
        simm = imm if imm < 0x8000 else imm - 0x10000

        desc = ""
        if opcode == 0x00:
            funct = instr & 0x3F
            if funct == 0x21:
                desc = f"ADDU r{rd}, r{rs}, r{rt}"
            elif funct == 0x23:
                desc = f"SUBU r{rd}, r{rs}, r{rt}"
            elif funct == 0x25:
                desc = f"OR r{rd}, r{rs}, r{rt}"
            elif funct == 0x00:
                sa = (instr >> 6) & 0x1F
                desc = f"SLL r{rd}, r{rt}, {sa}" if rd or rt or sa else "NOP"
            elif funct == 0x02:
                sa = (instr >> 6) & 0x1F
                desc = f"SRL r{rd}, r{rt}, {sa}"
            elif funct == 0x2A:
                desc = f"SLT r{rd}, r{rs}, r{rt}"
            elif funct == 0x2B:
                desc = f"SLTU r{rd}, r{rs}, r{rt}"
            elif funct == 0x08:
                desc = f"JR r{rs}"
            else:
                desc = f"SPECIAL funct={funct:#04x}"
        elif opcode == 0x05:
            target = addr + 4 + (simm << 2)
            desc = f"BNE r{rs}, r{rt}, 0x{target:08X}"
        elif opcode == 0x04:
            target = addr + 4 + (simm << 2)
            desc = f"BEQ r{rs}, r{rt}, 0x{target:08X}"
        elif opcode == 0x09:
            desc = f"ADDIU r{rt}, r{rs}, {simm}"
        elif opcode == 0x0D:
            desc = f"ORI r{rt}, r{rs}, 0x{imm:04X}"
        elif opcode == 0x0F:
            desc = f"LUI r{rt}, 0x{imm:04X}"
        elif opcode == 0x23:
            desc = f"LW r{rt}, {simm}(r{rs})"
        elif opcode == 0x20:
            desc = f"LB r{rt}, {simm}(r{rs})"
        elif opcode == 0x24:
            desc = f"LBU r{rt}, {simm}(r{rs})"
        elif opcode == 0x28:
            desc = f"SB r{rt}, {simm}(r{rs})"
        elif opcode == 0x2B:
            desc = f"SW r{rt}, {simm}(r{rs})"
        elif opcode == 0x03:
            desc = f"JAL 0x{(instr & 0x3FFFFFF) << 2 | (addr & 0xF0000000):08X}"
        elif opcode == 0x02:
            desc = f"J 0x{(instr & 0x3FFFFFF) << 2 | (addr & 0xF0000000):08X}"
        elif opcode == 0x11:
            desc = f"COP1 (invalid on PS1)"
        else:
            desc = f"opcode={opcode:#04x}"

        print(f"  0x{addr:08X}: 0x{instr:08X}  {desc}")
