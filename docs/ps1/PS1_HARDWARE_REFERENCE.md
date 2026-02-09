# PlayStation 1 Hardware Reference — Implementation Guide

> **Purpose:** Authoritative reference for all agents implementing the PS1 emulator.
> Every hardware behavior, register, timing constraint, and edge case is documented
> here. When in doubt, consult this document before making implementation decisions.

---

## Table of Contents

1. [System Overview](#1-system-overview)
2. [CPU — MIPS R3000A](#2-cpu--mips-r3000a)
3. [System Control Coprocessor — COP0](#3-system-control-coprocessor--cop0)
4. [Geometry Transformation Engine — GTE (COP2)](#4-geometry-transformation-engine--gte-cop2)
5. [Memory Map & Bus](#5-memory-map--bus)
6. [DMA Controller](#6-dma-controller)
7. [GPU](#7-gpu)
8. [SPU — Sound Processing Unit](#8-spu--sound-processing-unit)
9. [CD-ROM Controller](#9-cd-rom-controller)
10. [Timers](#10-timers)
11. [Interrupt Controller](#11-interrupt-controller)
12. [Controllers & Memory Cards (SIO)](#12-controllers--memory-cards-sio)
13. [MDEC — Macroblock Decoder](#13-mdec--macroblock-decoder)
14. [Boot Sequence & BIOS](#14-boot-sequence--bios)
15. [Debug Infrastructure Design](#15-debug-infrastructure-design)
16. [Implementation Priorities](#16-implementation-priorities)

---

## 1. System Overview

| Component       | Details                                                             |
| --------------- | ------------------------------------------------------------------- |
| **CPU**         | MIPS R3000A @ 33.8688 MHz, 32-bit, little-endian                    |
| **RAM**         | 2 MB main RAM, 1 KB scratchpad (data cache used as fast RAM)        |
| **VRAM**        | 1 MB (1024×512 × 16-bit)                                            |
| **Sound RAM**   | 512 KB                                                              |
| **CD-ROM**      | 2× speed CD drive, XA audio, ADPCM                                  |
| **GPU**         | 2D rasterizer with 3D polygon rendering, no texture filtering in HW |
| **SPU**         | 24 ADPCM voices, reverb, CD audio mixing                            |
| **Controllers** | SIO serial protocol, digital and analog (DualShock)                 |

### Clock Rates

```
Master Clock:     33.868800 MHz (NTSC) / 33.868800 MHz (PAL, same crystal)
CPU Clock:        33.8688 MHz (1 cycle = ~29.5 ns)
GPU Dot Clock:    Varies by video mode (5/7/8/10 MHz)
SPU Clock:        ~44.1 kHz sample rate
CD-ROM:           ~44.1 kHz for XA-ADPCM, double-speed data reads
Timer Source:     System clock, dot clock, or HBlank
```

### NTSC vs PAL Timing

| Parameter           | NTSC     | PAL   |
| ------------------- | -------- | ----- |
| Scanlines/frame     | 263      | 314   |
| Visible scanlines   | 240      | 256   |
| Dots/scanline       | 3413     | 3406  |
| CPU cycles/scanline | ~2171    | ~2165 |
| Frame rate          | 59.94 Hz | 50 Hz |
| VBlank scanlines    | 23       | 58    |

---

## 2. CPU — MIPS R3000A

### 2.1 Registers

| Register | Alias     | Purpose                            |
| -------- | --------- | ---------------------------------- |
| R0       | `$zero`   | Hardwired to zero (writes ignored) |
| R1       | `$at`     | Assembler temporary                |
| R2-R3    | `$v0-$v1` | Return values                      |
| R4-R7    | `$a0-$a3` | Function arguments                 |
| R8-R15   | `$t0-$t7` | Temporaries (caller-saved)         |
| R16-R23  | `$s0-$s7` | Saved (callee-saved)               |
| R24-R25  | `$t8-$t9` | More temporaries                   |
| R26-R27  | `$k0-$k1` | Kernel temporaries                 |
| R28      | `$gp`     | Global pointer                     |
| R29      | `$sp`     | Stack pointer                      |
| R30      | `$fp`     | Frame pointer                      |
| R31      | `$ra`     | Return address                     |
| **PC**   | —         | Program counter                    |
| **HI**   | —         | Multiply/divide result high        |
| **LO**   | —         | Multiply/divide result low         |

### 2.2 Instruction Encoding

All instructions are 32-bit, aligned to 4-byte boundaries.

**R-Type:** `[opcode(6)][rs(5)][rt(5)][rd(5)][shamt(5)][funct(6)]`
**I-Type:** `[opcode(6)][rs(5)][rt(5)][imm16(16)]`
**J-Type:** `[opcode(6)][target(26)]`

### 2.3 Instruction Set

#### ALU Operations (R-Type, opcode=0x00)

| Funct | Mnemonic | Operation                             |
| ----- | -------- | ------------------------------------- |
| 0x00  | SLL      | rd = rt << shamt                      |
| 0x02  | SRL      | rd = rt >> shamt (logical)            |
| 0x03  | SRA      | rd = rt >> shamt (arithmetic)         |
| 0x04  | SLLV     | rd = rt << (rs & 0x1F)                |
| 0x06  | SRLV     | rd = rt >> (rs & 0x1F) (logical)      |
| 0x07  | SRAV     | rd = rt >> (rs & 0x1F) (arithmetic)   |
| 0x08  | JR       | PC = rs                               |
| 0x09  | JALR     | rd = PC+8; PC = rs                    |
| 0x0C  | SYSCALL  | Exception                             |
| 0x0D  | BREAK    | Exception                             |
| 0x10  | MFHI     | rd = HI                               |
| 0x11  | MTHI     | HI = rs                               |
| 0x12  | MFLO     | rd = LO                               |
| 0x13  | MTLO     | LO = rs                               |
| 0x18  | MULT     | HI:LO = rs × rt (signed)              |
| 0x19  | MULTU    | HI:LO = rs × rt (unsigned)            |
| 0x1A  | DIV      | LO = rs / rt; HI = rs % rt (signed)   |
| 0x1B  | DIVU     | LO = rs / rt; HI = rs % rt (unsigned) |
| 0x20  | ADD      | rd = rs + rt (overflow trap)          |
| 0x21  | ADDU     | rd = rs + rt (no trap)                |
| 0x22  | SUB      | rd = rs - rt (overflow trap)          |
| 0x23  | SUBU     | rd = rs - rt (no trap)                |
| 0x24  | AND      | rd = rs & rt                          |
| 0x25  | OR       | rd = rs \| rt                         |
| 0x26  | XOR      | rd = rs ^ rt                          |
| 0x27  | NOR      | rd = ~(rs \| rt)                      |
| 0x2A  | SLT      | rd = (signed)rs < (signed)rt ? 1 : 0  |
| 0x2B  | SLTU     | rd = rs < rt ? 1 : 0                  |

#### I-Type Instructions

| Opcode | Mnemonic | Operation                                  |
| ------ | -------- | ------------------------------------------ |
| 0x01   | BcondZ   | BLTZ/BGEZ/BLTZAL/BGEZAL (rt field selects) |
| 0x02   | J        | PC = (PC & 0xF0000000) \| (target << 2)    |
| 0x03   | JAL      | R31 = PC+8; J                              |
| 0x04   | BEQ      | if rs == rt: PC += imm16_se << 2           |
| 0x05   | BNE      | if rs != rt: PC += imm16_se << 2           |
| 0x06   | BLEZ     | if rs <= 0: PC += imm16_se << 2            |
| 0x07   | BGTZ     | if rs > 0: PC += imm16_se << 2             |
| 0x08   | ADDI     | rt = rs + imm16_se (overflow trap)         |
| 0x09   | ADDIU    | rt = rs + imm16_se (no trap)               |
| 0x0A   | SLTI     | rt = (signed)rs < imm16_se ? 1 : 0         |
| 0x0B   | SLTIU    | rt = rs < (unsigned)imm16_se ? 1 : 0       |
| 0x0C   | ANDI     | rt = rs & imm16_ze                         |
| 0x0D   | ORI      | rt = rs \| imm16_ze                        |
| 0x0E   | XORI     | rt = rs ^ imm16_ze                         |
| 0x0F   | LUI      | rt = imm16 << 16                           |
| 0x10   | COP0     | Coprocessor 0 instruction                  |
| 0x12   | COP2     | Coprocessor 2 (GTE) instruction            |
| 0x20   | LB       | rt = sign_extend(mem8[rs + imm16_se])      |
| 0x21   | LH       | rt = sign_extend(mem16[rs + imm16_se])     |
| 0x22   | LWL      | Load Word Left (unaligned)                 |
| 0x23   | LW       | rt = mem32[rs + imm16_se]                  |
| 0x24   | LBU      | rt = zero_extend(mem8[rs + imm16_se])      |
| 0x25   | LHU      | rt = zero_extend(mem16[rs + imm16_se])     |
| 0x26   | LWR      | Load Word Right (unaligned)                |
| 0x28   | SB       | mem8[rs + imm16_se] = rt[7:0]              |
| 0x29   | SH       | mem16[rs + imm16_se] = rt[15:0]            |
| 0x2A   | SWL      | Store Word Left (unaligned)                |
| 0x2B   | SW       | mem32[rs + imm16_se] = rt                  |
| 0x2E   | SWR      | Store Word Right (unaligned)               |
| 0x32   | LWC2     | Load Word to COP2 register                 |
| 0x3A   | SWC2     | Store Word from COP2 register              |

### 2.4 Branch Delay Slots

**CRITICAL:** Every branch/jump instruction has a **delay slot**. The instruction
immediately following a branch is ALWAYS executed, regardless of whether the
branch is taken.

```
BEQ $t0, $t1, target   ; branch instruction
ADDI $t2, $t2, 1       ; THIS ALWAYS EXECUTES (delay slot)
; if branch taken, PC goes to target after the delay slot
```

**Implementation rules:**

1. When a branch is detected, set `delaySlot = true` and `branchTarget = computed_target`
2. Execute the NEXT instruction normally (the delay slot)
3. After executing the delay slot, set PC = branchTarget
4. Branch-in-delay-slot: Undefined behavior on real hardware. We cancel the first branch.

### 2.5 Load Delay Slots

**CRITICAL:** Load instructions (LB, LBU, LH, LHU, LW, LWL, LWR) have a one-cycle
delay before the loaded value is available.

```
LW $t0, 0($t1)     ; begins loading
ADDI $t2, $t0, 1   ; uses OLD value of $t0 (before load completes!)
ADD $t3, $t0, $t2   ; NOW uses NEW value of $t0
```

**Implementation:** Track pending load register and value. If the instruction in
the delay slot reads the same register being loaded, it gets the OLD value.

### 2.6 Multiply/Divide

- **MULT/MULTU**: 2 cycles minimum, but HI/LO reads interlock (stall until ready).
  Real hardware: variable timing, but most emulators treat as instant for accuracy.
- **DIV/DIVU**: 36 cycles. MFHI/MFLO reads interlock.
- **Division by zero**: LO = nonsensical value, HI = dividend. No exception.
  - DIVU: LO = 0xFFFFFFFF, HI = dividend
  - DIV: LO = ±1 (depending on dividend sign), HI = dividend

### 2.7 Exceptions

| Exception               | Cause Code | Description                 |
| ----------------------- | ---------- | --------------------------- |
| Interrupt               | 0x00       | External hardware interrupt |
| Address Error (Load)    | 0x04       | Unaligned load address      |
| Address Error (Store)   | 0x05       | Unaligned store address     |
| Bus Error (Instruction) | 0x06       | Instruction fetch error     |
| Bus Error (Data)        | 0x07       | Data access error           |
| Syscall                 | 0x08       | SYSCALL instruction         |
| Breakpoint              | 0x09       | BREAK instruction           |
| Reserved Instruction    | 0x0A       | Invalid opcode              |
| Coprocessor Unusable    | 0x0B       | COP access when disabled    |
| Arithmetic Overflow     | 0x0C       | ADD/ADDI/SUB overflow       |

**Exception handling:**

1. Save PC in COP0 EPC register (or PC-4 if in delay slot)
2. Set COP0 Cause register with exception code
3. Push SR exception bits (KUo/IEo ← KUp/IEp ← KUc/IEc)
4. Set PC = 0x80000080 (general exception vector)
5. BFC00180 used if BEV bit set in SR

---

## 3. System Control Coprocessor — COP0

### 3.1 Registers

| Reg | Name      | Purpose                                |
| --- | --------- | -------------------------------------- |
| 3   | BPC       | Breakpoint on execute address          |
| 5   | BDA       | Breakpoint on data access address      |
| 6   | JUMPDEST  | Jump destination (read-only)           |
| 7   | DCIC      | Breakpoint control                     |
| 8   | BadVAddr  | Bad virtual address (on address error) |
| 9   | BDAM      | Data access breakpoint mask            |
| 11  | BPCM      | Execute breakpoint mask                |
| 12  | **SR**    | **Status Register** (most important!)  |
| 13  | **Cause** | Exception cause                        |
| 14  | **EPC**   | Exception return address               |
| 15  | PRId      | Processor ID (0x00000002 for PS1)      |

### 3.2 Status Register (SR) — COP0 R12

```
Bit 0:  IEc  - Interrupt Enable (current)
Bit 1:  KUc  - Kernel/User mode (current) (0=kernel, 1=user)
Bit 2:  IEp  - Interrupt Enable (previous)
Bit 3:  KUp  - Kernel/User mode (previous)
Bit 4:  IEo  - Interrupt Enable (old)
Bit 5:  KUo  - Kernel/User mode (old)
Bits 8-15: Im - Interrupt Mask (8 bits: 2 software + 6 hardware)
Bit 16: Isc - Isolate Cache (when set, loads/stores target cache, not memory)
Bit 17: Swc - Swap caches (swap I-cache and D-cache)
Bit 22: BEV - Boot Exception Vectors (0=normal 0x80000080, 1=boot 0xBFC00180)
Bit 25: CU0 - COP0 usable (user mode only)
Bit 26: CU1 - COP1 usable (always 0, PS1 has no FPU)
Bit 27: CU2 - COP2 (GTE) usable
Bit 28: CU3 - COP3 usable (always 0)
```

### 3.3 Cause Register — COP0 R13

```
Bits 2-6:  ExcCode - Exception code (see table above)
Bits 8-15: IP - Interrupt Pending (matches Im bits in SR)
Bit 28-29: CE - Coprocessor error number
Bit 31:    BD - Branch Delay (exception occurred in delay slot)
```

### 3.4 Cache Isolation (Isc bit)

When SR.Isc = 1, all loads/stores go to the scratchpad/cache instead of main
memory. The BIOS uses this during boot to initialize the instruction cache.
**This MUST be implemented** or the BIOS will not boot.

---

## 4. Geometry Transformation Engine — GTE (COP2)

The GTE performs fast matrix/vector math for 3D transformations. It is accessed
via COP2 instructions (MTC2, MFC2, CTC2, CFC2, and COP2 commands).

### 4.1 Data Registers (accessed via MTC2/MFC2)

| Reg   | Name      | Description                                 |
| ----- | --------- | ------------------------------------------- |
| 0     | VXY0      | Vector 0 X/Y (16-bit signed each)           |
| 1     | VZ0       | Vector 0 Z (16-bit signed, upper 16 unused) |
| 2-3   | VXY1/VZ1  | Vector 1                                    |
| 4-5   | VXY2/VZ2  | Vector 2                                    |
| 6     | RGBC      | Color + code (R,G,B,Code — 8 bits each)     |
| 7     | OTZ       | Average Z (16-bit)                          |
| 8     | IR0       | Intermediate 0 (16-bit signed)              |
| 9-11  | IR1-IR3   | Intermediate 1-3 (16-bit signed)            |
| 12-15 | SXY0-3    | Screen X/Y FIFO (16-bit signed each)        |
| 16-19 | SZ0-3     | Screen Z FIFO (16-bit)                      |
| 20-22 | RGB0-2    | Color FIFO (R,G,B,Code)                     |
| 23    | RES1      | Prohibited                                  |
| 24    | MAC0      | 32-bit accumulator                          |
| 25-27 | MAC1-3    | 32-bit accumulators (48-bit internally)     |
| 28-29 | IRGB/ORGB | Color conversion input/output               |
| 30-31 | LZCS/LZCR | Leading zero count input/result             |

### 4.2 Control Registers (accessed via CTC2/CFC2)

| Reg   | Name        | Description                                  |
| ----- | ----------- | -------------------------------------------- |
| 0-4   | RT11-RT33   | Rotation matrix (3×3 signed 16-bit)          |
| 5-7   | TRX/TRY/TRZ | Translation vector (32-bit signed)           |
| 8-12  | L11-L33     | Light source matrix (3×3)                    |
| 13-15 | RBK/GBK/BBK | Background color (32-bit signed)             |
| 16-20 | LR1-LB3     | Light color matrix (3×3)                     |
| 21-23 | RFC/GFC/BFC | Far color (32-bit signed)                    |
| 24    | OFX         | Screen offset X (32-bit signed, 16.16 fixed) |
| 25    | OFY         | Screen offset Y (32-bit signed, 16.16 fixed) |
| 26    | H           | Projection plane distance (16-bit unsigned)  |
| 27    | DQA         | Depth cue coefficient (16-bit signed)        |
| 28    | DQB         | Depth cue offset (32-bit signed)             |
| 29    | ZSF3        | Z scale factor 3 (16-bit signed)             |
| 30    | ZSF4        | Z scale factor 4 (16-bit signed)             |
| 31    | FLAG        | Overflow flags (calculated by GTE commands)  |

### 4.3 Key GTE Commands

| Cmd   | Cycles | Description                                |
| ----- | ------ | ------------------------------------------ |
| RTPS  | 15     | Perspective transform single vertex        |
| RTPT  | 23     | Perspective transform triple (3 vertices)  |
| MVMVA | 8      | Matrix × vector + translation              |
| DCPL  | 8      | Depth cue (light)                          |
| DPCS  | 8      | Depth cue single                           |
| DPCT  | 17     | Depth cue triple                           |
| INTPL | 8      | Interpolation                              |
| SQR   | 5      | Square of vector                           |
| NCS   | 14     | Normal color single                        |
| NCT   | 30     | Normal color triple                        |
| NCDS  | 19     | Normal color depth single                  |
| NCDT  | 44     | Normal color depth triple                  |
| NCCS  | 17     | Normal color color single                  |
| NCCT  | 39     | Normal color color triple                  |
| CDP   | 13     | Color depth cue                            |
| CC    | 11     | Color color                                |
| NCLIP | 8      | Normal clipping                            |
| AVSZ3 | 5      | Average Z (3 values)                       |
| AVSZ4 | 6      | Average Z (4 values)                       |
| OP    | 6      | Outer product                              |
| GPF   | 5      | General purpose interpolation              |
| GPL   | 5      | General purpose interpolation + accumulate |

**Implementation priority:** RTPS, RTPT, NCLIP, AVSZ3/4, MVMVA are used by
virtually all 3D games. Lighting commands (NCS, NCDS, etc.) are needed for
properly lit scenes.

---

## 5. Memory Map & Bus

### 5.1 Physical Memory Map

```
0x00000000 - 0x001FFFFF  Main RAM (2 MB, mirrored at 0x00200000-0x007FFFFF)
0x1F000000 - 0x1F7FFFFF  Expansion Region 1 (8 MB)
0x1F800000 - 0x1F8003FF  Scratchpad (1 KB data cache)
0x1F801000 - 0x1F802FFF  I/O Ports (hardware registers)
0x1F802000 - 0x1F802FFF  Expansion Region 2
0x1FA00000 - 0x1FBFFFFF  Expansion Region 3
0x1FC00000 - 0x1FC7FFFF  BIOS ROM (512 KB)
0xFFFE0000 - 0xFFFE01FF  Cache Control (I-cache tags)
```

### 5.2 KSEG Mapping (Virtual → Physical)

The R3000A uses simple fixed-address translation (no TLB/MMU):

| Segment | Virtual Range         | Physical          | Cached? |
| ------- | --------------------- | ----------------- | ------- |
| KUSEG   | 0x00000000-0x7FFFFFFF | addr & 0x1FFFFFFF | Yes     |
| KSEG0   | 0x80000000-0x9FFFFFFF | addr & 0x1FFFFFFF | Yes     |
| KSEG1   | 0xA0000000-0xBFFFFFFF | addr & 0x1FFFFFFF | No      |
| KSEG2   | 0xC0000000-0xFFFFFFFF | Cache control     | N/A     |

**CRITICAL:** All three regions 0x00xxxxxx, 0x80xxxxxx, 0xA0xxxxxx map to the
same physical address. The bus implementation must mask the top 3 bits:
`physical = virtual & 0x1FFFFFFF`

### 5.3 I/O Port Map (0x1F801000-0x1F802FFF)

| Address               | Size | Component                     |
| --------------------- | ---- | ----------------------------- |
| 0x1F801000-0x1F801023 | 36B  | Memory Control 1              |
| 0x1F801040-0x1F80104F | 16B  | Controller/Memory Card (SIO0) |
| 0x1F801050-0x1F80105F | 16B  | Serial Port (SIO1)            |
| 0x1F801060            | 4B   | RAM Size                      |
| 0x1F801070-0x1F801077 | 8B   | Interrupt Controller          |
| 0x1F801080-0x1F8010FF | 128B | DMA Registers                 |
| 0x1F801100-0x1F80112F | 48B  | Timers                        |
| 0x1F801800-0x1F801803 | 4B   | CD-ROM Controller             |
| 0x1F801810-0x1F801817 | 8B   | GPU                           |
| 0x1F801820-0x1F801827 | 8B   | MDEC                          |
| 0x1F801C00-0x1F801FFF | 1KB  | SPU Registers                 |
| 0x1F802000-0x1F802FFF | 4KB  | Expansion Region 2            |

### 5.4 Bus Timing

| Region     | Read (cycles)                     | Write (cycles)  |
| ---------- | --------------------------------- | --------------- |
| RAM        | 6 (8-bit), 6 (16-bit), 6 (32-bit) | same            |
| BIOS       | 24                                | N/A (read-only) |
| Scratchpad | 1                                 | 1               |
| I/O Ports  | Varies                            | Varies          |

**Instruction cache:** 4 KB I-cache, 1 KB D-cache (used as scratchpad).
Cache line = 4 words (16 bytes). On cache miss, 4 words are fetched.

---

## 6. DMA Controller

The PS1 has 7 DMA channels for high-bandwidth data transfer.

### 6.1 Channels

| Channel | Direction | Device   | Purpose                                     |
| ------- | --------- | -------- | ------------------------------------------- |
| 0       | To        | MDEC IN  | MDEC compressed data                        |
| 1       | From      | MDEC OUT | MDEC decompressed data                      |
| 2       | Both      | GPU      | VRAM transfers, command lists               |
| 3       | To        | CD-ROM   | CD sector data to RAM                       |
| 4       | To        | SPU      | Sound data to SPU RAM                       |
| 5       | From      | PIO      | Expansion port                              |
| 6       | —         | OTC      | Ordering table clear (GPU linked list init) |

### 6.2 Register Layout (per channel, offset from 0x1F801080)

```
0x1F801080 + N*0x10 + 0x00: MADR  - Memory Address Register
0x1F801080 + N*0x10 + 0x04: BCR   - Block Count Register
0x1F801080 + N*0x10 + 0x08: CHCR  - Channel Control Register
```

### 6.3 CHCR Register

```
Bit 0:     Direction (0=To main RAM, 1=From main RAM)
Bit 1:     Address Step (0=Forward +4, 1=Backward -4)
Bit 8:     Chopping Enable
Bits 9-11: Chopping DMA Window Size (1 << N words)
Bits 12-14: Chopping CPU Window Size (1 << N clks)
Bit 16-17: Sync Mode (0=Immediate, 1=Request/Block, 2=Linked-List)
Bits 18-20: (unused)
Bit 24:    Start/Busy (0=stopped, 1=start/running)
Bit 28:    Start/Trigger (for sync mode 0)
Bit 29:    Pause (for debug)
Bit 30:    Unknown (?)
```

### 6.4 Transfer Modes

- **Mode 0 (Immediate/Manual):** Transfer BCR.words at once. Used by OTC (channel 6).
- **Mode 1 (Request/Block):** Transfer BCR.blocks × BCR.blocksize words. Device
  controls flow via DREQ signal. Used by MDEC, SPU, CD-ROM.
- **Mode 2 (Linked List):** Follow pointer chain in RAM. Each entry:
  `[next_addr(24) | num_words(8)]` followed by num_words × 32-bit GPU commands.
  Terminator: header with bit 23 set (addr = 0xFFFFFF). Used by GPU (channel 2).

### 6.5 DMA Control Register (DPCR) — 0x1F8010F0

Bits 0-27: Enable bits for each channel (4 bits per channel).
`(DPCR >> (channel * 4)) & 0x8` = channel enabled.

### 6.6 DMA Interrupt Register (DICR) — 0x1F8010F4

```
Bits 0-5:   Unknown
Bit 6:      Unknown
Bits 15:    Force IRQ
Bits 16-22: Enable IRQ for channels 0-6
Bit 23:     Master IRQ Enable
Bits 24-30: IRQ Flags for channels 0-6 (write 1 to acknowledge)
Bit 31:     Master IRQ Flag (read-only)
              = Force OR (MasterEnable AND (Enable AND Flags) != 0)
```

---

## 7. GPU

### 7.1 Registers

| Address    | Name    | Access                          |
| ---------- | ------- | ------------------------------- |
| 0x1F801810 | GP0     | Write: Commands & VRAM data     |
| 0x1F801810 | GPUREAD | Read: VRAM data & GPU info      |
| 0x1F801814 | GP1     | Write: Display control commands |
| 0x1F801814 | GPUSTAT | Read: GPU status                |

### 7.2 GPUSTAT Register (Read 0x1F801814)

```
Bits 0-3:   Texture page X base (N*64)
Bit 4:      Texture page Y base (0 or 256)
Bits 5-6:   Semi-transparency mode (0=B/2+F/2, 1=B+F, 2=B-F, 3=B+F/4)
Bits 7-8:   Texture page color depth (0=4-bit, 1=8-bit, 2=15-bit, 3=reserved)
Bit 9:      Dither 24->15 bit (0=off, 1=dither)
Bit 10:     Drawing to display area (0=prohibited, 1=allowed)
Bit 11:     Mask bit setting (force bit15 of pixels)
Bit 12:     Draw pixels (0=always, 1=not to masked areas)
Bit 13:     Interlace field
Bit 14:     Reverse flag (distort)
Bit 15:     Texture disable
Bits 16-17: Horizontal resolution 2 (0=256/320/512/640, 1=368)
Bit 18:     Horizontal resolution 1 (0=256, 1=320 / 0=512, 1=640)
Bit 19:     Vertical resolution (0=240, 1=480 interlaced)
Bit 20:     Video mode (0=NTSC, 1=PAL)
Bit 21:     Display color depth (0=15-bit, 1=24-bit)
Bit 22:     Vertical interlace
Bit 23:     Display enable (0=enabled, 1=disabled)
Bit 24:     IRQ1 flag
Bit 25:     DMA request (depends on DMA direction)
Bit 26:     Ready to receive CMD word
Bit 27:     Ready to send VRAM to CPU
Bit 28:     Ready to receive DMA block
Bits 29-30: DMA direction (0=off, 1=?, 2=CPUtoGP0, 3=GPUREADtoCPU)
Bit 31:     Drawing even/odd lines (interlace)
```

### 7.3 GP0 Commands (Rendering)

| Command   | Parameters | Description                                        |
| --------- | ---------- | -------------------------------------------------- |
| 0x01      | 0          | Clear texture cache                                |
| 0x02      | 2          | Fill rectangle in VRAM                             |
| 0x20-0x3F | varies     | Polygons (triangles, quads; flat/gouraud/textured) |
| 0x40-0x5F | varies     | Lines (flat/gouraud; single/polyline)              |
| 0x60-0x7F | varies     | Rectangles (variable/1×1/8×8/16×16; flat/textured) |
| 0x80-0x9F | 3          | VRAM-to-VRAM copy                                  |
| 0xA0-0xBF | 2+N        | CPU-to-VRAM copy                                   |
| 0xC0-0xDF | 2          | VRAM-to-CPU copy                                   |
| 0xE1      | 0          | Draw mode setting                                  |
| 0xE2      | 0          | Texture window setting                             |
| 0xE3      | 0          | Set drawing area top-left                          |
| 0xE4      | 0          | Set drawing area bottom-right                      |
| 0xE5      | 0          | Set drawing offset                                 |
| 0xE6      | 0          | Mask bit setting                                   |

### 7.4 GP1 Commands (Display Control)

| Command | Description                                                 |
| ------- | ----------------------------------------------------------- |
| 0x00    | Reset GPU                                                   |
| 0x01    | Reset command buffer                                        |
| 0x02    | Acknowledge IRQ1                                            |
| 0x03    | Display Enable (0=on, 1=off)                                |
| 0x04    | DMA direction                                               |
| 0x05    | Start of display area in VRAM                               |
| 0x06    | Horizontal display range                                    |
| 0x07    | Vertical display range                                      |
| 0x08    | Display mode (resolution, NTSC/PAL, interlace, color depth) |
| 0x10    | Get GPU info                                                |

### 7.5 Rendering Details

**Coordinate system:** 11-bit signed (-1024 to +1023).
**Texture coordinates:** 8-bit (0-255).
**VRAM:** 1024×512 pixels, 16-bit per pixel (1-5-5-5 ABGR format).
**Texture pages:** 256×256 (4-bit), 256×256 (8-bit), or 256×256 (15-bit).
**CLUTs:** 16 or 256 entries, stored anywhere in VRAM.
**Drawing area:** Configurable rectangle within VRAM.

---

## 8. SPU — Sound Processing Unit

### 8.1 Overview

- 24 ADPCM voices
- 512 KB sound RAM
- Reverb effect processor
- CD audio mixing
- External audio input
- 44.1 kHz sample rate (22050 Hz per channel for stereo)

### 8.2 Voice Registers (0x1F801C00 + voice\*0x10)

| Offset | Name        | Description                            |
| ------ | ----------- | -------------------------------------- |
| 0x00   | VOL_L       | Volume left (16-bit)                   |
| 0x02   | VOL_R       | Volume right (16-bit)                  |
| 0x04   | ADPCM_RATE  | Sample rate (16-bit, 0x1000 = 44.1kHz) |
| 0x06   | ADPCM_START | ADPCM start address (÷8)               |
| 0x08   | ADSR_LO     | Attack/Decay/Sustain rates             |
| 0x0A   | ADSR_HI     | Sustain/Release rates                  |
| 0x0C   | ADSR_VOL    | Current ADSR volume (16-bit)           |
| 0x0E   | ADPCM_REP   | ADPCM repeat/loop address (÷8)         |

### 8.3 Global SPU Registers (0x1F801D80+)

| Address    | Name               | Description                               |
| ---------- | ------------------ | ----------------------------------------- |
| 0x1F801D80 | MAIN_VOL_L         | Main volume left                          |
| 0x1F801D82 | MAIN_VOL_R         | Main volume right                         |
| 0x1F801D84 | REVERB_VOL_L       | Reverb output volume left                 |
| 0x1F801D86 | REVERB_VOL_R       | Reverb output volume right                |
| 0x1F801D88 | KEY_ON             | Voice key on (24 bits across 2 halfwords) |
| 0x1F801D8C | KEY_OFF            | Voice key off                             |
| 0x1F801D90 | FM_MODE            | Frequency modulation enable               |
| 0x1F801D94 | NOISE_MODE         | Noise generator enable                    |
| 0x1F801D98 | REVERB_ON          | Reverb enable                             |
| 0x1F801D9C | VOICE_STATUS       | Voice on/off status (read-only)           |
| 0x1F801DA2 | REVERB_BASE        | Reverb work area start                    |
| 0x1F801DA4 | IRQ_ADDR           | Sound RAM IRQ address                     |
| 0x1F801DA6 | DATA_ADDR          | Data transfer address                     |
| 0x1F801DA8 | DATA_FIFO          | Data transfer FIFO                        |
| 0x1F801DAA | SPUCNT             | SPU control register                      |
| 0x1F801DAC | DATA_TRANSFER_CTRL | Transfer type                             |
| 0x1F801DAE | SPUSTAT            | SPU status register                       |
| 0x1F801DB0 | CD_VOL_L           | CD audio volume left                      |
| 0x1F801DB2 | CD_VOL_R           | CD audio volume right                     |
| 0x1F801DB4 | EXT_VOL_L          | External audio volume left                |
| 0x1F801DB6 | EXT_VOL_R          | External audio volume right               |

### 8.4 ADPCM Format

SPU ADPCM is a custom 4-bit ADPCM format with 16 samples per 16-byte block:

```
Byte 0:  Shift amount (bits 0-3) | Filter (bits 4-5) | unused (bits 6-7)
Byte 1:  Flags: bit0=loop end, bit1=loop repeat, bit2=loop start
Bytes 2-15: 28 nibbles of ADPCM data (2 samples per byte)
```

---

## 9. CD-ROM Controller

### 9.1 Register Interface (0x1F801800-0x1F801803)

The CD-ROM interface uses an index register to multiplex access to several
registers through only 4 I/O addresses.

| Address    | Index | Read          | Write                   |
| ---------- | ----- | ------------- | ----------------------- |
| 0x1F801800 | —     | Status        | Index                   |
| 0x1F801801 | 0     | Response FIFO | Command                 |
| 0x1F801801 | 1     | Response FIFO | Sound Map Data Out      |
| 0x1F801801 | 2     | Response FIFO | Sound Map Coding Info   |
| 0x1F801801 | 3     | Response FIFO | R Audio Volume R→SPU R  |
| 0x1F801802 | 0     | Data FIFO     | Parameter FIFO          |
| 0x1F801802 | 1     | Data FIFO     | Interrupt Enable        |
| 0x1F801802 | 2     | Data FIFO     | L Audio Volume L→SPU L  |
| 0x1F801802 | 3     | Data FIFO     | R Audio Volume R→SPU L  |
| 0x1F801803 | 0     | IRQ/Flags     | Request Register        |
| 0x1F801803 | 1     | IRQ/Flags     | Interrupt Flag (W: ack) |
| 0x1F801803 | 2     | IRQ/Flags     | L Audio Volume L→SPU R  |
| 0x1F801803 | 3     | IRQ/Flags     | Audio Volume Apply      |

### 9.2 Key Commands

| CMD  | Name    | Description                              |
| ---- | ------- | ---------------------------------------- |
| 0x01 | GetStat | Get status byte                          |
| 0x02 | SetLoc  | Set seek target (MM:SS:FF in BCD)        |
| 0x06 | ReadN   | Read with retry (normal speed)           |
| 0x09 | Pause   | Pause reading                            |
| 0x0A | Init    | Initialize CD controller                 |
| 0x0E | SetMode | Set read mode (speed, sector size, etc.) |
| 0x15 | SeekL   | Seek (data mode)                         |
| 0x19 | Test    | Various sub-commands                     |
| 0x1A | GetID   | Get disc type/region                     |
| 0x1B | ReadS   | Read without retry                       |
| 0x1E | ReadTOC | Read table of contents                   |

---

## 10. Timers

### 10.1 Three Timer Channels

| Timer | Clock Source Options          |
| ----- | ----------------------------- |
| 0     | System clock / Dot clock      |
| 1     | System clock / HBlank         |
| 2     | System clock / System clock÷8 |

### 10.2 Registers (per timer)

```
0x1F801100 + N*0x10 + 0x00: Current value (16-bit)
0x1F801100 + N*0x10 + 0x04: Mode register
0x1F801100 + N*0x10 + 0x08: Target value (16-bit)
```

### 10.3 Timer Mode Register

```
Bit 0:    Sync Enable (0=free run, 1=use sync mode)
Bits 1-2: Sync Mode (depends on timer)
Bit 3:    Reset counter on target (0=after 0xFFFF, 1=after target)
Bit 4:    IRQ when target reached
Bit 5:    IRQ when 0xFFFF overflow
Bit 6:    Repeat IRQ (0=once, 1=repeat)
Bit 7:    IRQ toggle/pulse (0=pulse/short, 1=toggle on/off)
Bit 8:    Clock Source (see per-timer table)
Bit 9:    Clock Source (timer 2 only: 0=sysclk, 1=sysclk/8)
Bit 10:   IRQ request flag (0=requested) — set to 1 on read
Bit 11:   Reached target
Bit 12:   Reached 0xFFFF overflow
```

---

## 11. Interrupt Controller

### 11.1 Registers

```
0x1F801070: I_STAT - Interrupt Status (read: pending, write: acknowledge)
0x1F801074: I_MASK - Interrupt Mask (1=enabled)
```

### 11.2 Interrupt Sources

| Bit | Source                        |
| --- | ----------------------------- |
| 0   | VBlank                        |
| 1   | GPU (GP0 IRQ command)         |
| 2   | CDROM                         |
| 3   | DMA                           |
| 4   | Timer 0                       |
| 5   | Timer 1                       |
| 6   | Timer 2                       |
| 7   | Controller/Memory Card (SIO0) |
| 8   | SIO1                          |
| 9   | SPU                           |
| 10  | Lightpen (PIO)                |

**IRQ delivery:** An interrupt is pending when `I_STAT & I_MASK != 0`.
This sets bit 10 of COP0 Cause register (IP2). The CPU takes the interrupt
when SR.IEc=1 and the corresponding SR.Im bit is set.

---

## 12. Controllers & Memory Cards (SIO)

### 12.1 Communication Protocol

Serial protocol at 250 kHz. Controller uses active-low /CS select.

**Digital pad response:**

```
Host sends: 0x01             → Controller responds: 0xFF (hi-z)
Host sends: 0x42             → Controller responds: 0x41 (digital pad ID)
Host sends: 0x00 (TAP)       → Controller responds: 0x5A (ready)
Host sends: 0x00             → Controller responds: buttons_lo
Host sends: 0x00             → Controller responds: buttons_hi
```

### 12.2 Button Mapping (active-LOW)

| Bit | buttons_lo | buttons_hi |
| --- | ---------- | ---------- |
| 0   | Select     | L2         |
| 1   | L3 (stick) | R2         |
| 2   | R3 (stick) | L1         |
| 3   | Start      | R1         |
| 4   | Up         | △          |
| 5   | Right      | ○          |
| 6   | Down       | ✕          |
| 7   | Left       | □          |

---

## 13. MDEC — Macroblock Decoder

Hardware JPEG-like decoder for FMV. Decompresses 8×8 or 16×16 macroblocks.

**Registers:**

```
0x1F801820: MDEC Command/Data (write) / Data Out (read)
0x1F801824: MDEC Control (write) / Status (read)
```

**Priority:** Low. Most games can run without MDEC (only affects FMV).

---

## 14. Boot Sequence & BIOS

### 14.1 Initial State

```
PC = 0xBFC00000 (BIOS entry point, KSEG1 uncached mapping of 0x1FC00000)
SP = 0x801FFF00 (top of main RAM, KSEG0)
SR = 0x10900000 (BEV=1, CU0=1, CU2=1)
All other registers = 0 or undefined
```

### 14.2 BIOS Boot Process

1. Initialize I-cache (uses cache isolation — SR.Isc=1)
2. Copy exception handlers to RAM (0x80000000, 0x80000080)
3. Initialize hardware (GPU, SPU, CD-ROM, controllers)
4. Display Sony logo
5. Check for valid PS1 disc
6. Load and execute PS-EXE from disc

### 14.3 BIOS Size

Standard BIOS ROM: 512 KB (0x80000 bytes), loaded at 0x1FC00000.

---

## 15. Debug Infrastructure Design

### 15.1 Design Goals

Learned from GBA emulator development: **make debugging trivial from day one.**

1. **Zero-cost when disabled:** All tracing behind compile-time `constexpr` flags
2. **Component-level granularity:** Enable tracing per-component (CPU, GPU, SPU, etc.)
3. **Structured data:** Logs include cycle counts, PC, register state — machine-parseable
4. **State snapshots:** Any component can dump full state on demand
5. **Instruction trace format:** Compatible with known PS1 trace logs for comparison
6. **Memory access log:** Optional logging of every bus read/write with address, value, size
7. **Performance counters:** Track instructions/sec, cache hits, DMA bandwidth in real-time

### 15.2 Trace Categories

```cpp
namespace PS1Trace {
    inline constexpr bool CPU = false;      // Instruction-level trace
    inline constexpr bool CPU_REGS = false;  // Register dumps after each instruction
    inline constexpr bool MEMORY = false;    // Bus read/write trace
    inline constexpr bool DMA = false;       // DMA transfer trace
    inline constexpr bool GPU_CMD = false;   // GPU command trace
    inline constexpr bool GPU_RENDER = false;// GPU primitive rendering trace
    inline constexpr bool SPU = false;       // SPU register/voice trace
    inline constexpr bool CDROM = false;     // CD-ROM command/response trace
    inline constexpr bool IRQ = false;       // Interrupt trace
    inline constexpr bool TIMER = false;     // Timer tick/IRQ trace
    inline constexpr bool GTE = false;       // GTE command trace
    inline constexpr bool EXCEPTIONS = false;// Exception/trap trace
}
```

### 15.3 State Dump Format

Each component must implement:

```cpp
void DumpState(std::ostream& os) const;
std::string GetDebugSummary() const;  // One-line summary for status bar
```

---

## 16. Implementation Priorities

### Phase 1: Boot to BIOS shell

1. CPU (R3000A) — all instructions, exceptions, delay slots
2. Memory Bus — full map with mirroring, cache isolation
3. COP0 — SR, Cause, EPC, exception handling
4. Interrupt Controller — basic I_STAT/I_MASK
5. DMA — channels 2 (GPU) and 6 (OTC)
6. GPU — GP1 display control, basic GPUSTAT
7. Timers — basic counting

### Phase 2: Run simple games

8. GPU — GP0 rendering (polygons, rectangles, lines)
9. GTE — RTPS/RTPT, NCLIP, AVSZ3/4
10. SPU — basic ADPCM playback
11. Controller — digital pad
12. CD-ROM — basic read commands

### Phase 3: Full compatibility

13. GPU — all semi-transparency, texture modes, VRAM copies
14. GTE — all commands with proper flag calculation
15. SPU — reverb, ADSR, all voice features
16. CD-ROM — full command set, XA audio
17. MDEC — FMV decoding
18. Memory Card — save/load

---

_Last updated: 2026-02-08_
_Sources: No$PSX documentation, Martin Korth's PSX-SPX, Avocado emulator docs_
