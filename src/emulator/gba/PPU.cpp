#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <emulator/common/Logger.h>
#include <emulator/gba/GBAMemory.h>
#include <emulator/gba/PPU.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
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
inline bool EnvTruthy(const char *v) {
  return v != nullptr && v[0] != '\0' && v[0] != '0';
}

inline int EnvInt(const char *name, int defaultValue) {
  const char *v = std::getenv(name);
  if (!EnvTruthy(v)) {
    return defaultValue;
  }
  return std::atoi(v);
}

template <size_t N> inline bool EnvFlagCached(const char (&name)[N]) {
  static const bool enabled = EnvTruthy(std::getenv(name));
  return enabled;
}

bool TraceGbaSpam() { return EnvFlagCached("AIO_TRACE_GBA_SPAM"); }

bool PpuIgnoreWindows() { return EnvFlagCached("AIO_PPU_IGNORE_WINDOWS"); }

bool PpuDisableColorEffects() {
  return EnvFlagCached("AIO_PPU_DISABLE_COLOR_EFFECTS");
}

bool PpuSwap4bppNibbles() { return EnvFlagCached("AIO_PPU_SWAP_4BPP_NIBBLES"); }

struct BgPixelTraceConfig {
  bool enabled{false};
  int frame{-1};
  int x{-1};
  int y{-1};
};

const BgPixelTraceConfig &GetBgPixelTraceConfig() {
  static BgPixelTraceConfig cfg;
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    cfg.enabled = EnvTruthy(std::getenv("AIO_TRACE_PPU_BGPIX"));
    if (const char *s = std::getenv("AIO_TRACE_PPU_BGPIX_FRAME")) {
      cfg.frame = std::atoi(s);
    }
    if (const char *s = std::getenv("AIO_TRACE_PPU_BGPIX_X")) {
      cfg.x = std::atoi(s);
    }
    if (const char *s = std::getenv("AIO_TRACE_PPU_BGPIX_Y")) {
      cfg.y = std::atoi(s);
    }
  }
  return cfg;
}

struct FinalPixelTraceConfig {
  bool enabled{false};
  int frame{-1};
  int x{-1};
  int y{-1};
};

const FinalPixelTraceConfig &GetFinalPixelTraceConfig() {
  static FinalPixelTraceConfig cfg;
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    cfg.enabled = EnvTruthy(std::getenv("AIO_TRACE_PPU_PIX"));
    if (const char *s = std::getenv("AIO_TRACE_PPU_PIX_FRAME")) {
      cfg.frame = std::atoi(s);
    }
    if (const char *s = std::getenv("AIO_TRACE_PPU_PIX_X")) {
      cfg.x = std::atoi(s);
    }
    if (const char *s = std::getenv("AIO_TRACE_PPU_PIX_Y")) {
      cfg.y = std::atoi(s);
    }
  }
  return cfg;
}

struct ObjPixelTraceConfig {
  bool enabled{false};
  int frame{-1};
  int x{-1};
  int y{-1};
  int maxHits{16};
};

struct OamTraceConfig {
  bool enabled{false};
  int startFrame{0};
  int endFrame{-1};
  int maxLines{1200};
  bool spriteDetails{false};
  int spriteDetailsFrame{-1};
  int spriteDetailsMax{16};
};

const ObjPixelTraceConfig &GetObjPixelTraceConfig() {
  static ObjPixelTraceConfig cfg;
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    cfg.enabled = EnvTruthy(std::getenv("AIO_TRACE_PPU_OBJPIX"));
    if (const char *s = std::getenv("AIO_TRACE_PPU_OBJPIX_FRAME")) {
      cfg.frame = std::atoi(s);
    }
    if (const char *s = std::getenv("AIO_TRACE_PPU_OBJPIX_X")) {
      cfg.x = std::atoi(s);
    }
    if (const char *s = std::getenv("AIO_TRACE_PPU_OBJPIX_Y")) {
      cfg.y = std::atoi(s);
    }
    if (const char *s = std::getenv("AIO_TRACE_PPU_OBJPIX_MAX")) {
      cfg.maxHits = std::atoi(s);
      if (cfg.maxHits <= 0)
        cfg.maxHits = 16;
    }
  }
  return cfg;
}

const OamTraceConfig &GetOamTraceConfig() {
  static OamTraceConfig cfg;
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    cfg.enabled = EnvTruthy(std::getenv("AIO_TRACE_PPU_OAM"));
    cfg.startFrame = EnvInt("AIO_TRACE_PPU_OAM_START", 0);
    cfg.endFrame = EnvInt("AIO_TRACE_PPU_OAM_END", -1);
    cfg.maxLines = EnvInt("AIO_TRACE_PPU_OAM_MAX", 1200);
    cfg.spriteDetails = EnvTruthy(std::getenv("AIO_TRACE_PPU_OAM_SPR"));
    cfg.spriteDetailsFrame = EnvInt("AIO_TRACE_PPU_OAM_SPR_FRAME", -1);
    cfg.spriteDetailsMax = EnvInt("AIO_TRACE_PPU_OAM_SPR_MAX", 16);
    if (cfg.spriteDetailsMax <= 0) {
      cfg.spriteDetailsMax = 16;
    }
  }
  return cfg;
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

inline uint8_t ReadBgVram8(const uint8_t *vram, size_t vramSize,
                           uint32_t offset) {
  if (!vram || vramSize == 0)
    return 0;
  const uint32_t mapped =
      MapBgVramOffset(offset) % static_cast<uint32_t>(vramSize);
  return vram[mapped];
}

inline uint16_t ReadBgVram16(const uint8_t *vram, size_t vramSize,
                             uint32_t offset) {
  if (!vram || vramSize == 0)
    return 0;
  const uint32_t o0 = MapBgVramOffset(offset) % static_cast<uint32_t>(vramSize);
  const uint32_t o1 =
      MapBgVramOffset(offset + 1u) % static_cast<uint32_t>(vramSize);
  return (uint16_t)(vram[o0] | (vram[o1] << 8));
}

} // namespace

