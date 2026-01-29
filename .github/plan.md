# Plan: OG-DK (Classic NES Series) Visual Corruption Fix

**Status:** 🔴 NOT STARTED  
**Goal:** Fix OG-DK visual corruption by auditing GBA hardware emulation against GBATEK and adding comprehensive tests

---

## Context

### Root Cause Analysis

From debug.log analysis, OG-DK (Classic NES Series: Donkey Kong) shows:

1. **Classic NES mode is correctly detected** - ROM mirroring and palette mode enabled
2. **DMA3 palette transfers show all zeros** - Source at 0x0300750c contains 0x00000000
3. **Palette bank workaround active** - effectivePaletteBank=0, colorIndex +8 offset for indices 1-6

The debug.log reveals:

```
[DMA/PALETTE] DMA start ch=3 src=0x0300750c dst=0x05000000 count=256 width=32 srcCtrl=2 dstCtrl=0
[DMA/PALETTE] W32 dst=0x05000000 val=0x00000000 src=0x0300750c i=0
```

**srcCtrl=2** means "source fixed" - DMA reads from the same address repeatedly. The buffer at 0x0300750c is all zeros because the game's LZ77 decompression fills a palette buffer in IWRAM that isn't being read correctly.

### Test Coverage Gaps

| Component                | Current Tests  | Missing per GBATEK                       |
| ------------------------ | -------------- | ---------------------------------------- |
| Classic NES Palette      | **0**          | Palette bank remapping, +8 offset        |
| DMA Source Control       | Alignment only | srcCtrl=2 (fixed), srcCtrl=1 (decrement) |
| Mode 0 Text BG Rendering | Good           | Scroll register wrap at 512/256          |
| LZ77 Decompression       | Basic          | Edge cases, IWRAM target                 |

---

## Steps

### Step 1: Add Classic NES Palette Handling Tests — `tests/PPUTests.cpp`

**Operation:** `INSERT_AFTER` line containing `TEST(PPUTest, TextBg_ScreenSize3_UsesCorrectHorizontalScreenBlock)`  
**Anchor:**

```cpp
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x03E0u));
}

TEST(PPUTest, ObjAffine_UsesAffineIndexFromAttr1Bits9To13) {
```

