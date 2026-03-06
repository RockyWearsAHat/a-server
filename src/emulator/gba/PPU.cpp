#include <algorithm>
#include <atomic>
#include <cstring>
#include <emulator/gba/ARM7TDMI.h>
#include <emulator/gba/GBAMemory.h>
#include <emulator/gba/PPU.h>
#include <mutex>
#include <vector>

namespace AIO::Emulator::GBA {

uint32_t PPU::ApplyBrightnessIncrease(uint32_t colorARGB, int evyRaw) {
  int evy = evyRaw & 0x1F;
  if (evy > 16)
    evy = 16;

  auto to5 = [](uint8_t v8) -> int { return (int)(v8 >> 3); };
  auto from5 = [](int v5) -> uint8_t {
    if (v5 < 0)
      v5 = 0;
    if (v5 > 31)
      v5 = 31;
    return (uint8_t)(v5 << 3);
  };

  uint8_t r = (colorARGB >> 16) & 0xFF;
  uint8_t g = (colorARGB >> 8) & 0xFF;
  uint8_t b = colorARGB & 0xFF;

  uint8_t rr = from5(to5(r) + ((31 - to5(r)) * evy / 16));
  uint8_t gg = from5(to5(g) + ((31 - to5(g)) * evy / 16));
  uint8_t bb = from5(to5(b) + ((31 - to5(b)) * evy / 16));

  return 0xFF000000u | (rr << 16) | (gg << 8) | bb;
}

uint32_t PPU::ApplyBrightnessDecrease(uint32_t colorARGB, int evyRaw) {
  int evy = evyRaw & 0x1F;
  if (evy > 16)
    evy = 16;

  auto to5 = [](uint8_t v8) -> int { return (int)(v8 >> 3); };
  auto from5 = [](int v5) -> uint8_t {
    if (v5 < 0)
      v5 = 0;
    if (v5 > 31)
      v5 = 31;
    return (uint8_t)(v5 << 3);
  };

  uint8_t r = (colorARGB >> 16) & 0xFF;
  uint8_t g = (colorARGB >> 8) & 0xFF;
  uint8_t b = colorARGB & 0xFF;

  uint8_t rr = from5(to5(r) - (to5(r) * evy / 16));
  uint8_t gg = from5(to5(g) - (to5(g) * evy / 16));
  uint8_t bb = from5(to5(b) - (to5(b) * evy / 16));

  return 0xFF000000u | (rr << 16) | (gg << 8) | bb;
}

namespace {
std::atomic<uint64_t> g_ppuInstanceCounter{1};

inline uint64_t NextPpuInstanceId() {
  return g_ppuInstanceCounter.fetch_add(1, std::memory_order_relaxed);
}

inline uint16_t ReadLE16(const uint8_t *data, size_t size, uint32_t offset) {
  if (!data || size == 0)
    return 0;
  offset %= static_cast<uint32_t>(size);
  const uint32_t o1 = (offset + 1) % static_cast<uint32_t>(size);
  return (uint16_t)(data[offset] | (data[o1] << 8));
}

inline uint8_t Read8Wrap(const uint8_t *data, size_t size, uint32_t offset) {
  if (!data || size == 0)
    return 0;
  offset %= static_cast<uint32_t>(size);
  return data[offset];
}

// VRAM has 96KB of real storage (0x00000-0x17FFF) but is addressable as 128KB.
// The upper window (0x18000-0x1FFFF) mirrors to 0x10000-0x17FFF.
inline uint32_t MapVramOffset(uint32_t offset) {
  offset &= 0x1FFFFu;
  if (offset >= 0x18000u) {
    offset -= 0x8000u;
  }
  return offset;
}

inline uint8_t ReadVram8(const uint8_t *vram, size_t vramSize,
                         uint32_t offset) {
  if (!vram || vramSize == 0)
    return 0;
  const uint32_t mapped =
      MapVramOffset(offset) % static_cast<uint32_t>(vramSize);
  return vram[mapped];
}

inline uint16_t ReadVram16(const uint8_t *vram, size_t vramSize,
                           uint32_t offset) {
  if (!vram || vramSize == 0)
    return 0;
  const uint32_t o0 = MapVramOffset(offset) % static_cast<uint32_t>(vramSize);
  const uint32_t o1 =
      MapVramOffset(offset + 1) % static_cast<uint32_t>(vramSize);
  return (uint16_t)(vram[o0] | (vram[o1] << 8));
}

// BG-specific VRAM accessors -------------------------------------------------
// BG fetches for text modes (0-2) are confined to the 64KB BG VRAM window and
// must wrap within that window. Provide helpers that map offsets accordingly so
// PPU BG code can express intent and avoid accidentally sampling OBJ VRAM.
inline uint32_t MapBgVramOffset(uint32_t offset) {
  // Text BG fetches wrap within 64KB BG VRAM window.
  return offset & 0xFFFFu;
}

// Fast BG VRAM accessor — caller guarantees vram is non-null and vramSize >=
// 0x10000 (always true for GBA VRAM). The 16-bit mask in MapBgVramOffset
// guarantees the index stays within 64KB, well under the 96KB allocation.
inline uint8_t ReadBgVram8(const uint8_t *vram, size_t /*vramSize*/,
                           uint32_t offset) {
  return vram[offset & 0xFFFFu];
}

inline uint16_t ReadBgVram16(const uint8_t *vram, size_t /*vramSize*/,
                             uint32_t offset) {
  const uint32_t o0 = offset & 0xFFFFu;
  const uint32_t o1 = (offset + 1u) & 0xFFFFu;
  return static_cast<uint16_t>(vram[o0] | (vram[o1] << 8));
}

// Snapshot helpers for reading VRAM/palette by absolute GBA address.
// These avoid going through the memory bus so rendering reads frame-stable
// snapshot data instead of live memory the CPU may be updating.
inline uint8_t ReadSnapshotVram8(const uint8_t *snap, uint32_t absAddr) {
  uint32_t offset = absAddr & 0x1FFFFu;
  if (offset >= 0x18000u)
    offset -= 0x8000u;
  return snap[offset];
}

inline uint16_t ReadSnapshotVram16(const uint8_t *snap, uint32_t absAddr) {
  uint32_t offset = absAddr & 0x1FFFFu;
  if (offset >= 0x18000u)
    offset -= 0x8000u;
  uint32_t o1 = (offset + 1u);
  if (o1 >= 0x18000u)
    o1 -= 0x8000u;
  return static_cast<uint16_t>(snap[offset] | (snap[o1] << 8));
}

inline uint16_t ReadSnapshotPalette16(const uint8_t *snap, uint32_t absAddr) {
  uint32_t offset = absAddr & 0x3FFu;
  return static_cast<uint16_t>(snap[offset] | (snap[offset + 1u] << 8));
}

inline uint16_t ReadSnapshotOam16(const uint8_t *snap, uint32_t absAddr) {
  uint32_t offset = absAddr & 0x3FFu;
  return static_cast<uint16_t>(snap[offset] | (snap[offset + 1u] << 8));
}

} // namespace

PPU::PPU(GBAMemory &mem)
    : memory(mem), cycleCounter(0), scanline(0), frameCount(0) {
  instanceId = NextPpuInstanceId();
  // Initialize double buffers with black
  backBuffer.resize(SCREEN_WIDTH * SCREEN_HEIGHT, 0xFF000000);
  frontBuffer.resize(SCREEN_WIDTH * SCREEN_HEIGHT, 0xFF000000);

  vramSnapshot.resize(VRAM_SNAPSHOT_SIZE, 0);
  paletteSnapshot.resize(PALETTE_SNAPSHOT_SIZE, 0);
  oamSnapshot.resize(OAM_SNAPSHOT_SIZE, 0);
  // Initialize priority buffer (4 = backdrop, lowest priority)
  priorityBuffer.resize(SCREEN_WIDTH * SCREEN_HEIGHT, 4);

  layerBuffer.resize(SCREEN_WIDTH * SCREEN_HEIGHT, 5);
  underColorBuffer.resize(SCREEN_WIDTH * SCREEN_HEIGHT, 0xFF000000);
  underLayerBuffer.resize(SCREEN_WIDTH * SCREEN_HEIGHT, 5);
  objSemiTransparentBuffer.resize(SCREEN_WIDTH * SCREEN_HEIGHT, 0);
}

PPU::~PPU() = default;

void PPU::Reset() {
  // Reset timing state
  cycleCounter = 0;
  frameCount = 0;

  // Sync PPU scanline with VCOUNT set by memory (HLE BIOS sets 0x7E = 126).
  // Without this, PPU starts at scanline 0 (visible) while VCOUNT says 126
  // (VBlank), causing the game to render offset frames.
  uint16_t vcount = memory.ReadIORegister16Internal(0x06);
  scanline = vcount;
  prevVBlankState = (scanline >= 160 && scanline <= 227);

  // Reset internal affine counters
  bg2x_internal = 0;
  bg2y_internal = 0;
  bg3x_internal = 0;
  bg3y_internal = 0;

  // Clear OBJ window mask
  objWindowMaskLine.fill(0);

  // Reset latched scroll registers
  std::fill(std::begin(latchedBGHOFS), std::end(latchedBGHOFS), 0);
  std::fill(std::begin(latchedBGVOFS), std::end(latchedBGVOFS), 0);
  scrollLatchedForScanline = false;

  // Clear framebuffers to black
  std::fill(backBuffer.begin(), backBuffer.end(), 0xFF000000);
  {
    std::lock_guard<std::mutex> lock(bufferMutex);
    std::fill(frontBuffer.begin(), frontBuffer.end(), 0xFF000000);
  }

  // Reset priority and layer buffers
  std::fill(priorityBuffer.begin(), priorityBuffer.end(), 4);
  std::fill(layerBuffer.begin(), layerBuffer.end(), 5);
  std::fill(underColorBuffer.begin(), underColorBuffer.end(), 0xFF000000);
  std::fill(underLayerBuffer.begin(), underLayerBuffer.end(), 5);
  std::fill(objSemiTransparentBuffer.begin(), objSemiTransparentBuffer.end(),
            0);
}

void PPU::Update(int cycles) {
  // GBA PPU Timing (Simplified for now)
  // 4 cycles per pixel (roughly)
  // 240 pixels + 68 HBlank = 308 pixels per line
  // 308 * 4 = 1232 cycles per line
  // 160 lines + 68 VBlank = 228 lines total

  while (cycles > 0) {
    // Determine how many cycles we can advance in this step
    // We stop at HBlank Start (960) or End of Line (1232)

    int nextEvent = 1232; // Default to end of line
    if (cycleCounter < 960) {
      nextEvent = 960; // Stop at HBlank start
    }

    int cyclesToEvent = nextEvent - cycleCounter;
    int step = std::min(cycles, cyclesToEvent);

    cycleCounter += step;
    cycles -= step;

    // Publish current timing so GBAMemory can enforce timing-dependent access
    // rules.
    memory.SetPpuTimingState(scanline, cycleCounter);

    // Check if we hit an event
    if (cycleCounter == 960) {
      // HBlank Start: render using values set up during previous HBlank
      if (scanline < 160) {
        // Re-snapshot palette/VRAM if anything wrote to them since the last
        // scanline — CPU stores OR immediate DMA (timing=0). Without this,
        // software renderers (e.g. Classic NES Series) that update palette
        // mid-frame via immediate DMA render with stale snapshot data.
        bool palDirty =
            memory.WasPaletteDirtyByCPU() || memory.WasPaletteDirtyByDMA();
        bool vramDirty =
            memory.WasVramDirtyByCPU() || memory.WasVramDirtyByDMA();

        if (palDirty) {
          std::memcpy(paletteSnapshot.data(), memory.GetPaletteData(),
                      PALETTE_SNAPSHOT_SIZE);
        }
        if (vramDirty) {
          std::memcpy(vramSnapshot.data(), memory.GetVRAMData(),
                      VRAM_SNAPSHOT_SIZE);
        }
        if (memory.WasOamDirtyByDMA() || memory.WasOamDirtyByCPU()) {
          std::memcpy(oamSnapshot.data(), memory.GetOAMData(),
                      OAM_SNAPSHOT_SIZE);
        }
        memory.ClearCPUGraphicsDirtyFlags();
        memory.ClearGraphicsDMADirtyFlags();

        DrawScanline();
      }

      // Hardware enters HBlank on every scanline (including VBlank).
      // Set DISPSTAT first so any HBlank-triggered work sees the flag.
      uint16_t dispstat = memory.ReadIORegister16Internal(0x04);
      dispstat |= 2; // HBlank flag (Bit 1)
      memory.WriteIORegisterInternal(0x04, dispstat);

      // Trigger HBlank IRQ if enabled in DISPSTAT.
      if (dispstat & 0x10) { // HBlank IRQ Enable (Bit 4)
        uint16_t if_reg = memory.ReadIORegister16Internal(0x202);
        if_reg |= 2; // HBlank IRQ bit (Bit 1)
        memory.WriteIORegisterInternal(0x202, if_reg);
      }

      // HBlank DMA fires only during visible scanlines (GBATEK: "paused
      // during V-Blank"). HBlank flag/IRQ still fire every scanline above.
      if (scanline < 160) {
        memory.CheckDMA(2);

        // HBlank DMA may have written to OAM, VRAM, or Palette.
        // Selectively re-snapshot only the dirty regions so the next
        // scanline's DrawScanline() sees updated data. Without this,
        // games using HBlank DMA for sprite multiplexing or raster
        // effects (e.g. DKC) render with stale frame-start data.
        RefreshDirtySnapshots();
      }
    } else if (cycleCounter >= 1232) {
      // End of Line
      cycleCounter = 0;

      memory.SetPpuTimingState(scanline, cycleCounter);

      bool triggerVBlankDMA = false;

      // Clear DISPSTAT HBlank Flag
      uint16_t dispstat = memory.ReadIORegister16Internal(0x04);
      dispstat &= ~2;
      memory.WriteIORegisterInternal(0x04, dispstat);

      scanline++;
      if (scanline >= 228) {
        scanline = 0;
        frameCount++;

        SwapBuffers();
      }

      // Latch BG scroll registers at the start of each visible scanline.
      // The game may update scroll during the previous scanline's visible
      // period. By latching at cycle 0 of the NEW scanline, we capture
      // the value after the game's writes have taken effect, preventing
      // CPU batch-timing jitter from affecting which value DrawScanline sees.
      if (scanline < 160) {
        for (int bg = 0; bg < 4; ++bg) {
          latchedBGHOFS[bg] = ReadRegister(0x10 + bg * 4) & 0x01FF;
          latchedBGVOFS[bg] = ReadRegister(0x12 + bg * 4) & 0x01FF;
        }
        scrollLatchedForScanline = true;
      }

      // Update VCOUNT
      memory.WriteIORegisterInternal(0x06, scanline);

      memory.SetPpuTimingState(scanline, cycleCounter);

      // Update DISPSTAT VBlank flag
      dispstat = memory.ReadIORegister16Internal(0x04);
      bool isVBlank = (scanline >= 160 && scanline <= 227);

      bool wasVBlank = prevVBlankState;
      prevVBlankState = isVBlank;
      if (isVBlank) {
        // Mark that we've entered VBlank

        dispstat |= 1; // Set VBlank
        // std::cout << "[PPU DISPSTAT VBlank=1] Scanline=" << scanline << "
        // Frame=" << frameCount << std::endl;

        // Make VBlank visible immediately on entry.
        memory.WriteIORegisterInternal(0x04, dispstat);

        // Trigger VBlank IRQ on rising edge
        if (!wasVBlank) {

          // Latch BGxX/BGxY to internal registers at VBlank start
          // BG2 reference point (0x04000028-0x0400002F)
          uint32_t bg2x_l = ReadRegister(0x28);
          uint32_t bg2x_h = ReadRegister(0x2A);
          bg2x_internal = (bg2x_h << 16) | bg2x_l;
          if (bg2x_internal & 0x08000000)
            bg2x_internal |= 0xF0000000; // Sign extend

          uint32_t bg2y_l = ReadRegister(0x2C);
          uint32_t bg2y_h = ReadRegister(0x2E);
          bg2y_internal = (bg2y_h << 16) | bg2y_l;
          if (bg2y_internal & 0x08000000)
            bg2y_internal |= 0xF0000000;

          // BG3 reference point (0x04000038-0x0400003F)
          uint32_t bg3x_l = ReadRegister(0x38);
          uint32_t bg3x_h = ReadRegister(0x3A);
          bg3x_internal = (bg3x_h << 16) | bg3x_l;
          if (bg3x_internal & 0x08000000)
            bg3x_internal |= 0xF0000000;

          uint32_t bg3y_l = ReadRegister(0x3C);
          uint32_t bg3y_h = ReadRegister(0x3E);
          bg3y_internal = (bg3y_h << 16) | bg3y_l;
          if (bg3y_internal & 0x08000000)
            bg3y_internal |= 0xF0000000;

          if (dispstat & 0x8) { // VBlank IRQ Enable
            uint16_t if_reg = memory.ReadIORegister16Internal(0x202) | 1;
            memory.WriteIORegisterInternal(0x202, if_reg);
          }

          // Defer VBlank DMA until after all end-of-line bookkeeping
          // (VCOUNT/VCOUNT-match/DISPSTAT) is committed.
          triggerVBlankDMA = true;
        }
      } else {
        dispstat &= ~1; // Clear VBlank
        if (wasVBlank) {
          // std::cout << "[PPU DISPSTAT VBlank=0] Scanline=" << scanline << "
          // Frame=" << frameCount << std::endl;
        }
      }

      // V-Counter Match
      uint16_t vcountSetting = (dispstat >> 8) & 0xFF;
      if (scanline == vcountSetting) {
        dispstat |= 4;
        if (dispstat & 0x20) { // VCount IRQ Enable
          uint16_t if_reg = memory.ReadIORegister16Internal(0x202);
          if_reg |= 4;
          memory.WriteIORegisterInternal(0x202, if_reg);
        }
      } else {
        dispstat &= ~4;
      }
      memory.WriteIORegisterInternal(0x04, dispstat);

      // Trigger VBlank DMA after we have committed the scanline state.
      if (triggerVBlankDMA) {
        memory.CheckDMA(1);
      }
    }
  }
}

void PPU::DrawScanline() {
  if (scanline == 0) {
    SnapshotGraphicsMemory();
  }

  // Fallback: latch BG scroll registers if VBlank latching in Update()
  // hasn't run yet (first frame, or direct DrawScanline calls in tests).
  // In normal operation, scrollLatchedForScanline is true from the previous
  // frame's VBlank and the latched values remain stable for the entire frame.
  if (!scrollLatchedForScanline) {
    for (int bg = 0; bg < 4; ++bg) {
      latchedBGHOFS[bg] = ReadRegister(0x10 + bg * 4) & 0x01FF;
      latchedBGVOFS[bg] = ReadRegister(0x12 + bg * 4) & 0x01FF;
    }
  }

  uint16_t dispcnt = ReadRegister(0x00);

  // GBATEK: When forced blank is set (bit 7), the display shows white.
  // No BGs or OBJs are rendered.
  if (dispcnt & 0x0080u) {
    for (int x = 0; x < SCREEN_WIDTH; ++x) {
      backBuffer[scanline * SCREEN_WIDTH + x] = 0xFFFFFFFFu; // White
    }
    return;
  }

  int mode = dispcnt & 0x7;

  BuildObjWindowMaskForScanline();

  // Fetch Backdrop Color (Palette Index 0)
  uint16_t backdropColor =
      ReadSnapshotPalette16(paletteSnapshot.data(), 0x05000000);
  uint8_t r = (backdropColor & 0x1F) << 3;
  uint8_t g = ((backdropColor >> 5) & 0x1F) << 3;
  uint8_t b = ((backdropColor >> 10) & 0x1F) << 3;
  uint32_t backdropARGB = 0xFF000000 | (r << 16) | (g << 8) | b;

  // Clear line with backdrop color
  std::fill(backBuffer.begin() + scanline * SCREEN_WIDTH,
            backBuffer.begin() + (scanline + 1) * SCREEN_WIDTH, backdropARGB);

  // Initialize layer buffers for this scanline
  std::fill(layerBuffer.begin() + scanline * SCREEN_WIDTH,
            layerBuffer.begin() + (scanline + 1) * SCREEN_WIDTH, (uint8_t)5);
  std::fill(underColorBuffer.begin() + scanline * SCREEN_WIDTH,
            underColorBuffer.begin() + (scanline + 1) * SCREEN_WIDTH,
            backdropARGB);
  std::fill(underLayerBuffer.begin() + scanline * SCREEN_WIDTH,
            underLayerBuffer.begin() + (scanline + 1) * SCREEN_WIDTH,
            (uint8_t)5);
  std::fill(objSemiTransparentBuffer.begin() + scanline * SCREEN_WIDTH,
            objSemiTransparentBuffer.begin() + (scanline + 1) * SCREEN_WIDTH,
            (uint8_t)0);

  // Reset priority buffer for this scanline (4 = backdrop, lowest priority)
  std::fill(priorityBuffer.begin() + scanline * SCREEN_WIDTH,
            priorityBuffer.begin() + (scanline + 1) * SCREEN_WIDTH, (uint8_t)4);

  bool objEnabled = (dispcnt & 0x1000) != 0;

  // Pre-compute OBJ rendering budget so mode renderers can interleave
  // OBJ rendering with BG rendering per priority level
  if (objEnabled) {
    ComputeOBJBudget();
  }

  if (mode == 0) {
    RenderMode0(objEnabled);
  } else if (mode == 1) {
    RenderMode1(objEnabled);
  } else if (mode == 2) {
    RenderMode2(objEnabled);
  } else if (mode == 3) {
    RenderMode3();
    if (objEnabled)
      RenderOBJ();
  } else if (mode == 4) {
    RenderMode4();
    if (objEnabled)
      RenderOBJ();
  } else if (mode == 5) {
    RenderMode5();
    if (objEnabled)
      RenderOBJ();
  }

  // Apply Color Special Effects (Blending/Brightness)
  ApplyColorEffects();
}

void PPU::RenderOBJ() {
  ComputeOBJBudget();
  for (int p = 3; p >= 0; --p) {
    RenderOBJAtPriority(p);
  }
}

void PPU::ComputeOBJBudget() {
  // GBA hardware OBJ rendering budget: 1210 dots available per scanline.
  // When HBlank Interval Free (DISPCNT bit 5) is set, the budget drops
  // to 954 dots because OAM access during HBlank steals rendering time.
  // Each non-affine pixel costs 1 dot, each affine pixel costs 2 dots.
  // Sprites are evaluated in OAM order (0 first); once the budget is
  // exhausted, remaining sprites on this scanline are not rendered.
  const uint16_t dispcntForBudget = ReadRegister(0x00);
  const bool hblankIntervalFree = ((dispcntForBudget >> 5) & 1) != 0;
  const int maxObjDots = hblankIntervalFree ? 954 : 1210;

  int objDotsUsed = 0;
  objRenderable.fill(0);

  const uint8_t *oamData = oamSnapshot.data();
  const size_t oamSize = OAM_SNAPSHOT_SIZE;

  static const int sizes[3][4][2] = {{{8, 8}, {16, 16}, {32, 32}, {64, 64}},
                                     {{16, 8}, {32, 8}, {32, 16}, {64, 32}},
                                     {{8, 16}, {8, 32}, {16, 32}, {32, 64}}};

  for (int i = 0; i < 128; ++i) {
    const uint32_t oamOff = static_cast<uint32_t>(i * 8);
    uint16_t a0 = ReadLE16(oamData, oamSize, oamOff);
    uint16_t a1 = ReadLE16(oamData, oamSize, oamOff + 2);

    const bool affine = ((a0 >> 8) & 1) != 0;
    const bool dblOrDis = ((a0 >> 9) & 1) != 0;
    const uint8_t mode = (a0 >> 10) & 0x3;
    if (!affine && dblOrDis)
      continue;
    if (mode == 3)
      continue;

    int y = a0 & 0xFF;
    int shape = (a0 >> 14) & 0x3;
    if (shape == 3)
      continue;
    int sz = (a1 >> 14) & 0x3;
    int w = sizes[shape][sz][0];
    int h = sizes[shape][sz][1];
    int bw = (affine && dblOrDis) ? w * 2 : w;
    int bh = (affine && dblOrDis) ? h * 2 : h;

    if (y + bh > 256)
      y -= 256;

    if (scanline < y || scanline >= y + bh)
      continue;

    int dotsPerPixel = affine ? 2 : 1;
    int dotCost = bw * dotsPerPixel;
    objDotsUsed += dotCost;
    if (objDotsUsed <= maxObjDots) {
      objRenderable[i] = 1;
    }
  }
}

void PPU::RenderOBJAtPriority(int targetPriority) {
  const uint8_t *oamData = oamSnapshot.data();
  const size_t oamSize = OAM_SNAPSHOT_SIZE;

  // Render in reverse OAM order (127→0) so lower-index
  // sprites overwrite higher-index ones (correct priority).
  for (int i = 127; i >= 0; --i) {
    if (!objRenderable[i])
      continue;

    const uint32_t oamOff = static_cast<uint32_t>(i * 8);
    uint16_t attr2 = ReadLE16(oamData, oamSize, oamOff + 4);

    // Skip sprites not at the target priority level
    int priority = (attr2 >> 10) & 0x3;
    if (priority != targetPriority)
      continue;

    uint16_t attr0 = ReadLE16(oamData, oamSize, oamOff);
    uint16_t attr1 = ReadLE16(oamData, oamSize, oamOff + 2);

    // Check Y Coordinate
    int y = attr0 & 0xFF;

    const bool affine = ((attr0 >> 8) & 1) != 0;
    const bool doubleSizeOrDisable = ((attr0 >> 9) & 1) != 0;
    const uint8_t objMode =
        (attr0 >> 10) &
        0x3; // 0=Normal 1=Semi-Transparent 2=OBJ Window 3=Prohibited

    if (!affine && doubleSizeOrDisable) {
      continue; // OBJ disabled
    }
    if (objMode == 3) {
      continue;
    }
    if (objMode == 2) {
      continue; // OBJ window sprites define masks only (handled in
                // BuildObjWindowMaskForScanline)
    }

    bool isAffine = affine;
    bool isDoubleSize = affine && doubleSizeOrDisable;

    // Shape (0=Square, 1=Horizontal, 2=Vertical)
    int shape = (attr0 >> 14) & 0x3;
    int size = (attr1 >> 14) & 0x3;

    int width = 8, height = 8;
    // Lookup table for size
    static const int sizes[3][4][2] = {
        {{8, 8}, {16, 16}, {32, 32}, {64, 64}}, // Square
        {{16, 8}, {32, 8}, {32, 16}, {64, 32}}, // Horizontal
        {{8, 16}, {8, 32}, {16, 32}, {32, 64}}  // Vertical
    };

    width = sizes[shape][size][0];
    height = sizes[shape][size][1];

    // For double-size affine, the bounding box is doubled
    int boundWidth = isDoubleSize ? width * 2 : width;
    int boundHeight = isDoubleSize ? height * 2 : height;

    // GBA Y-coords are 8-bit unsigned; large sprites near Y=255 wrap into
    // negative territory to appear at the top of the screen.
    if (y + boundHeight > 256)
      y -= 256;

    // Check if scanline is within sprite bounds
    if (scanline >= y && scanline < y + boundHeight) {
      // Render this line of the sprite
      int x = attr1 & 0x1FF;

      if (x >= 256)
        x -= 512; // Sign extend 9-bit X

      int tileIndex = attr2 & 0x3FF;
      int paletteBank = (attr2 >> 12) & 0xF;
      bool is8bpp = (attr0 >> 13) & 1;
      const bool mosaicEnable = ((attr0 >> 12) & 1) != 0;

      int mosaicH = 1;
      int mosaicV = 1;
      if (mosaicEnable) {
        const uint16_t mosaic = ReadRegister(0x4C);
        mosaicH = ((mosaic >> 8) & 0xF) + 1;
        mosaicV = ((mosaic >> 12) & 0xF) + 1;
      }

      // In 8bpp OBJ mode, the tile index's LSB is ignored on real hardware
      // (tiles are 64 bytes = two 32-byte blocks).
      if (is8bpp) {
        tileIndex &= ~1;
      }

      // Flip flags only apply to non-affine sprites
      bool hFlip = !isAffine && ((attr1 >> 12) & 1);
      bool vFlip = !isAffine && ((attr1 >> 13) & 1);

      // Affine parameters
      int16_t pa = 0x100, pb = 0, pc = 0,
              pd = 0x100; // Identity matrix (1.0 in 8.8 fixed point)
      if (isAffine) {
        // Get affine parameter group index from bits 9-13 of attr1
        int affineIndex = (attr1 >> 9) & 0x1F;
        // Each affine parameter group is 32 bytes apart in OAM
        // Parameters are at offsets 6, 14, 22, 30 within each 32-byte block
        uint32_t affineBase = 0x07000006 + (affineIndex * 32);
        pa = (int16_t)ReadSnapshotOam16(oamSnapshot.data(), affineBase);
        pb = (int16_t)ReadSnapshotOam16(oamSnapshot.data(), affineBase + 8);
        pc = (int16_t)ReadSnapshotOam16(oamSnapshot.data(), affineBase + 16);
        pd = (int16_t)ReadSnapshotOam16(oamSnapshot.data(), affineBase + 24);
      }

      // Center of the sprite in sprite coordinates
      int centerX = width / 2;
      int centerY = height / 2;

      // Tile Base for OBJ is 0x06010000 (Char Block 4)
      uint16_t dispcnt = ReadRegister(0x00);
      bool mapping1D = (dispcnt >> 6) & 1;
      uint32_t tileBase = 0x06010000;

      for (int sx = 0; sx < boundWidth; ++sx) {
        int screenX = x + sx;
        if (screenX < 0 || screenX >= SCREEN_WIDTH)
          continue;

        int spriteX, spriteY;

        int sampleSX = sx;
        int sampleLine = scanline - y;
        if (mosaicEnable) {
          sampleSX -= (sampleSX % mosaicH);
          sampleLine -= (sampleLine % mosaicV);
        }

        if (isAffine) {
          // Calculate texture coordinates using inverse affine transformation
          // Screen position relative to center of bounds
          int px = sampleSX - boundWidth / 2;
          int py = sampleLine - boundHeight / 2;

          // Apply inverse affine matrix (in 8.8 fixed point)
          // texX = pa * px + pb * py + centerX
          // texY = pc * px + pd * py + centerY
          spriteX = ((pa * px + pb * py) >> 8) + centerX;
          spriteY = ((pc * px + pd * py) >> 8) + centerY;

          // Check if we're within the actual sprite bounds
          if (spriteX < 0 || spriteX >= width || spriteY < 0 ||
              spriteY >= height) {
            continue;
          }
        } else {
          // Non-affine sprite
          spriteX = sampleSX;
          int lineInSprite = sampleLine;

          if (hFlip)
            spriteX = width - 1 - sampleSX;
          if (vFlip)
            lineInSprite = height - 1 - lineInSprite;

          spriteY = lineInSprite;
        }

        // Fetch Pixel
        uint8_t colorIndex = 0;
        uint32_t pixelTileAddr = 0;
        uint8_t pixelTileByte = 0;
        int tileNum = 0;

        if (mapping1D) {
          // 1D Mapping
          if (is8bpp) {
            tileNum =
                tileIndex + (spriteY / 8) * (width / 8) * 2 + (spriteX / 8) * 2;
          } else {
            tileNum = tileIndex + (spriteY / 8) * (width / 8) + (spriteX / 8);
          }

          int inTileX = spriteX % 8;
          int inTileY = spriteY % 8;

          if (is8bpp) {
            const uint8_t *vramData = vramSnapshot.data();
            const size_t vramSize = VRAM_SNAPSHOT_SIZE;
            pixelTileAddr = tileBase + (uint32_t)tileNum * 32u +
                            (uint32_t)inTileY * 8u + (uint32_t)inTileX;
            pixelTileByte =
                ReadVram8(vramData, vramSize, pixelTileAddr - 0x06000000u);
            colorIndex = pixelTileByte;
          } else {
            const uint8_t *vramData = vramSnapshot.data();
            const size_t vramSize = VRAM_SNAPSHOT_SIZE;
            pixelTileAddr = tileBase + (uint32_t)tileNum * 32u +
                            (uint32_t)inTileY * 4u + (uint32_t)(inTileX / 2);
            pixelTileByte =
                ReadVram8(vramData, vramSize, pixelTileAddr - 0x06000000u);
            bool useHighNibble = (inTileX & 1) != 0;
            colorIndex = useHighNibble ? ((pixelTileByte >> 4) & 0xF)
                                       : (pixelTileByte & 0xF);
          }
        } else {
          // 2D Mapping
          int tx = spriteX / 8;
          int ty = spriteY / 8;

          if (is8bpp) {
            // 2D mapping arranges sprite tiles in rows of 32 tiles.
            // In 8bpp each tile consumes 2 blocks, so the row stride is 64
            // blocks.
            tileNum = tileIndex + ty * 64 + tx * 2;
          } else {
            tileNum = tileIndex + ty * 32 + tx;
          }

          int inTileX = spriteX % 8;
          int inTileY = spriteY % 8;

          if (is8bpp) {
            const uint8_t *vramData = vramSnapshot.data();
            const size_t vramSize = VRAM_SNAPSHOT_SIZE;
            pixelTileAddr = tileBase + (uint32_t)tileNum * 32u +
                            (uint32_t)inTileY * 8u + (uint32_t)inTileX;
            pixelTileByte =
                ReadVram8(vramData, vramSize, pixelTileAddr - 0x06000000u);
            colorIndex = pixelTileByte;
          } else {
            const uint8_t *vramData = vramSnapshot.data();
            const size_t vramSize = VRAM_SNAPSHOT_SIZE;
            pixelTileAddr = tileBase + (uint32_t)tileNum * 32u +
                            (uint32_t)inTileY * 4u + (uint32_t)(inTileX / 2);
            pixelTileByte =
                ReadVram8(vramData, vramSize, pixelTileAddr - 0x06000000u);
            bool useHighNibble = (inTileX & 1) != 0;
            colorIndex = useHighNibble ? ((pixelTileByte >> 4) & 0xF)
                                       : (pixelTileByte & 0xF);
          }
        }
        if (colorIndex != 0) {
          // Check if OBJ layer is enabled by window settings at this pixel
          if (!IsLayerEnabledAtPixel(screenX, scanline, 4)) {
            continue; // Window masks OBJ at this position
          }

          // Check OBJ priority against BG priority at this pixel
          // OBJ with priority N is drawn in front of BG with priority N or
          // higher (lower priority) That is, OBJ priority 2 draws in front of
          // BG priority 2, 3 but behind BG priority 0, 1
          int pixelIndex = scanline * SCREEN_WIDTH + screenX;

          // Only draw if sprite priority <= existing pixel priority
          // (lower number = higher display priority)
          if (priority <= priorityBuffer[pixelIndex]) {
            uint8_t effectiveColorIndex = colorIndex;
            uint8_t effectivePaletteBank = paletteBank;

            // Fetch Color (OBJ Palette starts at 0x05000200)
            uint32_t paletteAddr = 0x05000200;
            if (is8bpp) {
              paletteAddr += colorIndex * 2;
            } else {
              paletteAddr +=
                  (effectivePaletteBank * 32) + (effectiveColorIndex * 2);
            }

            const uint8_t *palData = paletteSnapshot.data();
            const size_t palSize = PALETTE_SNAPSHOT_SIZE;
            uint16_t color =
                ReadLE16(palData, palSize, paletteAddr - 0x05000000u);

            uint8_t r = (color & 0x1F) << 3;
            uint8_t g = ((color >> 5) & 0x1F) << 3;
            uint8_t b = ((color >> 10) & 0x1F) << 3;

            underColorBuffer[pixelIndex] = backBuffer[pixelIndex];
            underLayerBuffer[pixelIndex] = layerBuffer[pixelIndex];
            backBuffer[pixelIndex] = 0xFF000000 | (r << 16) | (g << 8) | b;
            layerBuffer[pixelIndex] = 4;
            objSemiTransparentBuffer[pixelIndex] = (objMode == 1) ? 1 : 0;
            // Update priority buffer (OBJ takes this priority slot)
            priorityBuffer[pixelIndex] = (uint8_t)priority;
          }
        }
      }
    }
  }
}

void PPU::RenderMode2(bool objEnabled) {
  // Mode 2: Affine, BG2 and BG3
  uint16_t dispcnt = ReadRegister(0x00);

  bool bg2Enabled = (dispcnt & 0x0400) != 0;
  bool bg3Enabled = (dispcnt & 0x0800) != 0;

  int bg2Priority = bg2Enabled ? (ReadRegister(0x0C) & 0x3) : 99;
  int bg3Priority = bg3Enabled ? (ReadRegister(0x0E) & 0x3) : 99;

  // Render per priority level (3→0), interleaving BGs and OBJs
  for (int p = 3; p >= 0; --p) {
    // BG3 first (higher index), then BG2
    if (bg3Enabled && bg3Priority == p)
      RenderAffineBackground(3);
    if (bg2Enabled && bg2Priority == p)
      RenderAffineBackground(2);
    if (objEnabled)
      RenderOBJAtPriority(p);
  }
}

void PPU::RenderAffineBackground(int bgIndex) {
  // bgIndex is 2 or 3
  uint16_t bgcnt = ReadRegister(0x08 + (bgIndex * 2));

  // Get background priority (0 = highest, 3 = lowest)
  int bgPriority = bgcnt & 0x3;

  // Affine Parameters
  // BG2: 0x20-0x2F, BG3: 0x30-0x3F
  uint32_t paramBase = 0x20 + (bgIndex - 2) * 0x10;

  int16_t pa = (int16_t)ReadRegister(paramBase + 0x00);
  int16_t pb = (int16_t)ReadRegister(paramBase + 0x02);
  int16_t pc = (int16_t)ReadRegister(paramBase + 0x04);
  int16_t pd = (int16_t)ReadRegister(paramBase + 0x06);

  // Use internal reference point registers (properly latched at VBlank)
  int32_t *bgx_int_ptr;
  int32_t *bgy_int_ptr;
  if (bgIndex == 2) {
    bgx_int_ptr = &bg2x_internal;
    bgy_int_ptr = &bg2y_internal;
  } else {
    bgx_int_ptr = &bg3x_internal;
    bgy_int_ptr = &bg3y_internal;
  }

  // Get current internal reference point for this scanline
  int32_t cx = *bgx_int_ptr;
  int32_t cy = *bgy_int_ptr;

  const bool mosaicEnable = ((bgcnt >> 6) & 1) != 0;
  int mosaicH = 1;
  int mosaicV = 1;
  if (mosaicEnable) {
    const uint16_t mosaic = ReadRegister(0x4C);
    mosaicH = (mosaic & 0xF) + 1;
    mosaicV = ((mosaic >> 4) & 0xF) + 1;
  }

  // Screen Size (0-3)
  int screenSize = (bgcnt >> 14) & 0x3;
  int sizeShift = 7 + screenSize;
  int sizeMask = (128 << screenSize) - 1;

  int screenBaseBlock = (bgcnt >> 8) & 0x1F;
  int charBaseBlock = (bgcnt >> 2) & 0x3;

  uint32_t vramBase = 0x06000000;
  uint32_t mapBase = vramBase + (screenBaseBlock * 2048);
  uint32_t tileBase = vramBase + (charBaseBlock * 16384);

  // Affine backgrounds always use 8bpp (256 colors)
  // Palette starts at 0x05000000 (256 colors * 2 bytes = 512 bytes)

  const bool overflowWrap = ((bgcnt >> 13) & 1) != 0;

  if (!mosaicEnable) {
    for (int x = 0; x < SCREEN_WIDTH; ++x) {
      // Convert fixed point (24.8) to integer
      int tx = cx >> 8;
      int ty = cy >> 8;

      if (overflowWrap ||
          (tx >= 0 && tx <= sizeMask && ty >= 0 && ty <= sizeMask)) {
        // Wrap coordinates
        int mapX = tx & sizeMask;
        int mapY = ty & sizeMask;

        // Fetch Tile Index from Map
        // Map is flat array of bytes
        int tileMapWidth = 16 << screenSize; // 16, 32, 64, 128 tiles
        int tileX = mapX / 8;
        int tileY = mapY / 8;

        uint32_t mapAddr = mapBase + (tileY * tileMapWidth) + tileX;
        uint8_t tileIndex = ReadSnapshotVram8(vramSnapshot.data(), mapAddr);

        // Fetch Pixel from Tile
        // 8bpp tiles are 64 bytes
        int inTileX = mapX % 8;
        int inTileY = mapY % 8;

        uint32_t tileAddr =
            tileBase + (tileIndex * 64) + (inTileY * 8) + inTileX;
        uint8_t colorIndex = ReadSnapshotVram8(vramSnapshot.data(), tileAddr);

        if (colorIndex != 0) {
          // Check if this BG layer is enabled by window settings at this pixel
          if (!IsLayerEnabledAtPixel(x, scanline, bgIndex)) {
            cx += pa;
            cy += pc;
            continue;
          }

          int pixelIndex = scanline * SCREEN_WIDTH + x;
          if (bgPriority <= priorityBuffer[pixelIndex]) {
            uint32_t paletteAddr = 0x05000000 + (colorIndex * 2);
            uint16_t color =
                ReadSnapshotPalette16(paletteSnapshot.data(), paletteAddr);

            uint8_t r = (color & 0x1F) << 3;
            uint8_t g = ((color >> 5) & 0x1F) << 3;
            uint8_t b = ((color >> 10) & 0x1F) << 3;

            underColorBuffer[pixelIndex] = backBuffer[pixelIndex];
            underLayerBuffer[pixelIndex] = layerBuffer[pixelIndex];
            backBuffer[pixelIndex] = 0xFF000000 | (r << 16) | (g << 8) | b;
            layerBuffer[pixelIndex] = (uint8_t)bgIndex;
            objSemiTransparentBuffer[pixelIndex] = 0;
            priorityBuffer[pixelIndex] = (uint8_t)bgPriority;
          }
        }
      }

      // Increment position for next pixel
      cx += pa;
      cy += pc;
    }
  } else {
    int32_t startCx = cx;
    int32_t startCy = cy;

    const int baseY = scanline - (scanline % mosaicV);
    const int deltaLines = scanline - baseY;
    startCx -= (int32_t)deltaLines * (int32_t)pb;
    startCy -= (int32_t)deltaLines * (int32_t)pd;

    for (int x = 0; x < SCREEN_WIDTH; ++x) {
      const int mosaicX = x - (x % mosaicH);
      const int32_t pxCx = startCx + (int32_t)mosaicX * (int32_t)pa;
      const int32_t pxCy = startCy + (int32_t)mosaicX * (int32_t)pc;

      const int tx = (int)(pxCx >> 8);
      const int ty = (int)(pxCy >> 8);

      if (overflowWrap ||
          (tx >= 0 && tx <= sizeMask && ty >= 0 && ty <= sizeMask)) {
        const int mapX = tx & sizeMask;
        const int mapY = ty & sizeMask;

        const int tileMapWidth = 16 << screenSize;
        const int tileX = mapX / 8;
        const int tileY = mapY / 8;

        const uint32_t mapAddr =
            mapBase + (uint32_t)(tileY * tileMapWidth) + (uint32_t)tileX;
        const uint8_t tileIndex =
            ReadSnapshotVram8(vramSnapshot.data(), mapAddr);

        const int inTileX = mapX % 8;
        const int inTileY = mapY % 8;

        const uint32_t tileAddr = tileBase + (uint32_t)(tileIndex * 64) +
                                  (uint32_t)(inTileY * 8) + (uint32_t)inTileX;
        const uint8_t colorIndex =
            ReadSnapshotVram8(vramSnapshot.data(), tileAddr);

        if (colorIndex != 0) {
          if (!IsLayerEnabledAtPixel(x, scanline, bgIndex)) {
            continue;
          }

          const int pixelIndex = scanline * SCREEN_WIDTH + x;
          if (bgPriority <= priorityBuffer[pixelIndex]) {
            const uint32_t paletteAddr =
                0x05000000u + (uint32_t)colorIndex * 2u;
            const uint16_t color =
                ReadSnapshotPalette16(paletteSnapshot.data(), paletteAddr);

            const uint8_t r = (color & 0x1F) << 3;
            const uint8_t g = ((color >> 5) & 0x1F) << 3;
            const uint8_t b = ((color >> 10) & 0x1F) << 3;

            underColorBuffer[pixelIndex] = backBuffer[pixelIndex];
            underLayerBuffer[pixelIndex] = layerBuffer[pixelIndex];
            backBuffer[pixelIndex] = 0xFF000000 | (r << 16) | (g << 8) | b;
            layerBuffer[pixelIndex] = (uint8_t)bgIndex;
            objSemiTransparentBuffer[pixelIndex] = 0;
            priorityBuffer[pixelIndex] = (uint8_t)bgPriority;
          }
        }
      }
    }
  }

  // Update internal reference point for next scanline
  // Add pb and pd to the internal registers (not pa and pc!)
  *bgx_int_ptr += pb;
  *bgy_int_ptr += pd;
}

void PPU::RenderMode3() {
  // Mode 3: 240x160 16bpp Bitmap (direct color, no palette)
  // Frame buffer at 0x06000000
  // Each pixel is 2 bytes (BGR555)

  uint32_t vramBase = 0x06000000;

  const int bgPriority = ReadRegister(0x0C) & 0x3; // BG2 priority

  for (int x = 0; x < SCREEN_WIDTH; ++x) {
    if (!IsLayerEnabledAtPixel(x, scanline, 2)) {
      continue;
    }
    uint32_t addr = vramBase + (scanline * SCREEN_WIDTH + x) * 2;
    uint16_t color = ReadSnapshotVram16(vramSnapshot.data(), addr);

    uint8_t r = (color & 0x1F) << 3;
    uint8_t g = ((color >> 5) & 0x1F) << 3;
    uint8_t b = ((color >> 10) & 0x1F) << 3;

    const int pixelIndex = scanline * SCREEN_WIDTH + x;
    if (bgPriority <= priorityBuffer[pixelIndex]) {
      underColorBuffer[pixelIndex] = backBuffer[pixelIndex];
      underLayerBuffer[pixelIndex] = layerBuffer[pixelIndex];
      backBuffer[pixelIndex] = 0xFF000000 | (r << 16) | (g << 8) | b;
      layerBuffer[pixelIndex] = 2;
      objSemiTransparentBuffer[pixelIndex] = 0;
      priorityBuffer[pixelIndex] = (uint8_t)bgPriority;
    }
  }
}

void PPU::RenderMode4() {
  // Mode 4: 240x160 8bpp Indexed Bitmap (palette lookup)
  // Frame buffer at 0x06000000 (or 0x0600A000 for page 2)
  // Each pixel is 1 byte (palette index)
  // Palette at 0x05000000

  uint16_t dispcnt = ReadRegister(0x00);
  bool page1 = (dispcnt >> 4) & 1; // Bit 4 = Display Frame Select

  uint32_t vramBase = page1 ? 0x0600A000 : 0x06000000;

  const int bgPriority = ReadRegister(0x0C) & 0x3; // BG2 priority

  for (int x = 0; x < SCREEN_WIDTH; ++x) {
    if (!IsLayerEnabledAtPixel(x, scanline, 2)) {
      continue;
    }
    uint32_t addr = vramBase + scanline * SCREEN_WIDTH + x;
    uint8_t colorIndex = ReadSnapshotVram8(vramSnapshot.data(), addr);

    // Bitmap modes do not have per-pixel transparency. Palette index 0 is a
    // valid color.
    const uint32_t paletteAddr = 0x05000000 + (uint32_t)colorIndex * 2u;
    const uint16_t color =
        ReadSnapshotPalette16(paletteSnapshot.data(), paletteAddr);

    const uint8_t r = (color & 0x1F) << 3;
    const uint8_t g = ((color >> 5) & 0x1F) << 3;
    const uint8_t b = ((color >> 10) & 0x1F) << 3;

    const int pixelIndex = scanline * SCREEN_WIDTH + x;
    if (bgPriority <= priorityBuffer[pixelIndex]) {
      underColorBuffer[pixelIndex] = backBuffer[pixelIndex];
      underLayerBuffer[pixelIndex] = layerBuffer[pixelIndex];
      backBuffer[pixelIndex] = 0xFF000000 | (r << 16) | (g << 8) | b;
      layerBuffer[pixelIndex] = 2;
      objSemiTransparentBuffer[pixelIndex] = 0;
      priorityBuffer[pixelIndex] = (uint8_t)bgPriority;
    }
  }
}

void PPU::RenderMode5() {
  // Mode 5: 160x128 16bpp Bitmap (direct color), double buffered
  // Smaller resolution centered on screen
  // Frame buffer at 0x06000000 (or 0x0600A000 for page 2)
  // Each pixel is 2 bytes (BGR555)

  uint16_t dispcnt = ReadRegister(0x00);
  bool page1 = (dispcnt >> 4) & 1; // Bit 4 = Display Frame Select

  uint32_t vramBase = page1 ? 0x0600A000 : 0x06000000;

  // Mode 5 is 160x128, centered would start at (40, 16) on 240x160 screen
  // But games typically handle positioning themselves
  // We render the 160x128 area into the top-left for simplicity
  // (Games may use affine to position it)

  const int MODE5_WIDTH = 160;
  const int MODE5_HEIGHT = 128;

  const int bgPriority = ReadRegister(0x0C) & 0x3; // BG2 priority

  if (scanline < MODE5_HEIGHT) {
    for (int x = 0; x < MODE5_WIDTH && x < SCREEN_WIDTH; ++x) {
      if (!IsLayerEnabledAtPixel(x, scanline, 2)) {
        continue;
      }
      uint32_t addr = vramBase + (scanline * MODE5_WIDTH + x) * 2;
      uint16_t color = ReadSnapshotVram16(vramSnapshot.data(), addr);

      uint8_t r = (color & 0x1F) << 3;
      uint8_t g = ((color >> 5) & 0x1F) << 3;
      uint8_t b = ((color >> 10) & 0x1F) << 3;

      const int pixelIndex = scanline * SCREEN_WIDTH + x;
      if (bgPriority <= priorityBuffer[pixelIndex]) {
        underColorBuffer[pixelIndex] = backBuffer[pixelIndex];
        underLayerBuffer[pixelIndex] = layerBuffer[pixelIndex];
        backBuffer[pixelIndex] = 0xFF000000 | (r << 16) | (g << 8) | b;
        layerBuffer[pixelIndex] = 2;
        objSemiTransparentBuffer[pixelIndex] = 0;
        priorityBuffer[pixelIndex] = (uint8_t)bgPriority;
      }
    }
  }
  // Scanlines >= 128 will just show backdrop
}

void PPU::RenderMode1(bool objEnabled) {
  // Mode 1: Mixed Tiled mode
  // BG0, BG1 = Regular tiled (text mode)
  // BG2 = Affine/Rotation-Scaling
  // BG3 = Not available in Mode 1

  uint16_t dispcnt = ReadRegister(0x00);

  struct BGInfo {
    int index;
    int priority;
    bool enabled;
    bool affine;
  };
  BGInfo bgs[3];

  bgs[0] = {0, 0, (dispcnt & 0x0100) != 0, false};
  bgs[1] = {1, 0, (dispcnt & 0x0200) != 0, false};
  bgs[2] = {2, 0, (dispcnt & 0x0400) != 0, true};

  for (int i = 0; i < 3; ++i) {
    if (bgs[i].enabled) {
      uint16_t bgcnt = ReadRegister(0x08 + (bgs[i].index * 2));
      bgs[i].priority = bgcnt & 0x3;
    } else {
      bgs[i].priority = 99;
    }
  }

  // Render per priority level (3→0), interleaving BGs and OBJs
  for (int p = 3; p >= 0; --p) {
    // BG2 first (higher index), then BG1, then BG0
    for (int bgSlot = 2; bgSlot >= 0; --bgSlot) {
      if (bgs[bgSlot].enabled && bgs[bgSlot].priority == p) {
        if (bgs[bgSlot].affine) {
          RenderAffineBackground(bgs[bgSlot].index);
        } else {
          RenderBackground(bgs[bgSlot].index);
        }
      }
    }
    if (objEnabled) {
      RenderOBJAtPriority(p);
    }
  }
}

void PPU::RenderMode0(bool objEnabled) {
  // Mode 0: Tiled, BG0-BG3
  // GBA composites per priority level: for each priority 3→0,
  // render BGs at that level (highest BG index first), then OBJs at that level.
  uint16_t dispcnt = ReadRegister(0x00);

  struct BGInfo {
    int index;
    int priority;
    bool enabled;
  };
  BGInfo bgs[4];

  for (int i = 0; i < 4; ++i) {
    bgs[i].index = i;
    bgs[i].enabled = (dispcnt & (0x100 << i)) != 0;
    if (bgs[i].enabled) {
      uint16_t bgcnt = ReadRegister(0x08 + (i * 2));
      bgs[i].priority = bgcnt & 0x3;
    } else {
      bgs[i].priority = 99;
    }
  }

  // Render per priority level (3→0). Within each level, render BGs with
  // higher index first so lower BG index wins ties, then OBJs at that level.
  for (int p = 3; p >= 0; --p) {
    for (int bgIdx = 3; bgIdx >= 0; --bgIdx) {
      if (bgs[bgIdx].enabled && bgs[bgIdx].priority == p) {
        RenderBackground(bgs[bgIdx].index);
      }
    }
    if (objEnabled) {
      RenderOBJAtPriority(p);
    }
  }
}

void PPU::RenderBackground(int bgIndex) {
  const uint16_t dispcnt = ReadRegister(0x00);
  const uint8_t bgMode = (uint8_t)(dispcnt & 0x7u);

  // NOTE: BG VRAM wrapping (mapOffset &= 0xFFFF) was attempted for modes 0-2
  // but broke SMA2. Removed for compatibility.
  (void)bgMode;

  uint16_t bgcnt = ReadRegister(0x08 + (bgIndex * 2));

  uint16_t bghofs = latchedBGHOFS[bgIndex];
  uint16_t bgvofs = latchedBGVOFS[bgIndex];

  const bool mosaicEnable = ((bgcnt >> 6) & 1) != 0;
  int mosaicH = 1;
  int mosaicV = 1;
  if (mosaicEnable) {
    const uint16_t mosaic = ReadRegister(0x4C);
    mosaicH = (mosaic & 0xF) + 1;
    mosaicV = ((mosaic >> 4) & 0xF) + 1;
  }

  // Get background priority (0 = highest, 3 = lowest)
  int bgPriority = bgcnt & 0x3;

  int charBaseBlock = (bgcnt >> 2) & 0x3;
  int screenBaseBlock = (bgcnt >> 8) & 0x1F;
  bool is8bpp = ((bgcnt >> 7) & 1) != 0;

  // 2: 256x512 (32x64 tiles)
  // 3: 512x512 (64x64 tiles)
  int screenSize = (bgcnt >> 14) & 0x3;
  int mapWidth = (screenSize & 1) ? 64 : 32;
  int mapHeight = (screenSize & 2) ? 64 : 32;

  const int wrapX = (mapWidth * 8) - 1;  // 255 or 511
  const int wrapY = (mapHeight * 8) - 1; // 255 or 511

  uint32_t vramBase = 0x06000000;
  uint32_t mapBase = vramBase + (screenBaseBlock * 2048);
  uint32_t tileBase = vramBase + (charBaseBlock * 16384);

  const uint8_t *vramData = vramSnapshot.data();
  const size_t vramSize = VRAM_SNAPSHOT_SIZE;
  const uint8_t *palData = paletteSnapshot.data();
  const size_t palSize = PALETTE_SNAPSHOT_SIZE;

  for (int x = 0; x < SCREEN_WIDTH; ++x) {
    // Wrap based on the actual BG size (256 or 512). For 256x256 backgrounds,
    // wrapping at 512 would incorrectly access non-existent screen blocks.
    const int baseX = mosaicEnable ? (x - (x % mosaicH)) : x;
    const int baseY =
        mosaicEnable ? (scanline - (scanline % mosaicV)) : scanline;
    int scrolledX = (baseX + bghofs) & wrapX;
    int scrolledY = (baseY + bgvofs) & wrapY;

    // Handle multi-screen-block maps for larger sizes
    // Size 0: 256x256 (1 block, 32x32 tiles)
    // Size 1: 512x256 (2 blocks horizontal, 64x32 tiles)
    // Size 2: 256x512 (2 blocks vertical, 32x64 tiles)
    // Size 3: 512x512 (4 blocks, 64x64 tiles)

    int tx = (scrolledX / 8);
    int ty = (scrolledY / 8);

    // Calculate which screen block we're in
    int blockX = (tx >= 32) ? 1 : 0;
    int blockY = (ty >= 32) ? 1 : 0;

    // Wrap tile coordinates within the block
    tx &= 31;
    ty &= 31;

    // Standard GBA tilemap: 2 bytes per entry, 2048 bytes per screen block.
    const int blockSize = 2048;
    int blockOffset = 0;
    switch (screenSize) {
    case 0: // 32x32, single block
      break;
    case 1: // 64x32, two horizontal blocks
      blockOffset = blockX * blockSize;
      break;
    case 2: // 32x64, two vertical blocks
      blockOffset = blockY * blockSize;
      break;
    case 3: // 64x64, four blocks
      blockOffset = blockX * blockSize + blockY * (blockSize * 2);
      break;
    }

    uint32_t mapAddr = 0;
    uint32_t mapOffset = 0;
    uint16_t tileEntry = 0;
    int tileIndex = 0;
    bool hFlip = false;
    bool vFlip = false;
    int paletteBank = 0;

    // Standard GBA tilemap: 2 bytes per tile (used by ALL games)
    const int entrySize = 2;
    mapAddr = mapBase + blockOffset + (ty * 32 + tx) * entrySize;
    mapOffset = mapAddr - 0x06000000u;
    tileEntry = ReadBgVram16(vramData, vramSize, mapOffset);

    // Extract tile entry components (standard GBA format)
    tileIndex = tileEntry & 0x3FF; // 10-bit tile index
    hFlip = (tileEntry >> 10) & 1;
    vFlip = (tileEntry >> 11) & 1;
    paletteBank = (tileEntry >> 12) & 0xF;

    int inTileX = scrolledX % 8;
    int inTileY = scrolledY % 8;

    if (hFlip)
      inTileX = 7 - inTileX;
    if (vFlip)
      inTileY = 7 - inTileY;

    uint8_t colorIndex = 0;
    uint32_t tileAddr = 0;
    uint8_t tileByte = 0;

    if (!is8bpp) {
      // 4bpp (16 colors per palette bank)
      uint32_t tileStartOffset =
          (tileBase - 0x06000000u) + (uint32_t)tileIndex * 32u;

      // mGBA behavior: skip tiles that would read from OBJ VRAM (>= 0x10000)
      // These tiles are simply not rendered, leaving transparent pixels
      if (tileStartOffset >= 0x10000u) {
        colorIndex = 0; // Transparent
      } else {
        tileAddr = 0x06000000u + tileStartOffset + (uint32_t)(inTileY * 4) +
                   (uint32_t)(inTileX / 2);

        uint32_t tileOffset = (tileAddr - 0x06000000u);
        tileByte = ReadBgVram8(vramData, vramSize, tileOffset);
        bool useHighNibble = (inTileX & 1) != 0;
        colorIndex = useHighNibble ? ((tileByte >> 4) & 0xF) : (tileByte & 0xF);
      }
    } else {
      // 8bpp (256 colors)
      // 64 bytes per tile
      uint32_t tileStartOffset =
          (tileBase - 0x06000000u) + (uint32_t)tileIndex * 64u;

      // mGBA behavior: skip tiles that would read from OBJ VRAM (>= 0x10000)
      if (tileStartOffset >= 0x10000u) {
        colorIndex = 0; // Transparent
      } else {
        tileAddr = tileBase + (tileIndex * 64) + (inTileY * 8) + inTileX;

        uint32_t tileOffset = tileAddr - 0x06000000u;
        tileByte = ReadBgVram8(vramData, vramSize, tileOffset);
        colorIndex = tileByte;
      }
    }

    // Color index 0 is transparent in standard GBA rendering
    bool isTransparent = (colorIndex == 0);

    if (!isTransparent) {
      // Check if this BG layer is enabled by window settings at this pixel
      if (!IsLayerEnabledAtPixel(x, scanline, bgIndex)) {
        continue; // Window masks this layer at this position
      }

      // Only write if this BG has higher or equal priority (lower or equal
      // number) than what's already there We use <= because when priority is
      // the same, lower BG index wins (rendered later in sorted order)
      int pixelIndex = scanline * SCREEN_WIDTH + x;
      if (bgPriority <= priorityBuffer[pixelIndex]) {
        // Calculate effective color index and palette bank
        uint8_t effectiveColorIndex = colorIndex;
        uint8_t effectivePaletteBank = paletteBank;

        // Fetch Color from Palette RAM
        uint32_t paletteAddr = 0x05000000;
        if (!is8bpp) {
          // 4bpp: paletteBank selects which 16-color palette, colorIndex
          // selects color
          paletteAddr +=
              (effectivePaletteBank * 32) + (effectiveColorIndex * 2);
        } else {
          // 8bpp: single 256-color palette
          paletteAddr += (colorIndex * 2);
        }
        uint16_t color = ReadLE16(palData, palSize, paletteAddr - 0x05000000u);

        // Convert 15-bit BGR to 32-bit ARGB
        // GBA: xBBBBBGGGGGRRRRR
        uint8_t r = (color & 0x1F) << 3;
        uint8_t g = ((color >> 5) & 0x1F) << 3;
        uint8_t b = ((color >> 10) & 0x1F) << 3;

        underColorBuffer[pixelIndex] = backBuffer[pixelIndex];
        underLayerBuffer[pixelIndex] = layerBuffer[pixelIndex];
        backBuffer[pixelIndex] = 0xFF000000 | (r << 16) | (g << 8) | b;
        layerBuffer[pixelIndex] = (uint8_t)bgIndex;
        objSemiTransparentBuffer[pixelIndex] = 0;
        priorityBuffer[pixelIndex] = (uint8_t)bgPriority;
      }
    }
  }
}

uint16_t PPU::ReadRegister(uint32_t offset) {
  // Read directly from IO register array, bypassing CPU memory interface.
  // This is critical because write-only registers (like BG scroll, rotation,
  // window dimensions) return open bus when read through the CPU interface,
  // but the PPU needs to read the actual written values internally.
  const uint8_t *ioRegs = memory.GetIORegs();
  constexpr uint32_t IO_REG_SIZE = 0x400; // IO register space is 1KB
  if (offset + 1 < IO_REG_SIZE) {
    return ioRegs[offset] | (static_cast<uint16_t>(ioRegs[offset + 1]) << 8);
  }
  return 0;
}

const std::vector<uint32_t> &PPU::GetFramebuffer() const {
  std::lock_guard<std::mutex> lock(bufferMutex);
  return frontBuffer;
}

void PPU::CopyFramebufferTo(uint32_t *dst, size_t count) const {
  std::lock_guard<std::mutex> lock(bufferMutex);
  size_t n = std::min(count, frontBuffer.size());
  std::memcpy(dst, frontBuffer.data(), n * sizeof(uint32_t));
}

void PPU::SwapBuffers() {
  std::lock_guard<std::mutex> lock(bufferMutex);
  std::swap(frontBuffer, backBuffer);
}

void PPU::SnapshotGraphicsMemory() {
  std::memcpy(vramSnapshot.data(), memory.GetVRAMData(), VRAM_SNAPSHOT_SIZE);
  std::memcpy(paletteSnapshot.data(), memory.GetPaletteData(),
              PALETTE_SNAPSHOT_SIZE);
  std::memcpy(oamSnapshot.data(), memory.GetOAMData(), OAM_SNAPSHOT_SIZE);
}

void PPU::RefreshDirtySnapshots() {
  if (memory.WasOamDirtyByDMA() || memory.WasOamDirtyByCPU())
    std::memcpy(oamSnapshot.data(), memory.GetOAMData(), OAM_SNAPSHOT_SIZE);
  if (memory.WasVramDirtyByDMA() || memory.WasVramDirtyByCPU())
    std::memcpy(vramSnapshot.data(), memory.GetVRAMData(), VRAM_SNAPSHOT_SIZE);
  if (memory.WasPaletteDirtyByDMA() || memory.WasPaletteDirtyByCPU())
    std::memcpy(paletteSnapshot.data(), memory.GetPaletteData(),
                PALETTE_SNAPSHOT_SIZE);
  memory.ClearGraphicsDMADirtyFlags();
  memory.ClearCPUGraphicsDirtyFlags();
}

void PPU::RestoreFramebuffer(const uint32_t *data, size_t count) {
  std::lock_guard<std::mutex> lock(bufferMutex);
  if (count == frontBuffer.size()) {
    std::memcpy(frontBuffer.data(), data, count * sizeof(uint32_t));
  }
}

void PPU::OnIOWrite(void *context, uint32_t offset, uint16_t value) {
  if (context)
    static_cast<PPU *>(context)->HandleIOWrite(offset, value);
}

void PPU::HandleIOWrite(uint32_t offset, uint16_t value) {
  // BG scroll register writes (0x10-0x1E): immediately update latched values.
  // On real hardware the scroll is sampled once per scanline, but with CPU
  // batch-timing jitter the write can land anywhere within a batch window.
  // Updating the latch at write time makes the value deterministic regardless
  // of batch alignment.
  if (offset >= 0x10 && offset <= 0x1E) {
    int bgIndex = (offset - 0x10) / 4;
    bool isVertical = ((offset - 0x10) % 4) >= 2;
    if (isVertical) {
      latchedBGVOFS[bgIndex] = value & 0x01FF;
    } else {
      latchedBGHOFS[bgIndex] = value & 0x01FF;
    }
    return;
  }

  // Per GBATEK: writing BGxX/BGxY mid-frame immediately re-latches the
  // internal reference point registers. This is used by games for wavy
  // backgrounds, screen-shake, and parallax effects (e.g. Metroid Zero
  // Mission). Only the full 32-bit write (high halfword) triggers relatch.
  auto signExtend28 = [](int32_t v) -> int32_t {
    if (v & 0x08000000)
      v |= 0xF0000000;
    return v;
  };

  switch (offset) {
  case 0x2A: { // BG2X high halfword
    uint32_t lo = ReadRegister(0x28);
    uint32_t hi = static_cast<uint32_t>(value) & 0x0FFF;
    bg2x_internal = signExtend28(static_cast<int32_t>((hi << 16) | lo));
    break;
  }
  case 0x2E: { // BG2Y high halfword
    uint32_t lo = ReadRegister(0x2C);
    uint32_t hi = static_cast<uint32_t>(value) & 0x0FFF;
    bg2y_internal = signExtend28(static_cast<int32_t>((hi << 16) | lo));
    break;
  }
  case 0x3A: { // BG3X high halfword
    uint32_t lo = ReadRegister(0x38);
    uint32_t hi = static_cast<uint32_t>(value) & 0x0FFF;
    bg3x_internal = signExtend28(static_cast<int32_t>((hi << 16) | lo));
    break;
  }
  case 0x3E: { // BG3Y high halfword
    uint32_t lo = ReadRegister(0x3C);
    uint32_t hi = static_cast<uint32_t>(value) & 0x0FFF;
    bg3y_internal = signExtend28(static_cast<int32_t>((hi << 16) | lo));
    break;
  }
  default:
    break;
  }
}

// Get window enable bits for a given pixel position
// Returns the enable mask (bits 0-3: BG0-3, bit 4: OBJ, bit 5: Color Effects)
uint8_t PPU::GetWindowMaskForPixel(int x, int y) {
  uint16_t dispcnt = ReadRegister(0x00);
  bool win0Enable = (dispcnt >> 13) & 1;
  bool win1Enable = (dispcnt >> 14) & 1;
  bool objwinEnable = (dispcnt >> 15) & 1;

  // If no windows are enabled, all layers are visible
  if (!win0Enable && !win1Enable && !objwinEnable) {
    return 0x3F; // All layers and effects enabled
  }

  // Check WIN0 (highest priority)
  if (win0Enable) {
    uint16_t win0h = ReadRegister(0x40);
    uint16_t win0v = ReadRegister(0x44);

    int win0Left = (win0h >> 8) & 0xFF;
    int win0Right = win0h & 0xFF;
    int win0Top = (win0v >> 8) & 0xFF;
    int win0Bottom = win0v & 0xFF;

    // Handle horizontal wrap-around
    bool inWin0H;
    if (win0Left <= win0Right) {
      inWin0H = (x >= win0Left && x < win0Right);
    } else {
      // Wrap case: left > right means window wraps around screen edge
      inWin0H = (x >= win0Left || x < win0Right);
    }

    // Handle vertical wrap-around
    bool inWin0V;
    if (win0Top <= win0Bottom) {
      inWin0V = (y >= win0Top && y < win0Bottom);
    } else {
      inWin0V = (y >= win0Top || y < win0Bottom);
    }

    if (inWin0H && inWin0V) {
      uint16_t winin = ReadRegister(0x48);
      return winin & 0x3F; // WIN0 enable bits (lower 6 bits)
    }
  }

  // Check WIN1 (second priority)
  if (win1Enable) {
    uint16_t win1h = ReadRegister(0x42);
    uint16_t win1v = ReadRegister(0x46);

    int win1Left = (win1h >> 8) & 0xFF;
    int win1Right = win1h & 0xFF;
    int win1Top = (win1v >> 8) & 0xFF;
    int win1Bottom = win1v & 0xFF;

    bool inWin1H;
    if (win1Left <= win1Right) {
      inWin1H = (x >= win1Left && x < win1Right);
    } else {
      inWin1H = (x >= win1Left || x < win1Right);
    }

    bool inWin1V;
    if (win1Top <= win1Bottom) {
      inWin1V = (y >= win1Top && y < win1Bottom);
    } else {
      inWin1V = (y >= win1Top || y < win1Bottom);
    }

    if (inWin1H && inWin1V) {
      uint16_t winin = ReadRegister(0x48);
      return (winin >> 8) &
             0x3F; // WIN1 enable bits (upper 6 bits of lower word)
    }
  }

  // Check OBJ window (third priority)
  if (objwinEnable) {
    if (x >= 0 && x < SCREEN_WIDTH && y == scanline) {
      if (objWindowMaskLine[(size_t)x] != 0) {
        uint16_t winout = ReadRegister(0x4A);
        return (winout >> 8) & 0x3F; // WINOBJ enable bits
      }
    }
  }

  // Outside all windows - use WINOUT
  uint16_t winout = ReadRegister(0x4A);
  return winout & 0x3F;
}

void PPU::BuildObjWindowMaskForScanline() {
  objWindowMaskLine.fill(0);

  const uint16_t dispcnt = ReadRegister(0x00);
  const bool objwinEnable = ((dispcnt >> 15) & 1) != 0;
  if (!objwinEnable) {
    return;
  }

  const bool mapping1D = ((dispcnt >> 6) & 1) != 0;
  const uint32_t tileBase = 0x06010000;

  const uint8_t *oamData = oamSnapshot.data();
  const size_t oamSize = OAM_SNAPSHOT_SIZE;
  const uint8_t *vramData = vramSnapshot.data();
  const size_t vramSize = VRAM_SNAPSHOT_SIZE;

  static const int sizes[3][4][2] = {
      {{8, 8}, {16, 16}, {32, 32}, {64, 64}}, // Square
      {{16, 8}, {32, 8}, {32, 16}, {64, 32}}, // Horizontal
      {{8, 16}, {8, 32}, {16, 32}, {32, 64}}  // Vertical
  };

  for (int i = 0; i < 128; ++i) {
    const uint32_t oamOff = (uint32_t)(i * 8);
    const uint16_t attr0 = ReadLE16(oamData, oamSize, oamOff);
    const uint16_t attr1 = ReadLE16(oamData, oamSize, oamOff + 2);
    const uint16_t attr2 = ReadLE16(oamData, oamSize, oamOff + 4);

    const bool mosaicEnable = ((attr0 >> 12) & 1) != 0;
    int mosaicH = 1;
    int mosaicV = 1;
    if (mosaicEnable) {
      const uint16_t mosaic = ReadRegister(0x4C);
      mosaicH = ((mosaic >> 8) & 0xF) + 1;
      mosaicV = ((mosaic >> 12) & 0xF) + 1;
    }

    const uint8_t objMode = (attr0 >> 10) & 0x3;
    if (objMode != 2) {
      continue;
    }

    int y = attr0 & 0xFF;

    const bool affine = ((attr0 >> 8) & 1) != 0;
    const bool doubleSizeOrDisable = ((attr0 >> 9) & 1) != 0;
    if (!affine && doubleSizeOrDisable) {
      continue; // disabled
    }
    const bool isAffine = affine;
    const bool isDoubleSize = affine && doubleSizeOrDisable;

    const int shape = (attr0 >> 14) & 0x3;
    const int size = (attr1 >> 14) & 0x3;

    // GBATEK: OBJ shape=3 is prohibited.
    if (shape == 3) {
      continue;
    }
    const int width = sizes[shape][size][0];
    const int height = sizes[shape][size][1];

    const int boundWidth = isDoubleSize ? width * 2 : width;
    const int boundHeight = isDoubleSize ? height * 2 : height;

    // GBA Y-coords are 8-bit unsigned; large sprites near Y=255 wrap into
    // negative territory to appear at the top of the screen.
    if (y + boundHeight > 256)
      y -= 256;

    if (scanline < y || scanline >= y + boundHeight) {
      continue;
    }

    int x = attr1 & 0x1FF;
    if (x >= 256)
      x -= 512;

    const int tileIndex = attr2 & 0x3FF;
    const bool is8bpp = ((attr0 >> 13) & 1) != 0;

    int effectiveTileIndex = tileIndex;
    if (is8bpp) {
      // In 8bpp OBJ mode, the tile index's LSB is ignored on real hardware.
      effectiveTileIndex &= ~1;
    }

    const bool hFlip = !isAffine && (((attr1 >> 12) & 1) != 0);
    const bool vFlip = !isAffine && (((attr1 >> 13) & 1) != 0);

    int16_t pa = 0x100, pb = 0, pc = 0, pd = 0x100;
    if (isAffine) {
      const int affineIndex = (attr1 >> 9) & 0x1F;
      const uint32_t affineBase = 0x07000006u + (uint32_t)(affineIndex * 32);
      pa = (int16_t)ReadSnapshotOam16(oamSnapshot.data(), affineBase);
      pb = (int16_t)ReadSnapshotOam16(oamSnapshot.data(), affineBase + 8);
      pc = (int16_t)ReadSnapshotOam16(oamSnapshot.data(), affineBase + 16);
      pd = (int16_t)ReadSnapshotOam16(oamSnapshot.data(), affineBase + 24);
    }

    const int centerX = width / 2;
    const int centerY = height / 2;

    for (int sx = 0; sx < boundWidth; ++sx) {
      const int screenX = x + sx;
      if (screenX < 0 || screenX >= SCREEN_WIDTH)
        continue;

      int sampleSX = sx;
      int sampleLine = scanline - y;
      if (mosaicEnable) {
        sampleSX -= (sampleSX % mosaicH);
        sampleLine -= (sampleLine % mosaicV);
      }

      int spriteX, spriteY;
      if (isAffine) {
        const int px = sampleSX - boundWidth / 2;
        const int py = sampleLine - boundHeight / 2;
        spriteX = ((pa * px + pb * py) >> 8) + centerX;
        spriteY = ((pc * px + pd * py) >> 8) + centerY;
        if (spriteX < 0 || spriteX >= width || spriteY < 0 ||
            spriteY >= height) {
          continue;
        }
      } else {
        spriteX = hFlip ? (width - 1 - sampleSX) : sampleSX;
        int lineInSprite = sampleLine;
        if (vFlip)
          lineInSprite = height - 1 - lineInSprite;
        spriteY = lineInSprite;
      }

      const int inTileX = spriteX % 8;
      const int inTileY = spriteY % 8;
      uint8_t colorIndex = 0;

      if (mapping1D) {
        int tileNum;
        if (is8bpp) {
          tileNum = effectiveTileIndex + (spriteY / 8) * (width / 8) * 2 +
                    (spriteX / 8) * 2;
        } else {
          tileNum =
              effectiveTileIndex + (spriteY / 8) * (width / 8) + (spriteX / 8);
        }

        if (is8bpp) {
          const uint32_t addr = tileBase + (uint32_t)tileNum * 32u +
                                (uint32_t)inTileY * 8u + (uint32_t)inTileX;
          colorIndex = ReadVram8(vramData, vramSize, addr - 0x06000000u);
        } else {
          const uint32_t addr = tileBase + (uint32_t)tileNum * 32u +
                                (uint32_t)inTileY * 4u +
                                (uint32_t)(inTileX / 2);
          const uint8_t byte =
              ReadVram8(vramData, vramSize, addr - 0x06000000u);
          colorIndex = (inTileX & 1) ? ((byte >> 4) & 0xF) : (byte & 0xF);
        }
      } else {
        const int tx = spriteX / 8;
        const int ty = spriteY / 8;

        int tileNum;
        if (is8bpp) {
          tileNum = effectiveTileIndex + ty * 64 + tx * 2;
        } else {
          tileNum = effectiveTileIndex + ty * 32 + tx;
        }

        if (is8bpp) {
          const uint32_t addr = tileBase + (uint32_t)tileNum * 32u +
                                (uint32_t)inTileY * 8u + (uint32_t)inTileX;
          colorIndex = ReadVram8(vramData, vramSize, addr - 0x06000000u);
        } else {
          const uint32_t addr = tileBase + (uint32_t)tileNum * 32u +
                                (uint32_t)inTileY * 4u +
                                (uint32_t)(inTileX / 2);
          const uint8_t byte =
              ReadVram8(vramData, vramSize, addr - 0x06000000u);
          colorIndex = (inTileX & 1) ? ((byte >> 4) & 0xF) : (byte & 0xF);
        }
      }

      if (colorIndex != 0) {
        objWindowMaskLine[(size_t)screenX] = 1;
      }
    }
  }
}

// Check if a specific layer should be rendered at this pixel
// layer: 0-3 = BG0-3, 4 = OBJ, 5 = Color Effects
bool PPU::IsLayerEnabledAtPixel(int x, int y, int layer) {
  uint8_t mask = GetWindowMaskForPixel(x, y);
  return (mask >> layer) & 1;
}

void PPU::ApplyColorEffects() {
  // Read blend control registers
  uint16_t bldcnt = ReadRegister(0x50); // BLDCNT

  uint16_t bldalpha = ReadRegister(0x52); // BLDALPHA
  uint16_t bldy = ReadRegister(0x54);     // BLDY

  int effectMode = (bldcnt >> 6) & 0x3;

  // Get the brightness coefficient (0-16, higher = more effect)
  int evyRaw = bldy & 0x1F;
  int evy = evyRaw;
  if (evy > 16)
    evy = 16;

  // For alpha blending (mode 1)
  int eva = bldalpha & 0x1F;        // First target coefficient
  int evb = (bldalpha >> 8) & 0x1F; // Second target coefficient
  if (eva > 16)
    eva = 16;
  if (evb > 16)
    evb = 16;

  // First target layers (bits 0-5 of BLDCNT)
  uint8_t firstTarget = bldcnt & 0x3F;
  // Second target layers (bits 8-13 of BLDCNT)
  uint8_t secondTarget = (bldcnt >> 8) & 0x3F;

  auto to5 = [](uint8_t v8) -> int {
    // Our pipeline expands BGR555 -> 8-bit via <<3, so >>3 recovers the exact
    // 5-bit channel.
    return (int)(v8 >> 3);
  };
  auto from5 = [](int v5) -> uint8_t {
    if (v5 < 0)
      v5 = 0;
    if (v5 > 31)
      v5 = 31;
    return (uint8_t)(v5 << 3);
  };

  // Public testing helpers (declared in PPU.h)
  auto applyBrightnessIncrease = [&](uint8_t r, uint8_t g, uint8_t b, int evy) {
    auto to5 = [](uint8_t v8) -> int { return (int)(v8 >> 3); };
    int rr = from5(to5(r) + ((31 - to5(r)) * evy / 16));
    int gg = from5(to5(g) + ((31 - to5(g)) * evy / 16));
    int bb = from5(to5(b) + ((31 - to5(b)) * evy / 16));
    return (uint32_t)((rr << 16) | (gg << 8) | bb);
  };

  auto applyBrightnessDecrease = [&](uint8_t r, uint8_t g, uint8_t b, int evy) {
    auto to5 = [](uint8_t v8) -> int { return (int)(v8 >> 3); };
    int rr = from5(to5(r) - (to5(r) * evy / 16));
    int gg = from5(to5(g) - (to5(g) * evy / 16));
    int bb = from5(to5(b) - (to5(b) * evy / 16));
    return (uint32_t)((rr << 16) | (gg << 8) | bb);
  };

  auto blendChannel5 = [](uint8_t a8, uint8_t b8, int eva, int evb) -> uint8_t {
    const int a5 = (int)(a8 >> 3);
    const int b5 = (int)(b8 >> 3);
    int out5 = (a5 * eva + b5 * evb) / 16;
    if (out5 < 0)
      out5 = 0;
    if (out5 > 31)
      out5 = 31;
    return (uint8_t)(out5 << 3);
  };

  // Apply effect to each pixel on this scanline
  for (int x = 0; x < SCREEN_WIDTH; ++x) {
    int pixelIndex = scanline * SCREEN_WIDTH + x;

    // Check if color effects are enabled at this pixel (window bit 5)
    if (!IsLayerEnabledAtPixel(x, scanline, 5)) {
      continue;
    }

    const uint8_t topLayer =
        layerBuffer[pixelIndex] <= 5 ? layerBuffer[pixelIndex] : 5;
    const uint8_t underLayer =
        underLayerBuffer[pixelIndex] <= 5 ? underLayerBuffer[pixelIndex] : 5;

    const bool topIsObjSemiTransparent =
        (topLayer == 4) && (objSemiTransparentBuffer[pixelIndex] != 0);

    // Respect BLDCNT target selection.
    const bool topIsFirstTarget = ((firstTarget >> topLayer) & 1) != 0;

    uint32_t color = backBuffer[pixelIndex];
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    // For now, we'll apply effects to all visible pixels
    // A more accurate implementation would track which layer each pixel came
    // from and only apply effects to first-target pixels

    // The priorityBuffer tells us the layer:
    // 0-3 = BG0-3 (by priority), 4 = backdrop
    // But we need to know the actual BG index, not just priority...
    // For simplicity, let's assume the effect applies if the mode is brightness

    // Semi-transparent OBJ pixels always use alpha blending against the
    // underlying pixel, regardless of the BLDCNT effect mode. Per GBATEK,
    // semi-transparent OBJs do NOT require the OBJ bit in firstTarget;
    // they only require the underlying layer to be in secondTarget.
    if (topIsObjSemiTransparent) {
      const bool underIsSecondTarget = ((secondTarget >> underLayer) & 1) != 0;
      if (!underIsSecondTarget) {
        continue;
      }

      const uint32_t under = underColorBuffer[pixelIndex];
      const uint8_t ur = (under >> 16) & 0xFF;
      const uint8_t ug = (under >> 8) & 0xFF;
      const uint8_t ub = under & 0xFF;

      r = blendChannel5(r, ur, eva, evb);
      g = blendChannel5(g, ug, eva, evb);
      b = blendChannel5(b, ub, eva, evb);
    } else if (effectMode == 1) {
      if (!topIsFirstTarget) {
        continue;
      }
      const bool underIsSecondTarget = ((secondTarget >> underLayer) & 1) != 0;
      if (!underIsSecondTarget) {
        continue;
      }

      // Regular alpha blending when BLDCNT selects the top as first target.
      const uint32_t under = underColorBuffer[pixelIndex];
      const uint8_t ur = (under >> 16) & 0xFF;
      const uint8_t ug = (under >> 8) & 0xFF;
      const uint8_t ub = under & 0xFF;

      r = blendChannel5(r, ur, eva, evb);
      g = blendChannel5(g, ug, eva, evb);
      b = blendChannel5(b, ub, eva, evb);
    } else if (effectMode == 2) {
      if (!topIsFirstTarget) {
        continue;
      }
      // Brightness Increase (fade to white)
      // I = I + (31-I) * EVY / 16
      r = from5(to5(r) + ((31 - to5(r)) * evy / 16));
      g = from5(to5(g) + ((31 - to5(g)) * evy / 16));
      b = from5(to5(b) + ((31 - to5(b)) * evy / 16));
    } else if (effectMode == 3) {
      if (!topIsFirstTarget) {
        continue;
      }
      // Brightness Decrease (fade to black)
      // I = I - I * EVY / 16
      r = from5(to5(r) - (to5(r) * evy / 16));
      g = from5(to5(g) - (to5(g) * evy / 16));
      b = from5(to5(b) - (to5(b) * evy / 16));
    }

    backBuffer[pixelIndex] = 0xFF000000 | (r << 16) | (g << 8) | b;
  }
}

} // namespace AIO::Emulator::GBA
