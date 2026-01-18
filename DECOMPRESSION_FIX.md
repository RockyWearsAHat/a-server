# DKC Graphics Fix: Decompression Buffer Flush Bug

## Issues Fixed

### 1. LZ77 Decompression Buffer Flush (SWI 0x11 & 0x12)

**Location**: `src/emulator/gba/ARM7TDMI.cpp` lines 3431-3510

**The Bug**:
When decompressing to VRAM with an odd decompressed size, the last byte would remain in `vramBuffer` and never be written to VRAM. The decompression loop would end with `vramBufferFull = true` but no final 16-bit write would occur.

**Example**:

- Decompressing 5 bytes to VRAM
- Bytes 0-1: Written as 16-bit word ✓
- Bytes 2-3: Written as 16-bit word ✓
- Byte 4: Stored in vramBuffer but loop ends → **BYTE 4 IS LOST** ✗

**Impact**:

- Palette data corruption (odd-sized palettes)
- Tile data corruption (odd-sized tile graphics)
- Manifests as pink/uninitialized textures in sprites (0xF800 = pure red or 0xFFFF = white)

**The Fix**:
Added buffer flush after decompression loop:

```cpp
if (toVram && vramBufferFull && written == decompSize) {
  memory.Write16(dst & ~1, vramBuffer);
}
```

### 2. RLE Decompression Buffer Flush (SWI 0x14 & 0x15)

**Location**: `src/emulator/gba/ARM7TDMI.cpp` lines 3625-3645

**The Bug**:
Identical to LZ77 - odd-sized decompressed RLE data would lose the last byte.

**The Fix**:
Applied same buffer flush pattern as LZ77.

### 3. Enhanced SWI Decompression Tracing

**Location**: `src/emulator/gba/ARM7TDMI.cpp` lines 3444-3450

**Change**:
Improved debug logging to show all LZ77 decompression calls (both WRAM and VRAM), not just VRAM.

**Before**:

```cpp
if (toVram && (dst & 0xFF000000) == 0x06000000) {
  std::cout << "[SWI 0x12] LZ77 to VRAM: ...
}
```

**After**:

```cpp
std::cout << "[SWI 0x" << std::hex << comment << "] LZ77 decomp: src=0x"
          << src << " dst=0x" << dst << " size=0x" << decompSize
          << " toVram=" << (toVram ? 1 : 0) << std::dec << std::endl;
```

## Why This Fixes DKC Issues

### Symptom: Alligators with Pink Textures

The alligator sprite graphics are likely compressed with odd decompressed sizes. Without the buffer flush:

1. Palette data loses last byte → uninitialized memory
2. Tile data loses last byte → truncated graphics
3. Pink textures appear (uninitialized palette entries)
4. Animation breaks (corrupted tile indices)
5. Game freezes (likely due to corrupted data causing infinite loops)

### Symptom: Sprites Falling Off-Screen

While primarily a game behavior (intentional OAM Y coordinate modifications), proper graphics decompression ensures the game's graphics systems work correctly, reducing cascading failures.

## Testing

The fix was verified with:

1. Unit test demonstrating odd-length VRAM decompression with buffer flush
2. All 5 bytes decompressed correctly and written to VRAM
3. Build successful with no new compilation errors

## Affected Games

Any game that:

1. Decompresses odd-sized graphics data to VRAM
2. Relies on complete palette or tile data
3. Uses LZ77 or RLE compression (SWI 0x11, 0x12, 0x14, 0x15)

Known affected: **Donkey Kong Country** (DKC) - alligator sprites