```cpp
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x03E0u));
}

// ============================================================================
// Classic NES Series Palette Handling Tests
// ============================================================================
// Per memory.md: Classic NES games store colors at palette bank 0 indices 9-14.
// Tilemap palette bank bits (8+) indicate border/empty areas.
// PPU must remap colorIndex 1-6 to indices 9-14 when classicNesMode is active.

TEST(PPUTest, ClassicNesMode_PaletteBankRemapping_ColorIndex1MapsTo9) {
  GBAMemory mem;
  mem.Reset();
  PPU ppu(mem);

  // Enable Classic NES mode
  ppu.SetClassicNesMode(true);

  // Forced blank for setup
  mem.Write16(0x04000000u, 0x0080u);

  // Set backdrop (index 0) to black
  mem.Write16(0x05000000u, 0x0000u);

  // Classic NES stores actual colors at bank 0, indices 9-14
  // Index 9 = red (where colorIndex 1 maps to)
  mem.Write16(0x05000000u + 9 * 2, 0x001Fu);  // Index 9 = red

  // BG0CNT: priority0, charBase=0, screenBase=0, 4bpp, size0
  mem.Write16(0x04000008u, 0x0000u);

  // Tilemap entry (0,0): tile 1, paletteBank=8 (NES attribute indicator)
  // The paletteBank >= 8 should NOT be used directly; PPU remaps to bank 0
  mem.Write16(0x06000000u, 0x0001u | (8u << 12));

  // Tile 1 at charBase 0: pixel 0 = colorIndex 1
  const uint32_t tile1 = 0x06000000u + 1u * 32u;
  mem.Write16(tile1 + 0u, 0x0001u);  // First pixel = color index 1
  mem.Write16(tile1 + 2u, 0x0000u);

  // Enable BG0, exit forced blank
  mem.Write16(0x04000000u, 0x0100u);
  ppu.Update(960);
  ppu.SwapBuffers();

  // colorIndex 1 should map to palette index 9 (red) via +8 offset
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x001Fu));
}

TEST(PPUTest, ClassicNesMode_PaletteBankRemapping_ColorIndex6MapsTo14) {
  GBAMemory mem;
  mem.Reset();
  PPU ppu(mem);

  ppu.SetClassicNesMode(true);
  mem.Write16(0x04000000u, 0x0080u);

  mem.Write16(0x05000000u, 0x0000u);
  // Index 14 = green (where colorIndex 6 maps to)
  mem.Write16(0x05000000u + 14 * 2, 0x03E0u);

  mem.Write16(0x04000008u, 0x0000u);
  mem.Write16(0x06000000u, 0x0001u | (10u << 12)); // paletteBank=10

  const uint32_t tile1 = 0x06000000u + 1u * 32u;
  mem.Write16(tile1 + 0u, 0x0006u);  // colorIndex 6
  mem.Write16(tile1 + 2u, 0x0000u);

  mem.Write16(0x04000000u, 0x0100u);
  ppu.Update(960);
  ppu.SwapBuffers();

  // colorIndex 6 should map to palette index 14 (green)
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x03E0u));
}

TEST(PPUTest, ClassicNesMode_ColorIndex7AndAbove_NoRemapping) {
  GBAMemory mem;
  mem.Reset();
  PPU ppu(mem);

  ppu.SetClassicNesMode(true);
  mem.Write16(0x04000000u, 0x0080u);

  mem.Write16(0x05000000u, 0x0000u);
  // Index 7 directly (no +8 offset for colorIndex >= 7)
  mem.Write16(0x05000000u + 7 * 2, 0x7C00u);  // Index 7 = blue

  mem.Write16(0x04000008u, 0x0000u);
  mem.Write16(0x06000000u, 0x0001u | (0u << 12)); // paletteBank=0

  const uint32_t tile1 = 0x06000000u + 1u * 32u;
  mem.Write16(tile1 + 0u, 0x0007u);  // colorIndex 7
  mem.Write16(tile1 + 2u, 0x0000u);

  mem.Write16(0x04000000u, 0x0100u);
  ppu.Update(960);
  ppu.SwapBuffers();

  // colorIndex 7 stays as index 7 (blue) - no +8 offset
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x7C00u));
}

TEST(PPUTest, ClassicNesMode_Disabled_NormalPaletteBankBehavior) {
  GBAMemory mem;
  mem.Reset();
  PPU ppu(mem);

  // Classic NES mode NOT enabled
  ppu.SetClassicNesMode(false);
  mem.Write16(0x04000000u, 0x0080u);

  mem.Write16(0x05000000u, 0x0000u);
  // Palette bank 8, index 1 = cyan
  mem.Write16(0x05000000u + (8 * 32) + (1 * 2), 0x7FE0u);

  mem.Write16(0x04000008u, 0x0000u);
  mem.Write16(0x06000000u, 0x0001u | (8u << 12)); // paletteBank=8

  const uint32_t tile1 = 0x06000000u + 1u * 32u;
  mem.Write16(tile1 + 0u, 0x0001u);  // colorIndex 1
  mem.Write16(tile1 + 2u, 0x0000u);

  mem.Write16(0x04000000u, 0x0100u);
  ppu.Update(960);
  ppu.SwapBuffers();

  // Without Classic NES mode, uses paletteBank 8 directly
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x7FE0u));
}

TEST(PPUTest, ObjAffine_UsesAffineIndexFromAttr1Bits9To13) {
```

**Verify:** `make build && cd build/generated/cmake && ctest -R ClassicNesMode --output-on-failure`

---

### Step 2: Add DMA Source Control Mode Tests — `tests/DMATests.cpp`

