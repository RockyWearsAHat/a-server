#pragma once
#include "emulator/common/ISaveStateable.h"
#include "emulator/genesis/GenesisCartridge.h"
#include "emulator/genesis/GenesisMemory.h"
#include "emulator/genesis/GenesisVDP.h"
#include "emulator/genesis/M68000.h"
#include "emulator/genesis/SN76489.h"
#include "emulator/genesis/YM2612.h"
#include "emulator/genesis/Z80.h"
#include <cstdint>
#include <span>

namespace AIO::Emulator::Genesis {

/// @brief Top-level Sega Genesis / Mega Drive emulator system.
///
/// Assembles and runs:
///   - Motorola MC68000 @ ~7.67 MHz (master / 7)
///   - Zilog Z80 @ ~3.58 MHz (master / 15)
///   - Sega 315-5313 VDP
///   - YM2612 FM synthesizer
///   - SN76489 PSG
///   - 64 KB main RAM, 8 KB Z80 RAM
///
/// @code
///   Genesis gen;
///   gen.Load(romData);          // map ROM, set reset vector
///   gen.Reset();                // assert RESET#, load vectors
///   gen.RunFrame();             // run one full NTSC frame
///   auto* fb = gen.GetVDP().GetFramebuffer();
/// @endcode
class Genesis : public AIO::Emulator::Common::ISaveStateable {
public:
    Genesis();
    ~Genesis() override = default;

    Genesis(const Genesis&)            = delete;
    Genesis& operator=(const Genesis&) = delete;

    /// Load a Mega Drive ROM image (binary).
    /// @throws std::runtime_error if the ROM is invalid.
    void Load(std::span<const uint8_t> romData);

    /// Assert the system RESET# line: reload all vector registers.
    void Reset();

    /// Execute one master-clock cycle worth of CPU work.
    /// Returns master-clock cycles consumed by the M68K step.
    [[nodiscard]] int Step();

    /// Execute exactly one complete NTSC video frame (262 scanlines).
    void RunFrame();

    // ── Component accessors ────────────────────────────────────────────────
    [[nodiscard]] GenesisCartridge& GetCartridge() noexcept { return cart_; }
    [[nodiscard]] GenesisMemory&    GetMemory()    noexcept { return mem_;  }
    [[nodiscard]] M68000&           GetCPU()       noexcept { return cpu_;  }
    [[nodiscard]] Z80&              GetZ80()       noexcept { return z80_;  }
    [[nodiscard]] GenesisVDP&       GetVDP()       noexcept { return vdp_;  }
    [[nodiscard]] YM2612&           GetYM()        noexcept { return ym_;   }
    [[nodiscard]] SN76489&          GetPSG()       noexcept { return psg_;  }

    [[nodiscard]] const GenesisCartridge& GetCartridge() const noexcept { return cart_; }
    [[nodiscard]] const GenesisMemory&    GetMemory()    const noexcept { return mem_;  }
    [[nodiscard]] const M68000&           GetCPU()       const noexcept { return cpu_;  }
    [[nodiscard]] const Z80&              GetZ80()       const noexcept { return z80_;  }
    [[nodiscard]] const GenesisVDP&       GetVDP()       const noexcept { return vdp_;  }
    [[nodiscard]] const YM2612&           GetYM()        const noexcept { return ym_;   }
    [[nodiscard]] const SN76489&          GetPSG()       const noexcept { return psg_;  }

    [[nodiscard]] uint64_t GetTotalCycles() const noexcept { return totalMasterCycles_; }

    // ISaveStateable
    void SaveState(AIO::Emulator::Common::SaveStateWriter& w) const override;
    void LoadState(AIO::Emulator::Common::SaveStateReader& r) override;

private:
    GenesisCartridge cart_;
    GenesisMemory    mem_;
    GenesisVDP       vdp_;
    YM2612           ym_;
    SN76489          psg_;
    M68000           cpu_;
    Z80              z80_;

    uint64_t totalMasterCycles_ {0};

    /// Master-clock cycle budget remaining in the current frame.
    int      frameCycleDebt_ {0};

    void WireInterrupts();
};

} // namespace AIO::Emulator::Genesis