PPU::PPU(GBAMemory &mem)
    : memory(mem), cycleCounter(0), scanline(0), frameCount(0) {
  instanceId = NextPpuInstanceId();
  // Initialize double buffers with black
  backBuffer.resize(SCREEN_WIDTH * SCREEN_HEIGHT, 0xFF000000);
  frontBuffer.resize(SCREEN_WIDTH * SCREEN_HEIGHT, 0xFF000000);
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
  scanline = 0;
  frameCount = 0;
  prevVBlankState = false;

  // Reset internal affine counters
  bg2x_internal = 0;
  bg2y_internal = 0;
  bg3x_internal = 0;
  bg3y_internal = 0;

  // Clear OBJ window mask
  objWindowMaskLine.fill(0);

  // CRITICAL: Reset Classic NES mode flag (fixes state leakage between ROMs)
  classicNesMode = false;

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

  // DIAGNOSTIC: Trace PPU::Update entry
  static const bool tracePpuUpdate = EnvFlagCached("AIO_TRACE_NES_PALETTE");
  static int ppuUpdateTraces = 0;
  if (tracePpuUpdate && ppuUpdateTraces < 5) {
    ppuUpdateTraces++;
    std::cerr << "[PPU_UPDATE] cycles=" << cycles << " scanline=" << scanline
              << " cycleCounter=" << cycleCounter << " frame=" << frameCount
              << std::endl;
    std::cerr.flush();
  }

  if (TraceGbaSpam()) {
    static int updateCallCount = 0;
  }

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
      // HBlank Start
      if (scanline < 160) {
        // Render the scanline using the VRAM/OAM state from the visible period.
        // HBlank is when games/DMA typically update VRAM for the *next*
        // scanline.
        DrawScanline();
      }

      // Hardware enters HBlank on every scanline (including VBlank).
      // Set DISPSTAT first so any HBlank-triggered work sees the flag.
      // Use ReadIORegister16Internal to avoid infinite recursion with flush.
      uint16_t dispstat = memory.ReadIORegister16Internal(0x04);
      dispstat |= 2; // HBlank flag (Bit 1)
      memory.WriteIORegisterInternal(0x04, dispstat);

      // Apply any palette/VRAM writes that were queued during the visible
      // period so they become visible at the next safe window.
      memory.ApplyDeferredWrites();

      // Trigger HBlank IRQ if enabled in DISPSTAT.
      if (dispstat & 0x10) { // HBlank IRQ Enable (Bit 4)
        uint16_t if_reg = memory.ReadIORegister16Internal(0x202);
        if_reg |= 2; // HBlank IRQ bit (Bit 1)
        // IF is write-1-to-clear when written by the CPU.
        // When an interrupt occurs, hardware sets IF bits.
        memory.WriteIORegisterInternal(0x202, if_reg);
      }

      // Trigger HBlank DMA after flags/IRQs are visible. This also avoids
      // stale writes if DMA advances PPU time (re-entrant Update()).
      memory.CheckDMA(2);
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

        // Swap buffers after frame completion for thread-safe display
        SwapBuffers();

        // DKC diagnostic: Log DISPCNT during problem frames
        if ((frameCount >= 2195 && frameCount <= 2205) ||
            (frameCount >= 2400 && frameCount <= 2415)) {
          uint16_t dispcnt = memory.ReadIORegister16Internal(0x00);
          bool forcedBlank = (dispcnt >> 7) & 1;
          uint8_t bgMode = dispcnt & 0x7;
          bool displayBG0 = (dispcnt >> 8) & 1;
          bool displayBG1 = (dispcnt >> 9) & 1;
          bool displayBG2 = (dispcnt >> 10) & 1;
          bool displayBG3 = (dispcnt >> 11) & 1;
          bool displayOBJ = (dispcnt >> 12) & 1;
          std::cout << "[DKC_DIAG] Frame " << frameCount << " DISPCNT=0x"
                    << std::hex << dispcnt << std::dec
                    << " mode=" << (int)bgMode << " forcedBlank=" << forcedBlank
                    << " BG0=" << displayBG0 << " BG1=" << displayBG1
                    << " BG2=" << displayBG2 << " BG3=" << displayBG3
                    << " OBJ=" << displayOBJ;

          // Check BG palette (first 16 colors)
          std::cout << " | BG_PAL:";
          for (int i = 0; i < 16; i++) {
            uint16_t color = memory.Read16(0x05000000 + i * 2);
            std::cout << " " << std::hex << color << std::dec;
          }
          std::cout << std::endl;
        }
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
          // Apply any deferred palette writes now that we're in VBlank
          // This ensures palette is stable for entire previous frame
          memory.ApplyDeferredWrites();

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

            // Also set BIOS_IF for IntrWait/VBlankIntrWait
            uint16_t biosIF = memory.Read16(0x03007FF8) | 1;
            memory.Write16(0x03007FF8, biosIF);

            if (TraceGbaSpam()) {
              static int vblankCount = 0;
              if (++vblankCount % 60 == 0) { // Log every 60 frames = 1 second
                std::cout << "[PPU] VBlank #" << vblankCount
                          << " Frame=" << frameCount << std::endl;
              }
            }
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
  uint16_t dispcnt = ReadRegister(0x00);
  int mode = dispcnt & 0x7;

  // DIAGNOSTIC: Trace DrawScanline entry
  static const bool traceDrawScanline = EnvFlagCached("AIO_TRACE_NES_PALETTE");
  static int drawScanlineTraces = 0;
  if (traceDrawScanline && drawScanlineTraces < 5 && scanline == 0) {
    drawScanlineTraces++;
    std::cerr << "[DRAW_SCANLINE] frame=" << frameCount << " mode=" << mode
              << " dispcnt=0x" << std::hex << dispcnt << std::dec << std::endl;
    std::cerr.flush();
  }

  // Optional: per-frame PPU state trace (minimal volume, scanline 0 only).
  // Enable with: AIO_TRACE_PPU_FRAMESTATE=1
  if (scanline == 0) {
    static const bool traceFrameState =
        EnvTruthy(std::getenv("AIO_TRACE_PPU_FRAMESTATE"));
    if (traceFrameState) {
      static bool initialized = false;
      static int startFrame = 0;
      static int endFrame = -1;
      static int maxLines = 1200;
      static int linesLogged = 0;

      if (!initialized) {
        initialized = true;
        // Defaults preserve old behavior: log first 1200 frames.
        startFrame = EnvInt("AIO_TRACE_PPU_FRAMESTATE_START", 0);
        endFrame = EnvInt("AIO_TRACE_PPU_FRAMESTATE_END", -1);
        maxLines = EnvInt("AIO_TRACE_PPU_FRAMESTATE_MAX", 1200);
      }

      const int fc = (int)frameCount;
      if (fc < startFrame) {
        return;
      }
      if (endFrame >= 0 && fc > endFrame) {
        return;
      }
      if (maxLines >= 0 && linesLogged >= maxLines) {
        return;
      }

      linesLogged++;

      const uint16_t bldcnt = ReadRegister(0x50);
      const uint16_t bldalpha = ReadRegister(0x52);
      const uint16_t winin = ReadRegister(0x48);
      const uint16_t winout = ReadRegister(0x4A);
      const uint16_t mosaic = ReadRegister(0x4C);
      const uint16_t win0h = ReadRegister(0x40);
      const uint16_t win0v = ReadRegister(0x44);
      const uint16_t win1h = ReadRegister(0x42);
      const uint16_t win1v = ReadRegister(0x46);
      const uint16_t bg0cnt = ReadRegister(0x08);
      const uint16_t bg1cnt = ReadRegister(0x0A);
      const uint16_t bg2cnt = ReadRegister(0x0C);
      const uint16_t bg3cnt = ReadRegister(0x0E);
      const uint16_t bg0hofs = ReadRegister(0x10);
      const uint16_t bg0vofs = ReadRegister(0x12);
      const uint16_t bg1hofs = ReadRegister(0x14);
      const uint16_t bg1vofs = ReadRegister(0x16);
      const uint16_t bg2hofs = ReadRegister(0x18);
      const uint16_t bg2vofs = ReadRegister(0x1A);
      const uint16_t bg3hofs = ReadRegister(0x1C);
      const uint16_t bg3vofs = ReadRegister(0x1E);
      const uint16_t dispstat = ReadRegister(0x04);
      const uint16_t vcount = ReadRegister(0x06);
      const uint16_t ie = memory.ReadIORegister16Internal(0x200);
      const uint16_t if_reg = memory.ReadIORegister16Internal(0x202);
      const uint16_t ime = memory.ReadIORegister16Internal(0x208);
      const uint16_t bios_if = memory.Read16(0x03007FF8);

      std::cout << "[PPU_FRAME] frameCount=" << fc << " logLine=" << linesLogged
                << " DISPCNT=0x" << std::hex << dispcnt << " mode=" << std::dec
                << mode << " BG_EN=0x" << std::hex << ((dispcnt >> 8) & 0xF)
                << " OBJ_EN=" << (((dispcnt >> 12) & 1) ? 1 : 0) << " WIN=0x"
                << (((dispcnt >> 13) & 0x7) | (((dispcnt >> 15) & 1) << 3))
                << " DISPSTAT=0x" << std::hex << dispstat
                << " VCOUNT=" << std::dec << (unsigned)vcount
                << " IME=" << (unsigned)ime << " IE=0x" << std::hex << ie
                << " IF=0x" << if_reg << " BIOS_IF=0x" << bios_if << " BG0=0x"
                << bg0cnt << " BG1=0x" << bg1cnt << " BG2=0x" << bg2cnt
                << " BG3=0x" << bg3cnt << " BG0HOFS=0x" << bg0hofs
                << " BG0VOFS=0x" << bg0vofs << " BG1HOFS=0x" << bg1hofs
                << " BG1VOFS=0x" << bg1vofs << " BG2HOFS=0x" << bg2hofs
                << " BG2VOFS=0x" << bg2vofs << " BG3HOFS=0x" << bg3hofs
                << " BG3VOFS=0x" << bg3vofs << " BLDCNT=0x" << bldcnt
                << " BLDALPHA=0x" << bldalpha << " WININ=0x" << winin
                << " WINOUT=0x" << winout << " WIN0H=0x" << win0h << " WIN0V=0x"
                << win0v << " WIN1H=0x" << win1h << " WIN1V=0x" << win1v
                << " MOSAIC=0x" << mosaic << std::dec << std::endl;
    }

    const auto &oamTraceCfg = GetOamTraceConfig();
    static int oamLinesLogged = 0;
    if (oamTraceCfg.enabled) {
      const int fc = (int)frameCount;
      if (fc >= oamTraceCfg.startFrame &&
          (oamTraceCfg.endFrame < 0 || fc <= oamTraceCfg.endFrame) &&
          (oamTraceCfg.maxLines < 0 || oamLinesLogged < oamTraceCfg.maxLines)) {
        oamLinesLogged++;

        uint32_t enabled = 0;
        uint32_t disabled = 0;
        uint32_t objWindow = 0;
        uint32_t prohibited = 0;
        uint32_t affine = 0;
        uint32_t semiTransparent = 0;
        uint32_t anyNonZeroAttr = 0;

        const uint8_t *oamData = memory.GetOAMData();
        const size_t oamSize = memory.GetOAMSize();
        const uint8_t *vramData = memory.GetVRAMData();
        const size_t vramSize = memory.GetVRAMSize();
        for (int i = 0; i < 128; ++i) {
          const uint32_t oamOff = (uint32_t)(i * 8);
          const uint16_t attr0 = ReadLE16(oamData, oamSize, oamOff);
          const uint16_t attr1 = ReadLE16(oamData, oamSize, oamOff + 2);
          const uint16_t attr2 = ReadLE16(oamData, oamSize, oamOff + 4);
          if ((attr0 | attr1 | attr2) != 0) {
            anyNonZeroAttr++;
          }

          const bool isAffine = ((attr0 >> 8) & 1) != 0;
          const bool doubleSizeOrDisable = ((attr0 >> 9) & 1) != 0;
          const uint8_t objMode = (attr0 >> 10) & 0x3;

          if (isAffine) {
            affine++;
          }
          if (objMode == 1) {
            semiTransparent++;
          }

          if (!isAffine && doubleSizeOrDisable) {
            disabled++;
            continue;
          }
          if (objMode == 3) {
            prohibited++;
            continue;
          }
          if (objMode == 2) {
            objWindow++;
            continue;
          }
          enabled++;
        }

        std::cout << "[PPU_OAM] frameCount=" << fc << " enabled=" << enabled
                  << " disabled=" << disabled << " objWindow=" << objWindow
                  << " prohibited=" << prohibited << " affine=" << affine
                  << " semiT=" << semiTransparent
                  << " anyNonZeroAttr=" << anyNonZeroAttr << std::endl;

        if (oamTraceCfg.spriteDetails &&
            (oamTraceCfg.spriteDetailsFrame < 0 ||
             fc == oamTraceCfg.spriteDetailsFrame)) {
          int printed = 0;
          const uint16_t dispcnt = ReadRegister(0x00);
          const bool mapping1D = ((dispcnt >> 6) & 1) != 0;

          for (int i = 0; i < 128 && printed < oamTraceCfg.spriteDetailsMax;
               ++i) {
            const uint32_t oamOff = (uint32_t)(i * 8);
            const uint16_t attr0 = ReadLE16(oamData, oamSize, oamOff);
            const uint16_t attr1 = ReadLE16(oamData, oamSize, oamOff + 2);
            const uint16_t attr2 = ReadLE16(oamData, oamSize, oamOff + 4);

            const bool isAffine = ((attr0 >> 8) & 1) != 0;
            const bool doubleSizeOrDisable = ((attr0 >> 9) & 1) != 0;
            const uint8_t objMode = (attr0 >> 10) & 0x3;
            if (!isAffine && doubleSizeOrDisable)
              continue;
            if (objMode == 3 || objMode == 2)
              continue;

            const int x = attr1 & 0x1FF;
            int y = attr0 & 0xFF;
            if (y > 160)
              y -= 256;

            const uint8_t paletteBank = (attr2 >> 12) & 0xF;
            const uint8_t priority = (attr2 >> 10) & 0x3;
            const bool is8bpp = ((attr0 >> 13) & 1) != 0;
            const uint32_t rawTileIndex = (attr2 & 0x3FFu);
            const uint32_t baseTileIndex =
                is8bpp ? (rawTileIndex & ~1u) : rawTileIndex;

            const uint32_t tileBase = 0x06010000u;
            const uint32_t baseAddr = tileBase + baseTileIndex * 32u;
            const uint32_t bytesToScan = is8bpp ? 64u : 32u;
            uint32_t nonZeroBytes = 0;
            for (uint32_t off = 0; off < bytesToScan; ++off) {
              const uint8_t b =
                  ReadVram8(vramData, vramSize, (baseAddr - 0x06000000u) + off);
              if (b != 0) {
                nonZeroBytes++;
              }
            }

            printed++;
            std::cout << "[PPU_OAM_SPR] frameCount=" << fc << " i=" << i
                      << " x=" << x << " y=" << y << " mode=" << (int)objMode
                      << " map1D=" << (mapping1D ? 1 : 0)
                      << " 8bpp=" << (is8bpp ? 1 : 0)
                      << " tile=" << rawTileIndex
                      << " baseTile=" << baseTileIndex
                      << " prio=" << (int)priority
                      << " palBank=" << (int)paletteBank
                      << " tileNonZeroBytes=" << nonZeroBytes << " attr0=0x"
                      << std::hex << attr0 << " attr1=0x" << attr1
                      << " attr2=0x" << attr2 << std::dec << std::endl;
          }
        }
      }
    }
  }

  BuildObjWindowMaskForScanline();

  // Fetch Backdrop Color (Palette Index 0)
  uint16_t backdropColor = memory.Read16(0x05000000);
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

  if (mode == 0) {
    RenderMode0();
  } else if (mode == 1) {
    RenderMode1();
  } else if (mode == 2) {
    RenderMode2();
  } else if (mode == 3) {
    RenderMode3();
  } else if (mode == 4) {
    RenderMode4();
  } else if (mode == 5) {
    RenderMode5();
  }

  // Render OBJ (Sprites)
  if (dispcnt & 0x1000) { // OBJ Enable
    RenderOBJ();
  }

  // Apply Color Special Effects (Blending/Brightness)
  ApplyColorEffects();

  // Optional: trace the final composed pixel for a given frame/coordinate.
  // Enable with: AIO_TRACE_PPU_PIX=1
  // Params: AIO_TRACE_PPU_PIX_FRAME, AIO_TRACE_PPU_PIX_X, AIO_TRACE_PPU_PIX_Y
  {
    const auto &cfg = GetFinalPixelTraceConfig();
    if (cfg.enabled && (cfg.frame < 0 || frameCount == cfg.frame) &&
        (cfg.y < 0 || scanline == cfg.y) &&
        (cfg.x >= 0 && cfg.x < SCREEN_WIDTH)) {
      const int x = cfg.x;
      const int pixelIndex = scanline * SCREEN_WIDTH + x;
      std::cout << "[PPU_PIX] frame=" << frameCount << " x=" << x
                << " y=" << scanline << " argb=0x" << std::hex
                << backBuffer[pixelIndex] << std::dec
                << " layer=" << (int)layerBuffer[pixelIndex]
                << " prio=" << (int)priorityBuffer[pixelIndex]
                << " underLayer=" << (int)underLayerBuffer[pixelIndex]
                << " objSemi=" << (int)objSemiTransparentBuffer[pixelIndex]
                << std::endl;
    }
  }

  // Optional: per-frame framebuffer hash (scanline 159 only).
  // Enable with: AIO_TRACE_PPU_FRAMEHASH=1
  // Optional range gating: AIO_TRACE_PPU_FRAMEHASH_START,
  // AIO_TRACE_PPU_FRAMEHASH_END (inclusive)
  if (scanline == 159) {
    static const bool traceFrameHash =
        (std::getenv("AIO_TRACE_PPU_FRAMEHASH") != nullptr);
    if (traceFrameHash) {
      static bool rangeParsed = false;
      static int startFrame = -1;
      static int endFrame = -1;

      if (!rangeParsed) {
        rangeParsed = true;
        if (const char *s = std::getenv("AIO_TRACE_PPU_FRAMEHASH_START")) {
          startFrame = std::atoi(s);
        }
        if (const char *s = std::getenv("AIO_TRACE_PPU_FRAMEHASH_END")) {
          endFrame = std::atoi(s);
        }
      }

      const int f = frameCount;
      const bool inRange = ((startFrame < 0 || f >= startFrame) &&
                            (endFrame < 0 || f <= endFrame));
      if (inRange) {
        // 64-bit FNV-1a over the ARGB framebuffer words.
        uint64_t h = 1469598103934665603ull;
        const size_t base = (size_t)0;
        const size_t len = (size_t)SCREEN_WIDTH * (size_t)SCREEN_HEIGHT;
        for (size_t i = base; i < base + len; ++i) {
          h ^= (uint64_t)backBuffer[i];
          h *= 1099511628211ull;
        }
        std::cout << "[PPU_HASH] frame=" << f << " hash=0x" << std::hex << h
                  << std::dec << std::endl;
      }
    }

    // Optional: dump frame(s) to PPM files for visual inspection.
    // Enable with: AIO_DUMP_PPU_FRAME_PPM=<frame> or
    // AIO_DUMP_PPU_FRAME_PPM=<start>-<end> or comma-separated list Optional:
    // AIO_DUMP_PPU_FRAME_PPM_PATH=/some/dir (default: /tmp)
    {
      static bool dumpParsed = false;
      static std::vector<int> dumpFrames;
      static std::set<int> dumpedFrames;
      static std::string dumpPath;

      if (!dumpParsed) {
        dumpParsed = true;
        if (const char *s = std::getenv("AIO_DUMP_PPU_FRAME_PPM")) {
          std::string spec(s);
          // Parse comma-separated list: "100,200,300" or range "100-200" or
          // single "100"
          size_t pos = 0;
          while (pos < spec.length()) {
            size_t comma = spec.find(',', pos);
            std::string token = (comma == std::string::npos)
                                    ? spec.substr(pos)
                                    : spec.substr(pos, comma - pos);

            // Check for range format "start-end"
            size_t dash = token.find('-');
            if (dash != std::string::npos && dash > 0 &&
                dash < token.length() - 1) {
              int start = std::atoi(token.substr(0, dash).c_str());
              int end = std::atoi(token.substr(dash + 1).c_str());
              for (int f = start; f <= end; f++) {
                dumpFrames.push_back(f);
              }
            } else {
              int frame = std::atoi(token.c_str());
              if (frame >= 0)
                dumpFrames.push_back(frame);
            }

            if (comma == std::string::npos)
              break;
            pos = comma + 1;
          }
        }
        if (const char *p = std::getenv("AIO_DUMP_PPU_FRAME_PPM_PATH")) {
          dumpPath = p;
        } else {
          dumpPath = "/tmp";
        }
      }

      // Check if current frame should be dumped
      bool shouldDump = false;
      for (int f : dumpFrames) {
        if (frameCount == f && dumpedFrames.find(f) == dumpedFrames.end()) {
          shouldDump = true;
          break;
        }
      }

      if (shouldDump) {
        dumpedFrames.insert(frameCount);
        const std::string file =
            dumpPath + "/ppu_frame_" + std::to_string(frameCount) + ".ppm";
        std::ofstream out(file, std::ios::binary);
        if (out.is_open()) {
          out << "P6\n" << SCREEN_WIDTH << " " << SCREEN_HEIGHT << "\n255\n";
          for (size_t i = 0; i < (size_t)SCREEN_WIDTH * (size_t)SCREEN_HEIGHT;
               ++i) {
            const uint32_t c = backBuffer[i];
            const unsigned char r = (unsigned char)((c >> 16) & 0xFF);
            const unsigned char g = (unsigned char)((c >> 8) & 0xFF);
            const unsigned char b = (unsigned char)(c & 0xFF);
            out.write((const char *)&r, 1);
            out.write((const char *)&g, 1);
            out.write((const char *)&b, 1);
          }
          out.close();
          std::cout << "[PPU_DUMP] wrote " << file << " (frame " << frameCount
                    << ")" << std::endl;
        } else {
          std::cout << "[PPU_DUMP] failed to open " << file << std::endl;
        }
      }
    }
  }
}