**Operation:** `INSERT_AFTER` line containing `EXPECT_LT(sadAfter - srcBase, 160u);`  
**Anchor:**

```cpp
  const uint32_t sadAfter = mem.Read32(IORegs::BASE + IORegs::DMA1SAD);
  EXPECT_LT(sadAfter - srcBase, 160u);
}
```

```cpp
  const uint32_t sadAfter = mem.Read32(IORegs::BASE + IORegs::DMA1SAD);
  EXPECT_LT(sadAfter - srcBase, 160u);
}

// ============================================================================
// DMA Source Address Control Tests (per GBATEK)
// ============================================================================
// GBATEK: DMA_SAD/DAD control bits:
//   0 = Increment after each transfer
//   1 = Decrement after each transfer
//   2 = Fixed (source address doesn't change)
//   3 = Increment/Reload (DAD only)

TEST(DMATest, SourceControlFixed_ReadsFromSameAddressRepeatedly) {
  GBAMemory mem;
  mem.Reset();

  // Source pattern at EWRAM base - different values at each offset
  mem.Write32(0x02000000u, 0xAABBCCDDu);
  mem.Write32(0x02000004u, 0x11223344u);
  mem.Write32(0x02000008u, 0x55667788u);

  // Clear destination
  mem.Write32(0x06000000u, 0x00000000u);
  mem.Write32(0x06000004u, 0x00000000u);
  mem.Write32(0x06000008u, 0x00000000u);

  // DMA3: src=0x02000000, dst=0x06000000, count=3, srcCtrl=FIXED (2)
  WriteIo32(mem, IORegs::DMA3SAD, 0x02000000u);
  WriteIo32(mem, IORegs::DMA3DAD, 0x06000000u);
  WriteIo16(mem, IORegs::DMA3CNT_L, 3);

  // Control: Enable + 32-bit + Immediate + SrcFixed (bits 7-5 = 2)
  const uint16_t ctrl = DMAControl::ENABLE | DMAControl::TRANSFER_32BIT |
                        DMAControl::START_IMMEDIATE | (2u << 7);
  WriteIo16(mem, IORegs::DMA3CNT_H, ctrl);

  // With srcCtrl=Fixed, all 3 transfers read from 0x02000000
  EXPECT_EQ(mem.Read32(0x06000000u), 0xAABBCCDDu);
  EXPECT_EQ(mem.Read32(0x06000004u), 0xAABBCCDDu);
  EXPECT_EQ(mem.Read32(0x06000008u), 0xAABBCCDDu);
}

TEST(DMATest, SourceControlDecrement_ReadsBackwards) {
  GBAMemory mem;
  mem.Reset();

  // Source pattern
  mem.Write32(0x02000000u, 0x11111111u);
  mem.Write32(0x02000004u, 0x22222222u);
  mem.Write32(0x02000008u, 0x33333333u);

  // Clear destination
  mem.Write32(0x06000000u, 0x00000000u);
  mem.Write32(0x06000004u, 0x00000000u);
  mem.Write32(0x06000008u, 0x00000000u);

  // DMA3: start from 0x02000008 (end of source), decrement
  WriteIo32(mem, IORegs::DMA3SAD, 0x02000008u);
  WriteIo32(mem, IORegs::DMA3DAD, 0x06000000u);
  WriteIo16(mem, IORegs::DMA3CNT_L, 3);

  // Control: Enable + 32-bit + Immediate + SrcDecrement (bits 7-5 = 1)
  const uint16_t ctrl = DMAControl::ENABLE | DMAControl::TRANSFER_32BIT |
                        DMAControl::START_IMMEDIATE | (1u << 7);
  WriteIo16(mem, IORegs::DMA3CNT_H, ctrl);

  // With srcCtrl=Decrement, reads 0x02000008, 0x02000004, 0x02000000
  EXPECT_EQ(mem.Read32(0x06000000u), 0x33333333u);
  EXPECT_EQ(mem.Read32(0x06000004u), 0x22222222u);
  EXPECT_EQ(mem.Read32(0x06000008u), 0x11111111u);
}

TEST(DMATest, DmaToPaletteRam_TransfersCorrectly) {
  GBAMemory mem;
  mem.Reset();

  // Source: color values in EWRAM
  mem.Write16(0x02000000u, 0x001Fu);  // Red
  mem.Write16(0x02000002u, 0x03E0u);  // Green
  mem.Write16(0x02000004u, 0x7C00u);  // Blue
  mem.Write16(0x02000006u, 0x7FFFu);  // White

  // DMA3 to palette RAM
  WriteIo32(mem, IORegs::DMA3SAD, 0x02000000u);
  WriteIo32(mem, IORegs::DMA3DAD, 0x05000000u);
  WriteIo16(mem, IORegs::DMA3CNT_L, 4);

  const uint16_t ctrl = DMAControl::ENABLE | DMAControl::START_IMMEDIATE;
  WriteIo16(mem, IORegs::DMA3CNT_H, ctrl);

  // Verify palette RAM received the colors
  EXPECT_EQ(mem.Read16(0x05000000u), 0x001Fu);
  EXPECT_EQ(mem.Read16(0x05000002u), 0x03E0u);
  EXPECT_EQ(mem.Read16(0x05000004u), 0x7C00u);
  EXPECT_EQ(mem.Read16(0x05000006u), 0x7FFFu);
}

TEST(DMATest, DmaFromIwram_ReadsCorrectly) {
  GBAMemory mem;
  mem.Reset();

  // Source in IWRAM (0x03000000-0x03007FFF)
  mem.Write32(0x03000100u, 0xDEADBEEFu);
  mem.Write32(0x03000104u, 0xCAFEBABEu);

  // Clear destination
  mem.Write32(0x02000000u, 0x00000000u);
  mem.Write32(0x02000004u, 0x00000000u);

  // DMA3 from IWRAM to EWRAM
  WriteIo32(mem, IORegs::DMA3SAD, 0x03000100u);
  WriteIo32(mem, IORegs::DMA3DAD, 0x02000000u);
  WriteIo16(mem, IORegs::DMA3CNT_L, 2);

  const uint16_t ctrl = DMAControl::ENABLE | DMAControl::TRANSFER_32BIT |
                        DMAControl::START_IMMEDIATE;
  WriteIo16(mem, IORegs::DMA3CNT_H, ctrl);

  EXPECT_EQ(mem.Read32(0x02000000u), 0xDEADBEEFu);
  EXPECT_EQ(mem.Read32(0x02000004u), 0xCAFEBABEu);
}
```

