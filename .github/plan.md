# Plan: Fix Classic NES Series (OG-DK) Palette Rendering

**Status:** 🔴 NOT STARTED  
**Goal:** Fix OG-DK showing only 1% red when mgba shows 20%+ red on title screen

---

## Context

### Investigation Summary

**Issue:** OG-DK.gba shows only ~1% red pixels (367); mgba shows ~20% red.

**VRAM Analysis (debug.log):** Tile data contains both:

- Low colorIndex (1-6): These get +8 offset → palette 9-14 ✓
- High colorIndex (9-14): These use direct index → palette 9-14 ✓

**Current Palette Logic (PPU.cpp ~line 2099):**

`````cpp
if (applyClassicNesPaletteOffset && !is8bpp && colorIndex != 0) {
  if (colorIndex <= 6) {
    ````markdown
    # Plan: Fix OG-DK (Classic NES Series) Corrupted Tiles

    **Status:** 🔴 NOT STARTED
    **Goal:** Fix OG-DK.gba (FDKE) severe BG tile corruption (wrong glyphs/patterns) without ROM-specific hacks.

    ---

    ## Diagnosis (working hypothesis)

    The current `classicNesMode` path in `PPU::RenderBackground()` contains a heuristic that dynamically chooses tile bases in VRAM for Classic NES titles. This is a game-specific workaround and can easily select incorrect data, producing repeating patterns.

    Separately, our BG tile fetch path currently reads VRAM through a unified 96KB view and (per existing tests) allows BG fetches to read from the OBJ region when tile indices overflow. GBATEK documents BG character/screen fetches as wrapping within the BG VRAM window for text modes. Classic NES-on-GBA ROMs appear to rely on correct BG wrapping (and/or not reading OBJ VRAM) to avoid pulling sprite/font data into BG.

    **Fix direction:** make BG tilemap + tile-graphics reads in text BG modes wrap within the 64KB BG VRAM window, and remove the Classic NES tile-base heuristic.

    ---

    ## Step 1 — Documentation update (spec-first)

    **File:** `.github/instructions/memory.md`

    **Operation:** `INSERT` under `### Classic NES Series / NES-on-GBA ROMs`

    ```markdown
    #### BG VRAM wrapping (text modes)

    For text backgrounds (modes 0–2), BG tilemap (screen blocks) and tile graphics (character blocks) are addressed within the BG VRAM window.

    Implementation invariant (spec-driven): BG fetches for text modes wrap within $64\,\text{KB}$ (offset mask `0xFFFF`), i.e. BG rendering must not accidentally pull tilemap/tile data from the OBJ VRAM region.
    ```

    ---

    ## Step 2 — Tests (fail first)

    ### 2.1 Update existing test to match spec

    **File:** `tests/PPUTests.cpp`

    **Operation:** `UPDATE` test `PPUTest.BgTileFetchDoesNotReadFromObjVram_Mode0`

    **Old expectation (current behavior):** BG tile fetch reads OBJ VRAM when tile index overflows into 0x06010000.

    **New expectation (spec):** BG tile fetch wraps within 64KB BG VRAM, so tile index 512 with charBase=3 wraps to 0x06000000 and produces red.

    ```cpp
    // Render scanline 0 and sample pixel (0,0). Spec behavior: BG fetch wraps
    // within 64KB BG VRAM, so tile 512 wraps and reads from BG VRAM => red.
    ppu.Update(TestUtil::kCyclesToHBlankStart);
    ppu.SwapBuffers();
    EXPECT_EQ(TestUtil::GetPixel(ppu, 0, 0), TestUtil::ARGBFromBGR555(0x001F));
    ```

    ### 2.2 Add a map-wrapping test for size=3

    **File:** `tests/PPUTests.cpp`

    **Operation:** `ADD` new test

    ```cpp
    TEST(PPUTest, BgTileMapWrapsWithin64K_Mode0_Size3) {
      GBAMemory mem;
      mem.Reset();

      // Mode 0, BG0 enabled.
      mem.Write16(0x04000000u, 0x0100u);

      // Palette: idx1=red, idx2=green.
      mem.Write16(0x05000002u, 0x001Fu);
      mem.Write16(0x05000004u, 0x03E0u);

      // BG0CNT:
      // - char base block 0 => tileBase=0x06000000
      // - screen base block 31 => mapBase=0x0600F800
      // - size 3 => 64x64 tiles (4 screen blocks, can overflow 64KB)
      const uint16_t bg0cnt = (uint16_t)((0u << 2) | (31u << 8) | (3u << 14));
      mem.Write16(0x04000008u, bg0cnt);

      // Scroll into bottom-right block (tx>=32, ty>=32).
      mem.Write16(0x04000010u, 256u); // BG0HOFS
      mem.Write16(0x04000012u, 256u); // BG0VOFS

      // Tile #0 filled with palette idx 1 (red), tile #1 with idx 2 (green).
      for (uint32_t o = 0; o < 32; o += 2) {
        mem.Write16(0x06000000u + o, 0x1111u);
        mem.Write16(0x06000020u + o, 0x2222u);
      }

      // If NOT wrapped, BG would read map entry at 0x06011000 (beyond 64KB).
      // We intentionally put tile #1 there (green) to catch incorrect behavior.
      mem.Write16(0x06011000u, 0x0001u);

      // With 64KB wrapping, 0x06011000 wraps to 0x06001000.
      // Put tile #0 there (red) as the expected correct entry.
      mem.Write16(0x06001000u, 0x0000u);

      PPU ppu(mem);
      ppu.Update(TestUtil::kCyclesToHBlankStart);
      ppu.SwapBuffers();
      EXPECT_EQ(TestUtil::GetPixel(ppu, 0, 0), TestUtil::ARGBFromBGR555(0x001F));
    }
    ```

    ---

    ## Step 3 — Implementation

    ### 3.1 Remove Classic NES tile-base heuristic; implement BG 64KB wrapping

    **File:** `src/emulator/gba/PPU.cpp`

    **Operation A:** `ADD` BG-specific VRAM readers next to the existing `ReadVram8/ReadVram16` helpers.

    ```cpp
    inline uint32_t MapBgVramOffset(uint32_t offset) {
      // Text BG fetches wrap within 64KB BG VRAM window.
      return offset & 0xFFFFu;
    }

    inline uint8_t ReadBgVram8(const uint8_t *vram, size_t vramSize,
                               uint32_t offset) {
      if (!vram || vramSize == 0)
        return 0;
      const uint32_t mapped = MapBgVramOffset(offset) % (uint32_t)vramSize;
      return vram[mapped];
    }

    inline uint16_t ReadBgVram16(const uint8_t *vram, size_t vramSize,
                                 uint32_t offset) {
      if (!vram || vramSize == 0)
        return 0;
      const uint32_t o0 = MapBgVramOffset(offset) % (uint32_t)vramSize;
      const uint32_t o1 = MapBgVramOffset(offset + 1u) % (uint32_t)vramSize;
      return (uint16_t)(vram[o0] | (vram[o1] << 8));
    }
    ```

    **Operation B:** `UPDATE` `PPU::RenderBackground()` to use `ReadBgVram16/ReadBgVram8` for tilemap entries and 4bpp/8bpp tile bytes.

    ```cpp
    uint16_t tileEntry = ReadBgVram16(vramData, vramSize, mapOffset);
    ```

    ```cpp
    tileByte = ReadBgVram8(vramData, vramSize, tileOffset);
    ```

    ```cpp
    tileByte = ReadBgVram8(vramData, vramSize, tileOffset);
    ```

    **Operation C:** `DELETE` the `resolveClassicNesTileStartOffset` lambda and the `if (classicNesMode) { ... }` override of tileStartOffset.

    ---

    ## Step 4 — Verify

    1) Build + run unit tests:

    ```bash
    cd "/Users/alexwaldmann/Desktop/AIO Server" && make build
    cd "/Users/alexwaldmann/Desktop/AIO Server/build/generated/cmake" && ctest --output-on-failure
    ```

    2) Headless OG-DK frame dump + ASCII preview (manual sanity):

    ```bash
    cd "/Users/alexwaldmann/Desktop/AIO Server" && rm -f current_ogdk.ppm && \
    ./build/bin/AIOServer --rom OG-DK.gba --headless --headless-max-ms 6000 \
      --headless-dump-ms 3000 --headless-dump-ppm current_ogdk.ppm
    python3 scripts/ppm_ascii_preview.py current_ogdk.ppm --w 120 --h 70 | head -80
    python3 analyze_ppm.py current_ogdk.ppm
    ```

    ---

    ## Handoff

    Run `@Implement` to apply Step 1–4 exactly.

    ````
`````