void PPU::RenderOBJ() {
  // Basic OBJ Rendering (No Affine/Rotation yet)
  // OAM is at 0x07000000 (1KB)
  // 128 Sprites, 8 bytes each

  // Debug: Log when sprite rendering occurs.
  // Enable with: AIO_TRACE_PPU_RENDEROBJ_CALLED=1
  static const bool renderObjCalledTrace =
      EnvTruthy(std::getenv("AIO_TRACE_PPU_RENDEROBJ_CALLED"));
  if (renderObjCalledTrace) {
    static int renderObjCalls = 0;
    if (renderObjCalls < 10) {
      renderObjCalls++;
      AIO::Emulator::Common::Logger::Instance().LogFmt(
          AIO::Emulator::Common::LogLevel::Info, "PPU",
          "[RenderOBJ_CALLED] frame=%llu scanline=%d", frameCount, scanline);
    }
  }

  // Optional: per-frame OBJ rendering stats to diagnose "sprites disappeared"
  // issues without dumping huge pixel traces.
  // Enable with: AIO_TRACE_PPU_OBJSTATS=1
  // Optional: AIO_TRACE_PPU_OBJSTATS_START / _END (frameCount bounds)
  static const bool objStatsEnabled =
      EnvTruthy(std::getenv("AIO_TRACE_PPU_OBJSTATS"));
  static const int objStatsStart = EnvInt("AIO_TRACE_PPU_OBJSTATS_START", 0);
  static const int objStatsEnd = EnvInt("AIO_TRACE_PPU_OBJSTATS_END", -1);
  static uint64_t objStatsFrame = UINT64_MAX;
  static uint64_t objStatsNonZero = 0;
  static uint64_t objStatsDrawn = 0;
  static uint64_t objStatsWinRejected = 0;
  static uint64_t objStatsPrioRejected = 0;
  if (objStatsEnabled) {
    if (objStatsFrame == UINT64_MAX) {
      objStatsFrame = frameCount;
    }
    if (objStatsFrame != frameCount) {
      const int prev = (int)objStatsFrame;
      if (prev >= objStatsStart && (objStatsEnd < 0 || prev <= objStatsEnd)) {
        std::cout << "[PPU_OBJSTATS] frameCount=" << prev
                  << " nonZero=" << objStatsNonZero
                  << " drawn=" << objStatsDrawn
                  << " winReject=" << objStatsWinRejected
                  << " prioReject=" << objStatsPrioRejected << std::endl;
      }
      objStatsFrame = frameCount;
      objStatsNonZero = 0;
      objStatsDrawn = 0;
      objStatsWinRejected = 0;
      objStatsPrioRejected = 0;
    }
  }

  const auto &objTraceCfg = GetObjPixelTraceConfig();
  static int objTraceFrame = -1;
  static int objTraceHitsThisFrame = 0;
  if (objTraceCfg.enabled && frameCount != objTraceFrame) {
    objTraceFrame = frameCount;
    objTraceHitsThisFrame = 0;
  }

  // Iterate backwards for priority (127 first, then 0 on top)
  for (int i = 127; i >= 0; --i) {
    uint32_t oamAddr = 0x07000000 + (i * 8);

    const uint8_t *oamData = memory.GetOAMData();
    const size_t oamSize = memory.GetOAMSize();
    const uint32_t oamOff = (oamAddr - 0x07000000u);
    uint16_t attr0 = ReadLE16(oamData, oamSize, oamOff);
    uint16_t attr1 = ReadLE16(oamData, oamSize, oamOff + 2);
    uint16_t attr2 = ReadLE16(oamData, oamSize, oamOff + 4);

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

    // Handle Y Wrapping (0-255)
    if (y > 160)
      y -= 256;

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

    // Check if scanline is within sprite bounds
    if (scanline >= y && scanline < y + boundHeight) {
      // Render this line of the sprite
      int x = attr1 & 0x1FF;
      if (x >= 256)
        x -= 512; // Sign extend 9-bit X

      int tileIndex = attr2 & 0x3FF;
      int priority = (attr2 >> 10) & 0x3;
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
        pa = (int16_t)memory.Read16(affineBase);
        pb = (int16_t)memory.Read16(affineBase + 8);
        pc = (int16_t)memory.Read16(affineBase + 16);
        pd = (int16_t)memory.Read16(affineBase + 24);
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
            const uint8_t *vramData = memory.GetVRAMData();
            const size_t vramSize = memory.GetVRAMSize();
            pixelTileAddr = tileBase + (uint32_t)tileNum * 32u +
                            (uint32_t)inTileY * 8u + (uint32_t)inTileX;
            pixelTileByte =
                ReadVram8(vramData, vramSize, pixelTileAddr - 0x06000000u);
            colorIndex = pixelTileByte;
          } else {
            const uint8_t *vramData = memory.GetVRAMData();
            const size_t vramSize = memory.GetVRAMSize();
            pixelTileAddr = tileBase + (uint32_t)tileNum * 32u +
                            (uint32_t)inTileY * 4u + (uint32_t)(inTileX / 2);
            pixelTileByte =
                ReadVram8(vramData, vramSize, pixelTileAddr - 0x06000000u);
            bool useHighNibble = (inTileX & 1) != 0;
            if (PpuSwap4bppNibbles()) {
              useHighNibble = !useHighNibble;
            }
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
            const uint8_t *vramData = memory.GetVRAMData();
            const size_t vramSize = memory.GetVRAMSize();
            pixelTileAddr = tileBase + (uint32_t)tileNum * 32u +
                            (uint32_t)inTileY * 8u + (uint32_t)inTileX;
            pixelTileByte =
                ReadVram8(vramData, vramSize, pixelTileAddr - 0x06000000u);
            colorIndex = pixelTileByte;
          } else {
            const uint8_t *vramData = memory.GetVRAMData();
            const size_t vramSize = memory.GetVRAMSize();
            pixelTileAddr = tileBase + (uint32_t)tileNum * 32u +
                            (uint32_t)inTileY * 4u + (uint32_t)(inTileX / 2);
            pixelTileByte =
                ReadVram8(vramData, vramSize, pixelTileAddr - 0x06000000u);
            bool useHighNibble = (inTileX & 1) != 0;
            if (PpuSwap4bppNibbles()) {
              useHighNibble = !useHighNibble;
            }
            colorIndex = useHighNibble ? ((pixelTileByte >> 4) & 0xF)
                                       : (pixelTileByte & 0xF);
          }
        }
        if (colorIndex != 0) {
          if (objStatsEnabled) {
            const int fc = (int)frameCount;
            if (fc >= objStatsStart && (objStatsEnd < 0 || fc <= objStatsEnd)) {
              objStatsNonZero++;
            }
          }
          // Check if OBJ layer is enabled by window settings at this pixel
          if (!IsLayerEnabledAtPixel(screenX, scanline, 4)) {
            if (objStatsEnabled) {
              const int fc = (int)frameCount;
              if (fc >= objStatsStart &&
                  (objStatsEnd < 0 || fc <= objStatsEnd)) {
                objStatsWinRejected++;
              }
            }
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
            if (objStatsEnabled) {
              const int fc = (int)frameCount;
              if (fc >= objStatsStart &&
                  (objStatsEnd < 0 || fc <= objStatsEnd)) {
                objStatsDrawn++;
              }
            }
            // Fetch Color (OBJ Palette starts at 0x05000200)
            uint32_t paletteAddr = 0x05000200;
            if (is8bpp) {
              paletteAddr += colorIndex * 2;
            } else {
              paletteAddr += (paletteBank * 32) + (colorIndex * 2);
            }

            const uint8_t *palData = memory.GetPaletteData();
            const size_t palSize = memory.GetPaletteSize();
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

            if (objTraceCfg.enabled &&
                objTraceHitsThisFrame < objTraceCfg.maxHits &&
                (objTraceCfg.frame < 0 || frameCount == objTraceCfg.frame) &&
                (objTraceCfg.y < 0 || scanline == objTraceCfg.y) &&
                (objTraceCfg.x < 0 || screenX == objTraceCfg.x)) {
              objTraceHitsThisFrame++;
              std::cout << "[PPU_OBJPIX] frame=" << frameCount
                        << " x=" << screenX << " y=" << scanline << " spr=" << i
                        << " attr0=0x" << std::hex << attr0 << " attr1=0x"
                        << attr1 << " attr2=0x" << attr2 << std::dec
                        << " map1D=" << (mapping1D ? 1 : 0)
                        << " 8bpp=" << (is8bpp ? 1 : 0)
                        << " baseTile=" << tileIndex << " tileNum=" << tileNum
                        << " inX=" << (spriteX % 8) << " inY=" << (spriteY % 8)
                        << " addr=0x" << std::hex << pixelTileAddr << std::dec
                        << " byte=0x" << std::hex << (int)pixelTileByte
                        << std::dec << " ci=" << (int)colorIndex << " pal=0x"
                        << std::hex << paletteAddr << std::dec << " out=0x"
                        << std::hex << backBuffer[pixelIndex] << std::dec
                        << std::endl;

              // Optional: dump the full 8x8 tile used for this pixel for visual
              // inspection. Enable with: AIO_DUMP_PPU_OBJTILE=1 Optional:
              // AIO_DUMP_PPU_OBJTILE_PATH=/some/dir (default: /tmp)
              static bool dumpedObjTile = false;
              if (!dumpedObjTile &&
                  EnvTruthy(std::getenv("AIO_DUMP_PPU_OBJTILE"))) {
                dumpedObjTile = true;
                std::string outDir = "/tmp";
                if (const char *p = std::getenv("AIO_DUMP_PPU_OBJTILE_PATH")) {
                  outDir = p;
                }

                const uint8_t *vramData = memory.GetVRAMData();
                const size_t vramSize = memory.GetVRAMSize();
                const uint8_t *palData2 = memory.GetPaletteData();
                const size_t palSize2 = memory.GetPaletteSize();

                const uint32_t tileDataBase =
                    0x06010000u + (uint32_t)tileNum * 32u;
                const std::string file =
                    outDir + "/obj_tile_f" + std::to_string(frameCount) + "_x" +
                    std::to_string(screenX) + "_y" + std::to_string(scanline) +
                    "_spr" + std::to_string(i) + "_tile" +
                    std::to_string(tileNum) + ".ppm";

                std::ofstream out(file, std::ios::binary);
                if (out.is_open()) {
                  out << "P6\n8 8\n255\n";
                  for (int ty2 = 0; ty2 < 8; ++ty2) {
                    for (int tx2 = 0; tx2 < 8; ++tx2) {
                      uint8_t ci2 = 0;
                      if (is8bpp) {
                        const uint32_t a =
                            tileDataBase + (uint32_t)ty2 * 8u + (uint32_t)tx2;
                        ci2 = ReadVram8(vramData, vramSize, a - 0x06000000u);
                      } else {
                        const uint32_t a = tileDataBase + (uint32_t)ty2 * 4u +
                                           (uint32_t)(tx2 / 2);
                        const uint8_t b2 =
                            ReadVram8(vramData, vramSize, a - 0x06000000u);
                        const bool useHigh = ((tx2 & 1) != 0);
                        ci2 = useHigh ? ((b2 >> 4) & 0xF) : (b2 & 0xF);
                      }

                      uint32_t palAddr2 = 0x05000200u;
                      if (is8bpp) {
                        palAddr2 += (uint32_t)ci2 * 2u;
                      } else {
                        palAddr2 +=
                            (uint32_t)paletteBank * 32u + (uint32_t)ci2 * 2u;
                      }
                      const uint16_t bgr =
                          ReadLE16(palData2, palSize2, palAddr2 - 0x05000000u);
                      const uint8_t rr = (uint8_t)((bgr & 0x1F) << 3);
                      const uint8_t gg = (uint8_t)(((bgr >> 5) & 0x1F) << 3);
                      const uint8_t bb = (uint8_t)(((bgr >> 10) & 0x1F) << 3);
                      out.write((const char *)&rr, 1);
                      out.write((const char *)&gg, 1);
                      out.write((const char *)&bb, 1);
                    }
                  }
                  out.close();
                  std::cout << "[PPU_OBJTILE_DUMP] wrote " << file << " base=0x"
                            << std::hex << tileDataBase << std::dec
                            << std::endl;
                } else {
                  std::cout << "[PPU_OBJTILE_DUMP] failed to open " << file
                            << std::endl;
                }
              }
            }
          } else {
            if (objStatsEnabled) {
              const int fc = (int)frameCount;
              if (fc >= objStatsStart &&
                  (objStatsEnd < 0 || fc <= objStatsEnd)) {
                objStatsPrioRejected++;
              }
            }
          }
        }
      }
    }
  }
}

