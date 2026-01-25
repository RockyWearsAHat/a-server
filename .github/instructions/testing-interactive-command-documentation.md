# This file is purely for documenting helpful testing commands and how to use them. This is meant purely for editing and reading by the @Plan agent, and reads from @Implement, @agent feel free to note commands that may be helpful and an entry in the index for quick finds

## Index (In Order of Manual Entries)

1. OGDK Instruction-Level Tracer
2. Headless ROM Test with PPM Output

## Manual (Index Title, Description of the Command, and a Copy/Paste Line)

### 1. OGDK Instruction-Level Tracer

Traces CPU instructions executing in IWRAM (0x03xxxxxx) and ROM (0x08000000-0x08001000).
Useful for debugging Classic NES Series games and understanding boot sequence.

**Enable env var:** `AIO_TRACE_OGDK_INSTR=1`

**Command:**

```bash
cd "/Users/alexwaldmann/Desktop/AIO Server" && AIO_TRACE_OGDK_INSTR=1 ./build/bin/AIOServer --headless --rom OG-DK.gba --headless-max-ms 500 2>&1 | head -100
```

**Output format:**

```
[OGDK_INSTR] PC=0x03007400 A op=0xe59fc040 R0=0x08005ff4 R1=0x03007400 R2=... CPSR=0x0000001f
[OGDK_ROM] PC=0x08000000 A op=0xea000031 R0=0x00000000 R1=0x00000000 R12=0x00000000
```

### 2. Headless ROM Test with PPM Output

Run a ROM headlessly and capture a frame at specified time for visual verification.

**Command:**

```bash
./build/bin/AIOServer --headless --rom OG-DK.gba --headless-max-ms 5000 --headless-dump-ppm /tmp/ogdk.ppm --headless-dump-ms 4500
```

**Parameters:**

- `--headless-max-ms` — Total runtime in milliseconds
- `--headless-dump-ppm` — Output PPM file path
- `--headless-dump-ms` — When to capture frame (ms from start)

### 3. OG-DK Anti-Piracy Analysis

The Classic NES Series (OG-DK, etc.) has an anti-piracy scan loop that searches EWRAM for sentinel value 0x040000D4 (DMA3SAD I/O address).

**Key finding:** Early EWRAM offsets (0x4-0x3F0) get overwritten by the IWRAM decompressor before the scan runs. The sentinel must be placed at a HIGH offset (0x3FF00) to survive.

**Trace command:**

```bash
AIO_TRACE_OGDK_INSTR=1 ./build/bin/AIOServer --headless --rom OG-DK.gba --headless-max-ms 500 2>&1 | grep "PC=0x080009c" | head -20
```

### 4. OG-DK Anti-Piracy Correction (2026-01-23)

**IMPORTANT:** The analysis in section 3 was INCORRECT.

**Correct finding:** The scan loop at 0x080009C8 scans **ROM** (0x08xxxxxx), NOT EWRAM!

- Instruction `e3a03302` decodes to `MOV R3, #0x08000000` (not 0x02000000)
- ARM immediate encoding: rotate=3, imm8=0x02 → 0x02 ROR 6 = 0x08000000
- Sentinel 0x040000D4 exists at ROM offset 0x118 and is found correctly

**The anti-piracy scan works correctly and is NOT the cause of OG-DK black screen!**

### 5. Non-Black Pixel Analysis

Quick check if PPM output has visual content:

```bash
python3 -c "from PIL import Image; img=Image.open('/tmp/ogdk.ppm'); p=list(img.getdata()); t=len(p); n=sum(1 for x in p if x!=(0,0,0) and sum(x)>10); print(f'{n/t:.4f} ({n}/{t})')"
```

### 6. OG-DK Boot Sequence Analysis (2026-01-24)

**Boot sequence confirmed via instruction tracing:**

1. ROM entry at 0x08000000
2. LDMIA at 0x080000D0 loads registers from ROM at 0x08000110
3. SWI 0x11 decompresses code to IWRAM at 0x03007400
4. Secondary decompressor runs at 0x03007400, writes to 0x03000000-0x03002000

**ROM data dump commands:**

```bash
# LZ77 header (source for SWI 0x11)
xxd -s $((0x5ff4)) -l 16 OG-DK.gba

# LDMIA data (R0,R1,R4,R6,R7,R8,R9 initialization)
xxd -s $((0x110)) -l 28 OG-DK.gba

# Secondary decompressor source
xxd -s $((0x6110)) -l 32 OG-DK.gba
```

**Enhanced instruction tracer (shows R6-R9 for decompressor analysis):**

```bash
AIO_TRACE_OGDK_INSTR=1 ./build/bin/AIOServer --headless --rom OG-DK.gba --headless-max-ms 200 2>&1 | grep "OGDK_INSTR" | head -50
```

### 7. Performance Comparison

DKC vs OG-DK speed check:

```bash
# DKC should produce many lines quickly
timeout 3 ./build/bin/AIOServer --headless --rom DKC.gba --headless-max-ms 1000 2>&1 | wc -l

# OG-DK runs ~100x slower due to tight decompression loop
timeout 3 ./build/bin/AIOServer --headless --rom OG-DK.gba --headless-max-ms 1000 2>&1 | wc -l
```