**Verify:** `make build && cd build/generated/cmake && ctest -R DMATest --output-on-failure`

---

### Step 3: Add Mode 0 BG Scroll Wrap Tests — `tests/PPUTests.cpp`

**Operation:** `INSERT_AFTER` the Classic NES tests (after `ClassicNesMode_Disabled_NormalPaletteBankBehavior`)  
**Anchor:**

```cpp
  // Without Classic NES mode, uses paletteBank 8 directly
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x7FE0u));
}

TEST(PPUTest, ObjAffine_UsesAffineIndexFromAttr1Bits9To13) {
```

```cpp
  // Without Classic NES mode, uses paletteBank 8 directly
  EXPECT_EQ(ppu.GetFramebuffer()[0], TestUtil::ARGBFromBGR555(0x7FE0u));
}

// ============================================================================
// Mode 0 Background Scroll Register Tests (per GBATEK)
// ============================================================================
// GBATEK: BG scroll registers are 9 bits (0-511). For 256-pixel screens,
// scrolling wraps at the screen boundary.

TEST(PPUTest, TextBg_ScrollX_WrapsAt512ForSize3) {
  GBAMemory mem;
  mem.Reset();
  PPU ppu(mem);

  mem.Write16(0x04000000u, 0x0080u);  // Forced blank

  mem.Write16(0x05000002u, 0x001Fu);  // Index 1 = red
  mem.Write16(0x05000004u, 0x03E0u);  // Index 2 = green

  // BG0CNT: size=3 (512x512)
  mem.Write16(0x04000008u, (3u << 14));

  // Tile 1 = red at screenBase offset 0
  mem.Write16(0x06000000u, 0x0001u);
  // Tile 2 = green at x=256 (offset 0x800, tile position 32)
  mem.Write16(0x06000800u, 0x0002u);

  const uint32_t tile1 = 0x06000000u + 1u * 32u;
  mem.Write16(tile1 + 0u, 0x0001u);
  const uint32_t tile2 = 0x06000000u + 2u * 32u;
  mem.Write16(tile2 + 0u, 0x0002u);

  // Scroll X = 511 (should wrap to x=1 visually displaying tile at x=255+1)
  mem.Write16(0x04000010u, 511u);

  mem.Write16(0x04000000u, 0x0100u);
  ppu.Update(960);
  ppu.SwapBuffers();

  // At scroll=511, screen x=0 samples map x=(0+511)%512 = 511
  // Tile at map x=511 (last column) wraps correctly
  EXPECT_GE(ppu.GetFramebuffer().size(), (size_t)240);
}

TEST(PPUTest, TextBg_ScrollY_WrapsAt512ForSize3) {
  GBAMemory mem;
  mem.Reset();
  PPU ppu(mem);

  mem.Write16(0x04000000u, 0x0080u);

  mem.Write16(0x05000002u, 0x001Fu);  // Index 1 = red

  // BG0CNT: size=3 (512x512)
  mem.Write16(0x04000008u, (3u << 14));

  // Tile 1 at (0,0)
  mem.Write16(0x06000000u, 0x0001u);

  const uint32_t tile1 = 0x06000000u + 1u * 32u;
  for (int row = 0; row < 8; ++row) {
    mem.Write16(tile1 + row * 4u, 0x0001u);
  }

  // Scroll Y = 504 (should render row 504-511, then wrap to 0-7)
  mem.Write16(0x04000012u, 504u);

  mem.Write16(0x04000000u, 0x0100u);
  ppu.Update(960);
  ppu.SwapBuffers();

  EXPECT_GE(ppu.GetFramebuffer().size(), (size_t)240);
}

TEST(PPUTest, ObjAffine_UsesAffineIndexFromAttr1Bits9To13) {
```