void PPU::RenderMode2() {
  // Mode 2: Affine, BG2 and BG3
  uint16_t dispcnt = ReadRegister(0x00);

  // Get priorities for enabled BGs
  bool bg2Enabled = dispcnt & 0x0400;
  bool bg3Enabled = dispcnt & 0x0800;

  int bg2Priority = bg2Enabled ? (ReadRegister(0x0C) & 0x3) : 99;
  int bg3Priority = bg3Enabled ? (ReadRegister(0x0E) & 0x3) : 99;

  // Render in priority order: lower priority first, then higher (BG2 wins ties)
  if (bg2Priority > bg3Priority || (bg2Priority == bg3Priority && bg2Enabled)) {
    if (bg3Enabled)
      RenderAffineBackground(3);
    if (bg2Enabled)
      RenderAffineBackground(2);
  } else {
    if (bg2Enabled)
      RenderAffineBackground(2);
    if (bg3Enabled)
      RenderAffineBackground(3);
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
        uint8_t tileIndex = memory.Read8(mapAddr);

        // Fetch Pixel from Tile
        // 8bpp tiles are 64 bytes
        int inTileX = mapX % 8;
        int inTileY = mapY % 8;

        uint32_t tileAddr =
            tileBase + (tileIndex * 64) + (inTileY * 8) + inTileX;
        uint8_t colorIndex = memory.Read8(tileAddr);

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
            uint16_t color = memory.Read16(paletteAddr);

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
        const uint8_t tileIndex = memory.Read8(mapAddr);

        const int inTileX = mapX % 8;
        const int inTileY = mapY % 8;

        const uint32_t tileAddr = tileBase + (uint32_t)(tileIndex * 64) +
                                  (uint32_t)(inTileY * 8) + (uint32_t)inTileX;
        const uint8_t colorIndex = memory.Read8(tileAddr);

        if (colorIndex != 0) {
          if (!IsLayerEnabledAtPixel(x, scanline, bgIndex)) {
            continue;
          }

          const int pixelIndex = scanline * SCREEN_WIDTH + x;
          if (bgPriority <= priorityBuffer[pixelIndex]) {
            const uint32_t paletteAddr =
                0x05000000u + (uint32_t)colorIndex * 2u;
            const uint16_t color = memory.Read16(paletteAddr);

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
    uint16_t color = memory.Read16(addr);

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
    uint8_t colorIndex = memory.Read8(addr);

    // Bitmap modes do not have per-pixel transparency. Palette index 0 is a
    // valid color.
    const uint32_t paletteAddr = 0x05000000 + (uint32_t)colorIndex * 2u;
    const uint16_t color = memory.Read16(paletteAddr);

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
      uint16_t color = memory.Read16(addr);

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

void PPU::RenderMode1() {
  // Mode 1: Mixed Tiled mode
  // BG0, BG1 = Regular tiled (text mode)
  // BG2 = Affine/Rotation-Scaling
  // BG3 = Not available in Mode 1

  uint16_t dispcnt = ReadRegister(0x00);

  // Get priorities for enabled BGs
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

  // Sort by priority (descending) then by index (descending for same priority)
  for (int i = 0; i < 3; ++i) {
    for (int j = i + 1; j < 3; ++j) {
      if (bgs[i].priority < bgs[j].priority ||
          (bgs[i].priority == bgs[j].priority && bgs[i].index < bgs[j].index)) {
        BGInfo temp = bgs[i];
        bgs[i] = bgs[j];
        bgs[j] = temp;
      }
    }
  }

  // Render in sorted order (lowest priority first)
  for (int i = 0; i < 3; ++i) {
    if (bgs[i].enabled) {
      if (bgs[i].affine) {
        RenderAffineBackground(bgs[i].index);
      } else {
        RenderBackground(bgs[i].index);
      }
    }
  }
}

void PPU::RenderMode0() {
  // Mode 0: Tiled, BG0-BG3
  uint16_t dispcnt = ReadRegister(0x00);

  // DIAGNOSTIC: Force stderr output for debugging
  static const bool traceMode0 = EnvFlagCached("AIO_TRACE_NES_PALETTE");
  static int mode0Traces = 0;
  if (traceMode0 && mode0Traces < 5 && scanline == 0) {
    mode0Traces++;
    std::cerr << "[MODE0] frame=" << frameCount << " dispcnt=0x" << std::hex
              << dispcnt << " bgEnables=0x" << ((dispcnt >> 8) & 0xF)
              << std::dec << std::endl;
    std::cerr.flush();
  }

  if (TraceGbaSpam()) {
    static int renderMode0Count = 0;
    if (renderMode0Count < 5) {
      renderMode0Count++;
      std::cout << "[PPU RenderMode0] DISPCNT=0x" << std::hex << dispcnt
                << " BG enables: 0x" << ((dispcnt >> 8) & 0xF)
                << " scanline=" << std::dec << scanline << std::endl;
    }
  }

  // Render backgrounds from lowest priority to highest
  // BG priority is in bits 0-1 of BGxCNT (0 = highest, 3 = lowest)
  // When priorities are equal, lower BG number wins (BG0 > BG1 > BG2 > BG3)
  // So we render: priority 3 first, then 2, 1, 0
  // Within same priority: BG3 first, then BG2, BG1, BG0 (so BG0 wins on ties)

  // Get priorities for enabled BGs
  struct BGInfo {
    int index;
    int priority;
    bool enabled;
  };
  BGInfo bgs[4];

  for (int i = 0; i < 4; ++i) {
    bgs[i].index = i;
    bgs[i].enabled = dispcnt & (0x100 << i);
    if (bgs[i].enabled) {
      uint16_t bgcnt = ReadRegister(0x08 + (i * 2));
      bgs[i].priority = bgcnt & 0x3;
    } else {
      bgs[i].priority = 99; // Won't be rendered
    }
  }

  // Sort by priority (descending) then by index (descending for same priority)
  // This means we render lowest priority first, BG with higher index first for
  // ties
  for (int i = 0; i < 4; ++i) {
    for (int j = i + 1; j < 4; ++j) {
      if (bgs[i].priority < bgs[j].priority ||
          (bgs[i].priority == bgs[j].priority && bgs[i].index < bgs[j].index)) {
        BGInfo temp = bgs[i];
        bgs[i] = bgs[j];
        bgs[j] = temp;
      }
    }
  }

  // Render in sorted order (lowest priority first)
  for (int i = 0; i < 4; ++i) {
    if (bgs[i].enabled) {
      RenderBackground(bgs[i].index);
    }
  }
}

void PPU::RenderBackground(int bgIndex) {
  const uint16_t dispcnt = ReadRegister(0x00);
  const uint8_t bgMode = (uint8_t)(dispcnt & 0x7u);

  // DIAGNOSTIC: Trace which mode and BG are being rendered
  static const bool traceMode = EnvFlagCached("AIO_TRACE_NES_PALETTE");
  static int modeTraces = 0;
  if (traceMode && modeTraces < 10 && scanline == 0) {
    modeTraces++;
    // Write to stderr which isn't buffered
    std::cerr << "[BG_RENDER] mode=" << (int)bgMode << " bgIndex=" << bgIndex
              << " dispcnt=0x" << std::hex << dispcnt << std::dec << std::endl;
  }

  // NOTE: BG VRAM wrapping (mapOffset &= 0xFFFF) was attempted for modes 0-2
  // but broke SMA2. Removed for compatibility.
  (void)bgMode;

  uint16_t bgcnt = ReadRegister(0x08 + (bgIndex * 2));

  uint16_t bghofs = ReadRegister(0x10 + (bgIndex * 4)) & 0x01FF;
  uint16_t bgvofs = ReadRegister(0x12 + (bgIndex * 4)) & 0x01FF;

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
  bool is8bpp = (bgcnt >> 7) & 1; // Wait, bit 7 is 256 colors? Docs say Bit 7.
  // GBATEK: Bit 7 = 256 Colors/1 Palette (0=16/16, 1=256/1)

  // Screen Size (0-3)
  // 0: 256x256 (32x32 tiles)
  // 1: 512x256 (64x32 tiles)
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

  // NOTE: Previous charBase override for Classic NES was REMOVED.
  // Analysis showed the fix increased corruption (5588 -> 7232 cyan pixels).
  // The actual issue appears to be tile/tilemap VRAM overlap, not charBase.

  // OG-DK diagnostic: dump BG setup and tilemap entries once
  static bool ogdkDumpDone = false;
  if (classicNesMode && frameCount == 100 && !ogdkDumpDone && bgIndex == 0 &&
      scanline == 0) {
    ogdkDumpDone = true;
    const uint8_t *vramDataDiag = memory.GetVRAMData();
    const size_t vramSizeDiag = memory.GetVRAMSize();
    const uint8_t *palDataDiag = memory.GetPaletteData();

    // Write to file for clean output
    std::ofstream diagFile("/tmp/ogdk_tilemap.txt");
    if (diagFile.is_open()) {
      diagFile << "=== OGDK Tilemap Diagnostic (frame 100) ===\n";
      diagFile << "BGCNT=0x" << std::hex << bgcnt << std::dec
               << " charBase=" << charBaseBlock << " (0x" << std::hex
               << tileBase << ") screenBase=" << screenBaseBlock << " (0x"
               << mapBase << ")" << std::dec << "\nsize=" << screenSize << " ("
               << mapWidth << "x" << mapHeight << " tiles) 8bpp=" << is8bpp
               << " hofs=" << bghofs << " vofs=" << bgvofs << "\n\n";

      // Dump raw bytes of first row of tilemap
      diagFile << "Raw bytes of tilemap row 0 (64 bytes = 32 entries):\n";
      for (int i = 0; i < 64; i++) {
        uint32_t offset = (mapBase - vramBase) + i;
        uint8_t byte = ReadBgVram8(vramDataDiag, vramSizeDiag, offset);
        diagFile << std::hex << std::setw(2) << std::setfill('0') << (int)byte
                 << " ";
        if ((i + 1) % 16 == 0)
          diagFile << "\n";
      }
      diagFile << std::dec << std::setfill(' ') << "\n";

      // Dump tilemap entries with full hex values
      diagFile << "Tilemap entries (hex) at screenBase=" << screenBaseBlock
               << ":\n";
      for (int row = 0; row < 4; row++) {
        diagFile << "Row " << std::setw(2) << row << ": ";
        for (int col = 0; col < 16; col++) {
          uint32_t offset = (mapBase - vramBase) + (row * 32 + col) * 2;
          uint16_t entry = ReadBgVram16(vramDataDiag, vramSizeDiag, offset);
          diagFile << std::hex << std::setw(4) << std::setfill('0') << entry
                   << " ";
        }
        diagFile << "\n";
      }
      diagFile << std::dec << std::setfill(' ') << "\n";

      // Dump palette bank 0
      diagFile << "Palette bank 0 (16 colors):\n  ";
      for (int i = 0; i < 16; i++) {
        uint16_t col = palDataDiag[i * 2] | (palDataDiag[i * 2 + 1] << 8);
        diagFile << std::hex << std::setw(4) << std::setfill('0') << col << " ";
      }
      diagFile << std::dec << std::setfill(' ') << "\n";

      // Dump first few tile data blocks
      diagFile << "\nTile data samples:\n";
      int sampleTiles[] = {0, 247, 32, 14, 510, 436, 251, 65};
      for (int t : sampleTiles) {
        uint32_t tileOff = (tileBase - vramBase) + t * 32;
        diagFile << "Tile " << std::setw(3) << t << " @0x" << std::hex
                 << std::setw(4) << std::setfill('0') << tileOff << ": "
                 << std::dec;
        for (int i = 0; i < 8; i++) {
          diagFile << std::hex << std::setw(2) << std::setfill('0')
                   << (int)ReadBgVram8(vramDataDiag, vramSizeDiag, tileOff + i)
                   << " ";
        }
        diagFile << "...\n" << std::dec << std::setfill(' ');
      }

      diagFile.close();
      std::cout << "[OGDK] Diagnostic written to /tmp/ogdk_tilemap.txt\n";
    }
  }

  const uint8_t *vramData = memory.GetVRAMData();
  const size_t vramSize = memory.GetVRAMSize();
  const uint8_t *palData = memory.GetPaletteData();
  const size_t palSize = memory.GetPaletteSize();

  // Debug: One-time VRAM analysis for Classic NES (disabled in production)
  // Can be re-enabled for debugging tile/VRAM layout issues.
  // static bool dumpedVramAnalysis = false;
  // if (classicNesMode && !dumpedVramAnalysis && frameCount > 150 && bgIndex ==
  // 0 && scanline == 0) { ... }

  const auto &traceCfg = GetBgPixelTraceConfig();
  static int tracedFrame = -1;
  static bool tracedBg[4] = {false, false, false, false};
  if (traceCfg.enabled && frameCount != tracedFrame) {
    tracedFrame = frameCount;
    tracedBg[0] = tracedBg[1] = tracedBg[2] = tracedBg[3] = false;
  }
  const bool shouldTraceThisBg =
      traceCfg.enabled && !tracedBg[bgIndex] &&
      (traceCfg.frame < 0 || frameCount == traceCfg.frame) &&
      (traceCfg.y < 0 || scanline == traceCfg.y);

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

    // Calculate block offset
    int blockOffset = 0;
    switch (screenSize) {
    case 0: // 32x32, single block
      break;
    case 1: // 64x32, two horizontal blocks
      blockOffset = blockX * 2048;
      break;
    case 2: // 32x64, two vertical blocks
      // For ScreenSize=2 (32x64 tiles), the second vertical
      // screen block starts at screen base + 1 (not +2), i.e.
      // +0x800 bytes per 32x32 block.
      blockOffset = blockY * 2048;
      break;
    case 3: // 64x64, four blocks
      blockOffset = blockX * 2048 + blockY * 4096;
      break;
    }

    // Fetch Tile Map Entry
    // Each screen block is 32x32 entries (2KB = 2048 bytes)
    uint32_t mapAddr = mapBase + blockOffset + (ty * 32 + tx) * 2;
    uint32_t mapOffset = mapAddr - 0x06000000u;
    uint16_t tileEntry = ReadBgVram16(vramData, vramSize, mapOffset);

    int tileIndex = tileEntry & 0x3FF;

    // Classic NES Series: The NES-on-GBA wrapper uses tilemap entries where
    // bit 9 (0x200) indicates CHR bank 1 (NES has two 256-tile pattern tables).
    // With charBase=1 (tiles at 0x6004000), tiles 320+ overlap with the
    // tilemap region at 0x6006800 (screenBase=13).
    //
    // Analysis of raw tilemap data shows high bytes like 0x80, 0x00, 0x01, etc.
    // which when combined with low bytes in 16-bit LE form give indices > 255.
    // But the actual NES tile indices are in the LOW byte only.
    //
    // Fix: For Classic NES games, extract tile index from low 8 bits only,
    // and use bit 8 (0x100 in the 16-bit entry) for CHR bank selection.
    // This keeps tile indices 0-255 per bank, avoiding tilemap overlap.
    //
    // The high byte patterns suggest:
    // - Bit 7 (0x80) = possible CHR bank select or NES attribute flag
    // - Bits 4-6 = additional attributes
    // - Low byte = raw NES tile index (0-255)
    //
    // For now, we mask to 8 bits and ignore CHR bank 1 (which overlaps
    // tilemap).
    // TODO: Investigate if charBase=0 games use both banks correctly.
    if (classicNesMode) {
      // Use low 8 bits as tile index (NES tiles are 0-255)
      // High bits may indicate CHR bank but with current VRAM layout,
      // bank 1 tiles (256+) would overlap the tilemap region
      tileIndex = tileEntry & 0xFF;
    }

    bool hFlip = (tileEntry >> 10) & 1;
    bool vFlip = (tileEntry >> 11) & 1;
    int paletteBank = (tileEntry >> 12) & 0xF;

    // Classic NES Series palette handling:
    // The NES-on-GBA emulator only populates palette banks 0-7 via DMA.
    // However, it generates tilemap entries with bit 15 set (palBank=8+),
    // possibly as a flag or due to NES attribute table quirks.
    // We mask to 3 bits to use banks 0-7 where actual colors exist.
    // Additionally, within each bank, indices 0-7 are black/zeros - the
    // actual colors are at indices 8-15. The +8 offset (below) handles this.
    // Enabled automatically for Classic NES Series games (game code "FD*"),
    // or manually via AIO_CLASSIC_NES_PALETTE_FIX=1
    // Can be disabled via AIO_DISABLE_CLASSIC_NES_FIX=1 for testing
    static const bool envOverride =
        EnvFlagCached("AIO_CLASSIC_NES_PALETTE_FIX");
    static const bool envDisable = EnvFlagCached("AIO_DISABLE_CLASSIC_NES_FIX");
    const bool applyClassicNesPaletteOffset =
        !envDisable && (classicNesMode || envOverride);

    // DIAGNOSTIC: Check if classic NES mode is active
    static const bool traceClassicMode = EnvFlagCached("AIO_TRACE_NES_PALETTE");
    static int classicModeTraces = 0;
    if (traceClassicMode && classicModeTraces < 5 && scanline == 0 && x == 0) {
      classicModeTraces++;
      std::cout << "[NES_MODE] classicNesMode=" << classicNesMode
                << " envOverride=" << envOverride
                << " envDisable=" << envDisable
                << " applyOffset=" << applyClassicNesPaletteOffset << std::endl;
    }

    // Note: We no longer mask paletteBank - we override it to 0 for Classic NES
    // in the palette lookup section below.

    int inTileX = scrolledX % 8;
    int inTileY = scrolledY % 8;

    if (hFlip)
      inTileX = 7 - inTileX;
    if (vFlip)
      inTileY = 7 - inTileY;

    uint8_t colorIndex = 0;
    uint32_t tileAddr = 0;
    uint8_t tileByte = 0;

    // Classic NES tile resolver removed — rely on explicit BG VRAM wrapping
    // (`MapBgVramOffset` / `ReadBgVram*`) so behavior is spec-driven (modes 0-2
    // wrap within the 64KB BG VRAM window). Removing heuristics avoids ROM-
    // specific tile-base guessing and keeps behavior predictable.

    if (!is8bpp) {
      // 4bpp (16 colors)
      // 32 bytes per tile (8x8 pixels * 4 bits = 256 bits = 32 bytes)
      uint32_t tileStartOffset =
          (tileBase - 0x06000000u) + (uint32_t)tileIndex * 32u;
      tileAddr = 0x06000000u + tileStartOffset + (uint32_t)(inTileY * 4) +
                 (uint32_t)(inTileX / 2);

      // Bounds check: For Classic NES games, apply VRAM wrapping instead of
      // skipping. The NES emulator may intentionally use wrapped tile indices.
      // Normal GBA games can read from OBJ VRAM (per test expectations).
      // Note: We removed the aggressive skip here because it was causing
      // valid tile content to be hidden. VRAM wrapping is handled by the
      // ReadVram8 function which masks addresses to VRAM size.
      // (Classic NES bounds check disabled - relying on VRAM wrapping)

      uint32_t tileOffset = (tileAddr - 0x06000000u);
      tileByte = ReadBgVram8(vramData, vramSize, tileOffset);
      bool useHighNibble = (inTileX & 1) != 0;
      if (PpuSwap4bppNibbles()) {
        useHighNibble = !useHighNibble;
      }
      colorIndex = useHighNibble ? ((tileByte >> 4) & 0xF) : (tileByte & 0xF);

    } else {
      // 8bpp (256 colors)
      // 64 bytes per tile
      tileAddr = tileBase + (tileIndex * 64) + (inTileY * 8) + inTileX;

      // Note: Removed aggressive bounds check that was skipping valid tile
      // content. BG-specific VRAM wrapping is handled by ReadBgVram8.

      uint32_t tileOffset = tileAddr - 0x06000000u;
      tileByte = ReadBgVram8(vramData, vramSize, tileOffset);
      colorIndex = tileByte;
    }

    if (shouldTraceThisBg && (traceCfg.x < 0 || x == traceCfg.x)) {
      tracedBg[bgIndex] = true;
      std::cout << "[PPU_BGPIX] frame=" << frameCount << " bg=" << bgIndex
                << " x=" << x << " y=" << scanline << " BGCNT=0x" << std::hex
                << bgcnt << std::dec << " charBase=" << charBaseBlock
                << " screenBase=" << screenBaseBlock << " size=" << screenSize
                << " hofs=" << bghofs << " vofs=" << bgvofs
                << " scX=" << scrolledX << " scY=" << scrolledY << " mapAddr=0x"
                << std::hex << mapAddr << std::dec << " entry=0x" << std::hex
                << tileEntry << std::dec << " tile=" << tileIndex
                << " hFlip=" << (hFlip ? 1 : 0) << " vFlip=" << (vFlip ? 1 : 0)
                << " palBank=" << paletteBank << " inX=" << inTileX
                << " inY=" << inTileY << " tileAddr=0x" << std::hex << tileAddr
                << std::dec << " byte=0x" << std::hex << (int)tileByte
                << std::dec << " ci=" << (int)colorIndex << std::endl;
    }

    if (colorIndex != 0) { // Index 0 is transparent
      // Check if this BG layer is enabled by window settings at this pixel
      if (!IsLayerEnabledAtPixel(x, scanline, bgIndex)) {
        continue; // Window masks this layer at this position
      }

      // Only write if this BG has higher or equal priority (lower or equal
      // number) than what's already there We use <= because when priority is
      // the same, lower BG index wins (rendered later in sorted order)
      int pixelIndex = scanline * SCREEN_WIDTH + x;
      if (bgPriority <= priorityBuffer[pixelIndex]) {
        // Apply Classic NES Series palette handling
        uint8_t effectiveColorIndex = colorIndex;
        uint8_t effectivePaletteBank = paletteBank;
        // Classic NES Series palette handling:
        // The NES-on-GBA wrapper stores actual NES colors at palette bank 0,
        // indices 9-14. Tilemap entries may have palBank >= 8 for border/empty
        // areas (which should render black, as banks 1-15 are zeros).
        //
        // Algorithm:
        // - For normal tiles (palBank < 8): Apply +8 to colorIndex 1-6
        //   (so 1→9, 2→10, ..., 6→14) to read actual NES colors
        // - For colorIndex 7+: No offset (uses index directly)
        // - For border areas (palBank >= 8): Use bank 0 without offset
        //   (indices 0-8 are zeros → BLACK)

        if (applyClassicNesPaletteOffset && !is8bpp && colorIndex != 0) {
          // Always use palette bank 0 for Classic NES
          effectivePaletteBank = 0;

          if (paletteBank < 8 && colorIndex <= 6) {
            // Normal tiles: Apply +8 offset to map to actual colors
            effectiveColorIndex = colorIndex + 8;
          }
          // colorIndex 7+ or palBank >= 8: no offset (black/border)
        }

        // Fetch Color from Palette RAM
        // 0x05000000
        uint32_t paletteAddr = 0x05000000;
        if (!is8bpp) {
          paletteAddr +=
              (effectivePaletteBank * 32) + (effectiveColorIndex * 2);
        } else {
          paletteAddr += (colorIndex * 2);
        }
        uint16_t color = ReadLE16(palData, palSize, paletteAddr - 0x05000000u);

        // DIAGNOSTIC: Trace actual palette color reads for Classic NES
        // Only trace after frame 10 when palette should be loaded
        static const bool traceColor = EnvFlagCached("AIO_TRACE_NES_PALETTE");
        static int colorTraces = 0;
        if (traceColor && colorTraces < 50 && frameCount >= 10 &&
            scanline == 0 && x < 100 && colorIndex != 0) {
          colorTraces++;
          std::cout << "[NES_COLOR] frame=" << frameCount << " x=" << x
                    << " effIdx=" << (int)effectiveColorIndex << " palAddr=0x"
                    << std::hex << paletteAddr << " color=0x" << color
                    << std::dec << std::endl;
        }

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
  // IO Registers start at 0x04000000
  return memory.Read16(0x04000000 + offset);
}

const std::vector<uint32_t> &PPU::GetFramebuffer() const {
  std::lock_guard<std::mutex> lock(bufferMutex);
  return frontBuffer;
}

void PPU::SwapBuffers() {
  std::lock_guard<std::mutex> lock(bufferMutex);
  std::swap(frontBuffer, backBuffer);
  if (TraceGbaSpam()) {
    std::cout << "[SWAP] frame=" << frameCount << " front0=0x" << std::hex
              << frontBuffer[0] << std::dec << std::endl;
  }

  // DKC diagnostic: Log around problem frames + OAM analysis
  if (frameCount >= 1655 && frameCount <= 1665) {
    // Count non-zero sprites
    int activeSprites = 0;
    for (int i = 0; i < 128; i++) {
      uint16_t attr0 = memory.Read16(0x07000000 + i * 8);
      if ((attr0 & 0x300) != 0x200) { // Not disabled
        activeSprites++;
      }
    }
    std::cout << "[DKC_DIAG] Frame " << frameCount << " complete, "
              << activeSprites << " active sprites" << std::endl;
  }
  if (frameCount >= 1885 && frameCount <= 1895) {
    int activeSprites = 0;
    for (int i = 0; i < 128; i++) {
      uint16_t attr0 = memory.Read16(0x07000000 + i * 8);
      if ((attr0 & 0x300) != 0x200) {
        activeSprites++;
      }
    }
    std::cout << "[DKC_DIAG] Frame " << frameCount
              << " complete (banana load expected ~1890), " << activeSprites
              << " active sprites" << std::endl;
  }
  if (frameCount >= 2195 && frameCount <= 2205) {
    std::cout << "[DKC_DIAG] Frame " << frameCount
              << " complete (rope expected ~2200)" << std::endl;
    // Detailed OAM analysis for first 20 sprites
    for (int i = 0; i < 20; i++) {
      uint16_t attr0 = memory.Read16(0x07000000 + i * 8);
      uint16_t attr1 = memory.Read16(0x07000000 + i * 8 + 2);
      uint16_t attr2 = memory.Read16(0x07000000 + i * 8 + 4);
      if ((attr0 & 0x300) != 0x200) { // Not disabled
        int y = attr0 & 0xFF;
        int x = attr1 & 0x1FF;
        int tileIndex = attr2 & 0x3FF;
        int paletteNum = (attr2 >> 12) & 0xF;
        bool is8bpp = (attr0 >> 13) & 1;
        std::cout << "  [OAM] Sprite " << i << ": pos=(" << x << "," << y
                  << ") tile=" << tileIndex << " pal=" << paletteNum << " "
                  << (is8bpp ? "8bpp" : "4bpp") << std::endl;
      }
    }
    // Log OBJ palette 0
    std::cout << "  [PAL] OBJ Palette 0: ";
    for (int i = 0; i < 16; i++) {
      uint16_t color = memory.Read16(0x05000200 + i * 2);
      std::cout << std::hex << color << " " << std::dec;
    }
    std::cout << std::endl;
  }
  if (frameCount >= 2389 && frameCount <= 2399) {
    std::cout << "[DKC_DIAG] Frame " << frameCount
              << " complete (alligator expected ~2394)" << std::endl;
    // Detailed OAM analysis for first 20 sprites
    for (int i = 0; i < 20; i++) {
      uint16_t attr0 = memory.Read16(0x07000000 + i * 8);
      uint16_t attr1 = memory.Read16(0x07000000 + i * 8 + 2);
      uint16_t attr2 = memory.Read16(0x07000000 + i * 8 + 4);
      if ((attr0 & 0x300) != 0x200) { // Not disabled
        int y = attr0 & 0xFF;
        int x = attr1 & 0x1FF;
        int tileIndex = attr2 & 0x3FF;
        int paletteNum = (attr2 >> 12) & 0xF;
        bool is8bpp = (attr0 >> 13) & 1;
        std::cout << "  [OAM] Sprite " << i << ": pos=(" << x << "," << y
                  << ") tile=" << tileIndex << " pal=" << paletteNum << " "
                  << (is8bpp ? "8bpp" : "4bpp") << std::endl;
      }
    }
    // Log OBJ palette 0
    std::cout << "  [PAL] OBJ Palette 0: ";
    for (int i = 0; i < 16; i++) {
      uint16_t color = memory.Read16(0x05000200 + i * 2);
      std::cout << std::hex << color << " " << std::dec;
    }
    std::cout << std::endl;
  }
}

void PPU::RestoreFramebuffer(const std::vector<uint32_t> &buffer) {
  std::lock_guard<std::mutex> lock(bufferMutex);
  if (buffer.size() == frontBuffer.size()) {
    frontBuffer = buffer;
  }
}

void PPU::SetClassicNesMode(bool enabled) {
  classicNesMode = enabled;
  if (enabled) {
    std::cout << "[PPU] Classic NES Series mode enabled (palette bank "
                 "workaround active)"
              << std::endl;
  }
}

// Get window enable bits for a given pixel position
// Returns the enable mask (bits 0-3: BG0-3, bit 4: OBJ, bit 5: Color Effects)
uint8_t PPU::GetWindowMaskForPixel(int x, int y) {
  if (PpuIgnoreWindows()) {
    return 0x3F;
  }

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

  const uint8_t *oamData = memory.GetOAMData();
  const size_t oamSize = memory.GetOAMSize();
  const uint8_t *vramData = memory.GetVRAMData();
  const size_t vramSize = memory.GetVRAMSize();

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
    if (y > 160)
      y -= 256;

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
      pa = (int16_t)memory.Read16(affineBase);
      pb = (int16_t)memory.Read16(affineBase + 8);
      pc = (int16_t)memory.Read16(affineBase + 16);
      pd = (int16_t)memory.Read16(affineBase + 24);
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
  if (PpuDisableColorEffects()) {
    return;
  }

  // Read blend control registers
  uint16_t bldcnt = ReadRegister(0x50);   // BLDCNT
  uint16_t bldalpha = ReadRegister(0x52); // BLDALPHA
  uint16_t bldy = ReadRegister(0x54);     // BLDY

  int effectMode = (bldcnt >> 6) & 0x3;

  // Get the brightness coefficient (0-16, higher = more effect)
  int evyRaw = bldy & 0x1F;
  int evy = evyRaw;
  if (evy > 16)
    evy = 16;

  // Limited diagnostics to help track down fade issues (enable with
  // AIO_TRACE_GBA_SPAM)
  if (TraceGbaSpam() && (effectMode == 2 || effectMode == 3)) {
    static int fadeLogCount = 0;
    if (fadeLogCount < 100) {
      fadeLogCount++;
      std::cout << "[FADE] frame=" << frameCount << " scanline=" << scanline
                << " bldcnt=0x" << std::hex << bldcnt << std::dec
                << " bldy_raw=" << evyRaw << " bldy_clamped=" << evy
                << " mode=" << effectMode << std::endl;
    }
  }

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

  if (TraceGbaSpam()) {
    std::cout << "[FADE] firstTarget=0x" << std::hex << (int)firstTarget
              << " secondTarget=0x" << (int)secondTarget << std::dec
              << std::endl;
  }

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

    // Diagnostic: log first pixel's topLayer and current RGB when tracing
    // enabled
    if (TraceGbaSpam() && x == 0 && scanline == 0) {
      uint32_t color0 = backBuffer[pixelIndex];
      std::cout << "[FADEPIX] x=" << x << " scanline=" << scanline
                << " topLayer=" << (int)topLayer
                << " topFirst=" << (((firstTarget >> topLayer) & 1) != 0)
                << " color=0x" << std::hex << color0 << std::dec << std::endl;
    }

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

      if (TraceGbaSpam()) {
        std::cout << "[FADE_SEMITR] x=" << x << " topLayer=" << (int)topLayer
                  << " underLayer=" << (int)underLayer << " eva=" << eva
                  << " evb=" << evb << " under=0x" << std::hex << under
                  << std::dec << std::endl;
      }

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
      if (TraceGbaSpam()) {
        std::cout << "[FADEAPPLY] topFirst=" << topIsFirstTarget
                  << " before RGB=(" << (int)r << "," << (int)g << "," << (int)b
                  << ")\n";
      }
      r = from5(to5(r) + ((31 - to5(r)) * evy / 16));
      g = from5(to5(g) + ((31 - to5(g)) * evy / 16));
      b = from5(to5(b) + ((31 - to5(b)) * evy / 16));
      if (TraceGbaSpam()) {
        std::cout << "[FADEAPPLY] after RGB=(" << (int)r << "," << (int)g << ","
                  << (int)b << ")\n";
      }
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
