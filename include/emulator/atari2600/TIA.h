#pragma once
// TIA — Television Interface Adaptor.
// Generates NTSC/PAL composite video and audio for the Atari 2600.
// Handles playfield, two player sprites, two missiles, ball, and two audio channels.

#include <cstdint>
#include <array>

namespace Atari2600 {

class TIA {
public:
    TIA() noexcept;
    ~TIA() = default;

    TIA(const TIA&)            = delete;
    TIA& operator=(const TIA&) = delete;

    // Called every color clock (3 per CPU cycle on NTSC)
    void Tick();

    // Returns true when a complete frame has been rendered
    bool IsFrameReady()          const noexcept { return frameReady_; }
    void ClearFrameReady()             noexcept { frameReady_ = false; }

    static constexpr int kWidth  = 160;
    static constexpr int kHeight = 192;

    const uint32_t* GetFramebuffer() const noexcept { return framebuffer_.data(); }

    void SetTrigger(int port, bool pressed) noexcept;

    void Reset()  noexcept;

    // CPU-facing register interface (addr 0x00–0x3F write; 0x00–0x0D read)
    uint8_t Read(uint16_t addr)              noexcept;
    void    Write(uint16_t addr, uint8_t v)  noexcept;

    // WSYNC signal: CPU calls this to stall until HBLANK completes
    void WSync() noexcept { wsyncRequested_ = true; }
    bool TakeWsync() noexcept {
        if (wsyncRequested_) { wsyncRequested_ = false; return true; }
        return false;
    }

private:
    // Timing constants (NTSC)
    static constexpr int kClocksPerScanline      = 228; // 68 HBLANK + 160 visible
    static constexpr int kScanlinesVisible        = 192;
    static constexpr int kScanlinesVSync          = 3;
    static constexpr int kScanlinesVBlank         = 37;
    static constexpr int kScanlinesOverscan       = 30;
    static constexpr int kTotalScanlines          = 262;
    static constexpr int kHBlankClocks            = 68;

    int  colorClock_  = 0;         // 0–227 per scanline
    int  scanline_    = 0;         // 0–261
    bool frameReady_  = false;
    bool wsyncRequested_ = false;
    bool vsync_       = false;
    bool vblank_      = false;

    // Write-only registers
    uint8_t  colupf_   = 0;  // 0x08 Playfield colour
    uint8_t  colup0_   = 0;  // 0x06 Player 0 colour
    uint8_t  colup1_   = 0;  // 0x07 Player 1 colour
    uint8_t  colubk_   = 0;  // 0x09 Background colour
    uint8_t  ctrlpf_   = 0;  // 0x0A Playfield control
    uint8_t  pf0_      = 0;  // 0x0D Playfield register 0 (bits 7–4)
    uint8_t  pf1_      = 0;  // 0x0E Playfield register 1 (bits 7–0)
    uint8_t  pf2_      = 0;  // 0x0F Playfield register 2 (bits 7–0)
    uint8_t  grp0_     = 0;  // 0x1B Player 0 graphics
    uint8_t  grp1_     = 0;  // 0x1C Player 1 graphics
    uint8_t  enam0_    = 0;  // 0x1D Missile 0 enable
    uint8_t  enam1_    = 0;  // 0x1E Missile 1 enable
    uint8_t  enabl_    = 0;  // 0x1F Ball enable
    uint8_t  refp0_    = 0;  // 0x0B Player 0 reflection
    uint8_t  refp1_    = 0;  // 0x0C Player 1 reflection
    uint8_t  resp0_    = 0;  // Player 0 X position (set via RESP0 strobe)
    uint8_t  resp1_    = 0;  // Player 1 X position
    uint8_t  resm0_    = 0;  // Missile 0 X position
    uint8_t  resm1_    = 0;  // Missile 1 X position
    uint8_t  resbl_    = 0;  // Ball X position
    uint8_t  nusiz0_   = 0;  // 0x04 Player 0 / missile 0 size
    uint8_t  nusiz1_   = 0;  // 0x05 Player 1 / missile 1 size
    uint8_t  ctrlbl_   = 0;  // 0x20 Ball control (size)
    uint8_t  hmp0_     = 0;  // 0x20 Player 0 horizontal motion
    uint8_t  hmp1_     = 0;  // 0x21 Player 1 horizontal motion
    uint8_t  hmm0_     = 0;  // 0x22 Missile 0 horizontal motion
    uint8_t  hmm1_     = 0;  // 0x23 Missile 1 horizontal motion
    uint8_t  hmbl_     = 0;  // 0x24 Ball horizontal motion
    uint8_t  vdelp0_   = 0;  // 0x25 Player 0 vertical delay
    uint8_t  vdelp1_   = 0;  // 0x26 Player 1 vertical delay
    uint8_t  vdelbl_   = 0;  // 0x27 Ball vertical delay
    uint8_t  grp0_old_ = 0;  // Previous player 0 graphics (for vertical delay)
    uint8_t  grp1_old_ = 0;

    // Collision latches (read-only, 0x00–0x07)
    uint8_t  cxm0p_  = 0;
    uint8_t  cxm1p_  = 0;
    uint8_t  cxp0fb_ = 0;
    uint8_t  cxp1fb_ = 0;
    uint8_t  cxm0fb_ = 0;
    uint8_t  cxm1fb_ = 0;
    uint8_t  cxblpf_ = 0;
    uint8_t  cxppmm_ = 0;
    uint8_t  inpt4_  = 0x80; // Trigger A (active-low from port)
    uint8_t  inpt5_  = 0x80; // Trigger B

    void RenderPixel() noexcept;
    bool PlayfieldBit(int x)  const noexcept;
    bool PlayerBit(int x, uint8_t grp, uint8_t pos, uint8_t nusiz, bool reflect) const noexcept;
    bool MissileBit(int x, uint8_t pos, uint8_t nusiz, bool ena) const noexcept;
    bool BallBit(int x, uint8_t pos, uint8_t ctrl, bool ena) const noexcept;

    // NTSC colour lookup (approximated as ARGB)
    static uint32_t NTSCColor(uint8_t colVal) noexcept;

    std::array<uint32_t, kWidth * kHeight> framebuffer_{};
};

} // namespace Atari2600