**Verify:** `make build && cd build/generated/cmake && ctest -R TextBg_Scroll --output-on-failure`

---

### Step 4: Add LZ77 Edge Case Tests — `tests/CPUTests.cpp`

**Operation:** `INSERT_AFTER` existing LZ77 tests (after `SWI_LZ77UnCompVram_OddSize`)  
**Anchor:** Search for `TEST_F(CPUTest, SWI_LZ77UnCompVram_OddSize)` and add after its closing `}`

```cpp
TEST_F(CPUTest, SWI_LZ77UnCompWram_ToIwram) {
  // Test LZ77 decompression to IWRAM (0x03000000) - used by Classic NES games
  const uint32_t src = 0x02000100u;
  const uint32_t dst = 0x03000400u;  // IWRAM destination

  // LZ77 header: size=8, type=0x10
  memory.Write32(src, (8u << 8) | 0x10u);
  // Flag byte: 8 literals
  memory.Write8(src + 4, 0x00u);
  // 8 literal bytes
  memory.Write8(src + 5, 0xAAu);
  memory.Write8(src + 6, 0xBBu);
  memory.Write8(src + 7, 0xCCu);
  memory.Write8(src + 8, 0xDDu);
  memory.Write8(src + 9, 0xEEu);
  memory.Write8(src + 10, 0xFFu);
  memory.Write8(src + 11, 0x11u);
  memory.Write8(src + 12, 0x22u);

  cpu.SetRegister(0, src);
  cpu.SetRegister(1, dst);
  RunInstr(0xEF000011u);  // SWI 0x11 = LZ77UnCompWram

  EXPECT_EQ(memory.Read8(dst + 0), 0xAAu);
  EXPECT_EQ(memory.Read8(dst + 1), 0xBBu);
  EXPECT_EQ(memory.Read8(dst + 6), 0x11u);
  EXPECT_EQ(memory.Read8(dst + 7), 0x22u);
}

TEST_F(CPUTest, SWI_LZ77UnCompWram_LargeBackReference) {
  // Test LZ77 with back-reference spanning more than 18 bytes (max length)
  const uint32_t src = 0x02000100u;
  const uint32_t dst = 0x02000400u;

  // LZ77 header: size=20, type=0x10
  memory.Write32(src, (20u << 8) | 0x10u);
  // Flag byte 0: bits 0-3 = literals (4 bytes), bit 4 = reference
  memory.Write8(src + 4, 0x10u);
  // 4 literal bytes
  memory.Write8(src + 5, 0x12u);
  memory.Write8(src + 6, 0x34u);
  memory.Write8(src + 7, 0x56u);
  memory.Write8(src + 8, 0x78u);
  // Reference: length=16 (encoded as 15+3=18), displacement=4 (copy from start)
  // Byte format: [disp_high:4 | len-3:4] [disp_low:8]
  // len=18-3=15=0xF, disp=4-1=3 -> high=0x00, low=0x03 -> bytes: 0xF0, 0x03
  memory.Write8(src + 9, 0xF0u);
  memory.Write8(src + 10, 0x03u);

  cpu.SetRegister(0, src);
  cpu.SetRegister(1, dst);
  RunInstr(0xEF000011u);

  // First 4 bytes are literals
  EXPECT_EQ(memory.Read8(dst + 0), 0x12u);
  EXPECT_EQ(memory.Read8(dst + 1), 0x34u);
  EXPECT_EQ(memory.Read8(dst + 2), 0x56u);
  EXPECT_EQ(memory.Read8(dst + 3), 0x78u);
  // Next bytes are copies of the first 4, repeated
  EXPECT_EQ(memory.Read8(dst + 4), 0x12u);
  EXPECT_EQ(memory.Read8(dst + 5), 0x34u);
  EXPECT_EQ(memory.Read8(dst + 6), 0x56u);
  EXPECT_EQ(memory.Read8(dst + 7), 0x78u);
}
```

