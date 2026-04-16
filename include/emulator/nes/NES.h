#pragma once

#include "emulator/common/ISaveStateable.h"
#include "NESCartridge.h"
#include "NESMemory.h"
#include "RP2A03.h"
#include "PPU2C02.h"
#include "APU2A03.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>

namespace AIO::Emulator::NES {

/// Top-level NES emulator.
///
/// Composes:
///   - NESCartridge   (ROM loader + mapper)
///   - NESMemory      (CPU address bus)
///   - RP2A03         (6502-derived CPU)
///   - PPU2C02        (picture processing unit)
///   - APU2A03        (audio processing unit)
///
/// Clock ratios (NTSC):
///   Master clock:  21.477272 MHz
///   CPU  = master / 12  ≈ 1.789773 MHz
///   PPU  = master / 4   ≈ 5.369318 MHz  (3 PPU dots per CPU cycle)
///
/// Usage:
/// @code
///   NES nes;
///   nes.Load(romBytes);
///   nes.Reset();
///   while (running) {
///       nes.Step();
///       if (nes.GetPPU().FrameCount() > lastFrame) {
///           Blit(nes.GetPPU().GetFramebuffer());
///           lastFrame = nes.GetPPU().FrameCount();
///       }
///   }
/// @endcode
class NES : public AIO::Emulator::Common::ISaveStateable {
public:
    NES();
    ~NES() override = default;

    // ── ROM loading ───────────────────────────────────────────────────────

    /// Load a ROM from raw bytes. Resets the machine implicitly.
    /// @throws std::invalid_argument if the ROM is invalid.
    void Load(std::span<const uint8_t> romData);

    void Reset();

    // ── Execution ─────────────────────────────────────────────────────────

    /// Execute one CPU instruction and tick PPU/APU accordingly.
    /// @return CPU cycles consumed by this instruction.
    [[nodiscard]] int Step();

    /// Run until the PPU produces a complete new frame.
    void RunFrame();

    // ── Subsystem access ──────────────────────────────────────────────────

    [[nodiscard]] NESCartridge&  GetCartridge() noexcept { return *cart_; }
    [[nodiscard]] NESMemory&     GetMemory()    noexcept { return *mem_; }
    [[nodiscard]] RP2A03&        GetCPU()       noexcept { return *cpu_; }
    [[nodiscard]] PPU2C02&       GetPPU()       noexcept { return *ppu_; }
    [[nodiscard]] APU2A03&       GetAPU()       noexcept { return *apu_; }

    // ── Cycle counter ─────────────────────────────────────────────────────

    /// Total master-clock cycles since last Reset().
    [[nodiscard]] uint64_t GetTotalCycles() const noexcept { return totalMasterCycles_.load(); }

    // ── ISaveStateable ────────────────────────────────────────────────────

    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    std::unique_ptr<NESCartridge> cart_;
    std::unique_ptr<NESMemory>    mem_;
    std::unique_ptr<RP2A03>       cpu_;
    std::unique_ptr<PPU2C02>      ppu_;
    std::unique_ptr<APU2A03>      apu_;

    std::atomic<uint64_t> totalMasterCycles_{0};

    // OAM DMA is a $4014 write that stalls the CPU for 513/514 cycles
    // while the PPU's OAM is loaded from a CPU page.
    int oamDmaStallCycles_ = 0;

    void HandleOamDma(uint8_t page);
};

} // namespace AIO::Emulator::NES
