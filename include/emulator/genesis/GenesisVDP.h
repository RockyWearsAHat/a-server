#pragma once
#include "emulator/common/ISaveStateable.h"
#include <array>
#include <cstdint>
#include <functional>

namespace AIO::Emulator::Genesis {

/// @brief Sega 315-5313 VDP (Video Display Processor).
///
/// Responsible for all Genesis graphics output:
///   - Two scroll planes (Plane A and Plane B) with hardware H/V scrolling
///   - Sprite plane (up to 80 sprites in H40, 64 in H32)
///   - Window plane
///   - DMA engine (VRAM fill, VRAM-to-VRAM copy, 68K-to-VRAM transfer)
///   - H-interrupt and V-interrupt generation
///   - Shadow/Highlight color mode
///
/// The VDP exposes three M68K ports:
///   - DATA  port (0xC00000 / 0xC00002): 16-bit read/write
///   - CTRL  port (0xC00004 / 0xC00006): command word write, status read
///   - H/V   counter (0xC00008 / 0xC0000A): scanline/pixel position
///
/// Timing reference: Charles MacDonald "Genesis Hardware Notes" §VDP.
///
/// @code
///   GenesisVDP vdp;
///   vdp.SetVIntCallback([&](){ cpu.SetInterruptLevel(6); });
///   vdp.SetHIntCallback([&](){ cpu.SetInterruptLevel(4); });
///   vdp.Tick(7); // advance by 7 master-clock cycles
/// @endcode
class GenesisVDP : public AIO::Emulator::Common::ISaveStateable {
public:
    /// Framebuffer: 320×240 RGBA (or 256×240 in H32 mode).
    static constexpr int kFramebufferWidth  = 320;
    static constexpr int kFramebufferHeight = 240;
    static constexpr int kFramebufferPixels = kFramebufferWidth * kFramebufferHeight;

    GenesisVDP()  = default;
    ~GenesisVDP() override = default;

    GenesisVDP(const GenesisVDP&)            = delete;
    GenesisVDP& operator=(const GenesisVDP&) = delete;

    /// Advance the VDP by the given number of master-clock cycles.
    /// Fires H-interrupt and V-interrupt callbacks as appropriate.
    void Tick(uint32_t masterCycles);

    // ── M68K port interface ────────────────────────────────────────────────
    [[nodiscard]] uint16_t ReadData();
    void                   WriteData(uint16_t value);
    [[nodiscard]] uint16_t ReadStatus();
    void                   WriteCtrl(uint16_t value);
    [[nodiscard]] uint16_t ReadHVCounter() const noexcept;

    // ── Interrupt callbacks ────────────────────────────────────────────────
    using IntCallback = std::function<void()>;
    void SetVIntCallback(IntCallback cb) noexcept { vintCb_ = std::move(cb); }
    void SetHIntCallback(IntCallback cb) noexcept { hintCb_ = std::move(cb); }

    // ── Framebuffer access ─────────────────────────────────────────────────
    /// Returns RGBA pixel data: kFramebufferWidth × kFramebufferHeight × 4 bytes.
    [[nodiscard]] const uint8_t* GetFramebuffer() const noexcept {
        return framebuffer_.data();
    }

    [[nodiscard]] uint64_t FrameCount() const noexcept { return frameCount_; }

    // ── Debug / test access ────────────────────────────────────────────────
    [[nodiscard]] uint8_t  ReadVRAM (uint16_t addr) const noexcept { return vram_[addr]; }
    [[nodiscard]] uint16_t ReadCRAM (uint8_t  idx)  const noexcept;
    [[nodiscard]] uint16_t ReadVSRAM(uint8_t  idx)  const noexcept;
    [[nodiscard]] uint8_t  GetReg   (uint8_t  r)    const noexcept { return regs_[r & 0x1F]; }

    // ISaveStateable
    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    // ── Internal memory ────────────────────────────────────────────────────
    std::array<uint8_t,  0x10000> vram_  {};  ///< 64 KB VRAM
    std::array<uint16_t, 0x40>    cram_  {};  ///< 64 × 9-bit colour entries (stored as 16-bit)
    std::array<uint16_t, 0x28>    vsram_ {};  ///< 40 vertical-scroll entries

    std::array<uint8_t, 0x20>     regs_  {};  ///< 24 writable VDP registers (0–23)

    // ── Framebuffer ────────────────────────────────────────────────────────
    std::array<uint8_t, kFramebufferPixels * 4> framebuffer_ {};

    // ── VDP address / command state ────────────────────────────────────────
    uint32_t addrReg_    {0};      ///< Current VRAM/CRAM/VSRAM address
    uint8_t  addrCode_   {0};      ///< CD5–CD0 from last control word
    bool     firstWord_  {true};   ///< Are we waiting for the first control word?
    uint32_t ctrlLatch_  {0};      ///< Latched first control word

    // ── DMA state ─────────────────────────────────────────────────────────
    bool     dmaPending_ {false};
    uint32_t dmaSrc_     {0};
    uint16_t dmaLen_     {0};
    uint8_t  dmaType_    {0};  ///< 0=68K copy, 2=VRAM fill, 3=VRAM copy

    // ── Timing ────────────────────────────────────────────────────────────
    int      hCounter_   {0};   ///< Current pixel within scanline
    int      vCounter_   {0};   ///< Current scanline
    uint32_t cycleAcc_   {0};   ///< Accumulated master-clock cycles

    uint64_t frameCount_ {0};

    // ── Interrupt lines ────────────────────────────────────────────────────
    bool     vintPending_ {false};
    bool     hintPending_ {false};
    int      hintCounter_ {0};   ///< Down-counter for H-interrupt

    // ── Callbacks ─────────────────────────────────────────────────────────
    IntCallback vintCb_;
    IntCallback hintCb_;

    // ── Helpers ────────────────────────────────────────────────────────────
    void  RunScanline();
    void  RenderScanline(int line);
    void  RenderPlane (int line, bool planeA, int* pixels) const;
    void  RenderSprites(int line, int* pixels) const;
    void  CompositeOutput(int line, const int* bg, const int* fg, const int* sp);
    void  ExecuteDMA();
    [[nodiscard]] uint16_t CRAMToRGBA(uint16_t cramWord) const noexcept;
    void  AdvanceAddress() noexcept;
    [[nodiscard]] int ActiveWidth() const noexcept;
    [[nodiscard]] int ActiveLines() const noexcept;
    [[nodiscard]] int CyclesPerLine() const noexcept;
};

} // namespace AIO::Emulator::Genesis