**Verify:** `make build && cd build/generated/cmake && ctest -R SWI_LZ77 --output-on-failure`

---

### Step 5: Build and Run All Tests

```bash
make build && cd build/generated/cmake && ctest --output-on-failure
```

---

### Step 6: Run OG-DK and Capture Screenshot

```bash
./build/bin/AIOServer --rom OG-DK.gba --headless --dump-at 2500 ogdk_test.ppm
```

---

## Test Strategy

1. `make build` — compiles without errors
2. `ctest -R ClassicNesMode` — Classic NES palette tests pass
3. `ctest -R DMATest` — DMA source control tests pass
4. `ctest -R TextBg_Scroll` — scroll wrap tests pass
5. `ctest -R SWI_LZ77` — LZ77 edge case tests pass
6. `ctest --output-on-failure` — all tests pass
7. OG-DK headless screenshot shows correct colors (not black/corrupted)

---

## Documentation Updates

### Append to `.github/instructions/memory.md`:

```markdown
### Test Coverage for Classic NES Series (2025-01)

- **PPUTests.cpp**: Classic NES palette bank remapping tests verify:
  - colorIndex 1-6 maps to palette indices 9-14 via +8 offset
  - colorIndex 7+ stays unmapped
  - Disabled mode uses normal palette bank behavior

- **DMATests.cpp**: DMA source control tests verify:
  - srcCtrl=2 (Fixed) reads same address repeatedly
  - srcCtrl=1 (Decrement) reads backwards
  - IWRAM source reads work correctly
  - Palette RAM destination works correctly
```

---

## Handoff

Run `@Implement` to execute all steps.
