// DeterminismTests.cpp — cross-system save-state determinism harness.
//
// For each emulated system, verifies:
//   1. FrameDeterminism — two independent runs from the same initial ROM
//      produce identical framebuffer hashes and frame counts.
//   2. SaveStateDeterminism — running N steps, serializing state, running M
//      more steps, restoring state, then replaying M steps produces bit-
//      identical CPU PC and master-cycle counts.
//      This is the core contract of ISaveStateable / GB::State.
//
// No real ROM files are required. Each system uses the same minimal synthetic
// ROM that its per-subsystem unit tests already exercise, so failures here
// always point to the save-state machinery rather than ROM-format parsing.

#include "emulator/common/SaveState.h"
#include "emulator/genesis/Genesis.h"
#include "emulator/gb/GB.h"
#include "emulator/nes/NES.h"
#include "emulator/snes/SNES.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

using namespace AIO::Emulator::Common;
using AIO::Emulator::Genesis::Genesis;
using AIO::Emulator::NES::NES;
using AIO::Emulator::SNES::SNES;
using GBEmulator::GB;

// ── Test helpers ─────────────────────────────────────────────────────────────

namespace {

/// FNV-1a 64-bit hash over a raw pixel buffer.
/// Deterministic, fast, and collision-resistant enough for test assertions.
uint64_t FrameHash(const void* data, size_t bytes) {
    constexpr uint64_t kFnvBasis = 14695981039346656037ULL;
    constexpr uint64_t kFnvPrime = 1099511628211ULL;
    const auto* p = static_cast<const uint8_t*>(data);
    uint64_t h = kFnvBasis;
    for (size_t i = 0; i < bytes; ++i) {
        h ^= static_cast<uint64_t>(p[i]);
        h *= kFnvPrime;
    }
    return h;
}

// ── Synthetic ROM builders ────────────────────────────────────────────────────

/// Minimal iNES ROM: NROM mapper, 1× 16KB PRG, CHR RAM.
/// Contains a tight NOP+JMP loop so repeated Step() doesn't trap/crash.
std::vector<uint8_t> MakeNesRom() {
    std::vector<uint8_t> rom(16 + 16384, 0x00);
    // iNES header
    rom[0] = 'N'; rom[1] = 'E'; rom[2] = 'S'; rom[3] = 0x1A;
    rom[4] = 1;   // 1× 16KB PRG
    rom[5] = 0;   // CHR RAM
    // Program: NOP stream then JMP $8000 (infinite safe loop)
    for (int i = 0; i < 16000; ++i) rom[16 + i] = 0xEA; // NOP
    // JMP $8000 at offset 16000
    rom[16 + 16000] = 0x4C;
    rom[16 + 16001] = 0x00;
    rom[16 + 16002] = 0x80;
    // NROM: single 16KB bank mirrors to $8000–$BFFF AND $C000–$FFFF
    // Reset vector at $FFFC (offset 0x3FFC in PRG bank)
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = 0x80;
    // IRQ/BRK vector -> $8000 (safe landing)
    rom[16 + 0x3FFE] = 0x00;
    rom[16 + 0x3FFF] = 0x80;
    return rom;
}

/// Minimal SNES ROM: 1MB NOP stream with 65816 reset vector at $00:FFFC.
std::vector<uint8_t> MakeSnesRom() {
    std::vector<uint8_t> rom(1024 * 1024, 0xEA); // NOP (65816)
    // Reset vector at $FFFC -> $8000
    rom[0xFFFC] = 0x00;
    rom[0xFFFD] = 0x80;
    return rom;
}

/// Minimal Genesis ROM: 2MB filled with NOP (0x4E71), valid SSP and PC vectors.
std::vector<uint8_t> MakeGenesisRom() {
    std::vector<uint8_t> rom(2 * 1024 * 1024, 0x4E);
    for (size_t i = 1; i < rom.size(); i += 2) rom[i] = 0x71; // 0x4E71 NOP
    // SSP @ vector 0 = 0x00FF0000
    rom[0] = 0x00; rom[1] = 0xFF; rom[2] = 0x00; rom[3] = 0x00;
    // PC @ vector 1 = 0x00000200
    rom[4] = 0x00; rom[5] = 0x00; rom[6] = 0x02; rom[7] = 0x00;
    // HINT autovector (level 4, 0x70) -> 0x00000200
    rom[0x70] = 0x00; rom[0x71] = 0x00; rom[0x72] = 0x02; rom[0x73] = 0x00;
    // VINT autovector (level 6, 0x78) -> 0x00000200
    rom[0x78] = 0x00; rom[0x79] = 0x00; rom[0x7A] = 0x02; rom[0x7B] = 0x00;
    return rom;
}

/// Minimal GB ROM: 32 KB, simple cartridge type, and a safe JP $0100 loop.
std::vector<uint8_t> MakeGbRom() {
    std::vector<uint8_t> rom(0x8000, 0x00); // NOP-filled 32 KB ROM

    // Entry loop at 0x0100: JP 0x0100
    rom[0x0100] = 0xC3;
    rom[0x0101] = 0x00;
    rom[0x0102] = 0x01;

    // Header metadata used by cartridge detection.
    rom[0x0147] = 0x00; // ROM ONLY
    rom[0x0149] = 0x00; // No external RAM

    return rom;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// NES determinism
// ─────────────────────────────────────────────────────────────────────────────

class NESDeterminism : public ::testing::Test {
protected:
    const std::vector<uint8_t> rom_ = MakeNesRom();
};

/// Two independent NES instances loaded from the same ROM must produce the
/// same framebuffer hash after RunFrame.
TEST_F(NESDeterminism, FrameDeterminism) {
    NES a, b;
    a.Load(rom_);
    b.Load(rom_);

    a.RunFrame();
    b.RunFrame();

    constexpr size_t kNesPixels = 256 * 240; // visible NES frame
    const uint64_t ha = FrameHash(a.GetPPU().GetFramebuffer(), kNesPixels * 4);
    const uint64_t hb = FrameHash(b.GetPPU().GetFramebuffer(), kNesPixels * 4);

    EXPECT_EQ(ha, hb) << "NES framebuffer differs between two equivalent runs";
    EXPECT_EQ(a.GetPPU().FrameCount(), b.GetPPU().FrameCount());
}

/// Save state after 50 step()s, run 50 more, restore, run same 50 again.
/// PC and total master cycles must match the original second run.
TEST_F(NESDeterminism, SaveStateDeterminism) {
    NES nes;
    nes.Load(rom_);

    // Warmup: 50 steps
    for (int i = 0; i < 50; ++i) static_cast<void>(nes.Step());

    // Capture state
    SaveStateWriter w;
    nes.SaveState(w);
    const uint64_t cyclesAtSave = nes.GetTotalCycles();

    // Run 50 steps forward — record outcome
    for (int i = 0; i < 50; ++i) static_cast<void>(nes.Step());
    const uint64_t cyclesRun1 = nes.GetTotalCycles();
    const uint16_t pcRun1     = nes.GetCPU().GetPC();

    // Restore to save point and replay the same 50 steps
    SaveStateReader r(w.Buffer());
    nes.LoadState(r);
    ASSERT_EQ(nes.GetTotalCycles(), cyclesAtSave) << "LoadState did not restore cycle counter";

    for (int i = 0; i < 50; ++i) static_cast<void>(nes.Step());
    const uint64_t cyclesRun2 = nes.GetTotalCycles();
    const uint16_t pcRun2     = nes.GetCPU().GetPC();

    EXPECT_EQ(cyclesRun1, cyclesRun2) << "NES master cycles diverged after state restore";
    EXPECT_EQ(pcRun1, pcRun2)         << "NES CPU PC diverged after state restore";
}

// ─────────────────────────────────────────────────────────────────────────────
// SNES determinism
// ─────────────────────────────────────────────────────────────────────────────

class SNESDeterminism : public ::testing::Test {
protected:
    const std::vector<uint8_t> rom_ = MakeSnesRom();
};

TEST_F(SNESDeterminism, FrameDeterminism) {
    SNES a, b;
    a.Load(rom_);
    b.Load(rom_);

    a.RunFrame();
    b.RunFrame();

    // SNES visible: 256×224 RGBA8
    constexpr size_t kSnesBytes = 256 * 224 * 4;
    const uint64_t ha = FrameHash(a.GetPPU().GetFramebuffer(), kSnesBytes);
    const uint64_t hb = FrameHash(b.GetPPU().GetFramebuffer(), kSnesBytes);

    EXPECT_EQ(ha, hb) << "SNES framebuffer differs between two equivalent runs";
    EXPECT_EQ(a.GetPPU().FrameCount(), b.GetPPU().FrameCount());
}

TEST_F(SNESDeterminism, SaveStateDeterminism) {
    SNES snes;
    snes.Load(rom_);

    for (int i = 0; i < 50; ++i) static_cast<void>(snes.Step());

    SaveStateWriter w;
    snes.SaveState(w);
    const uint64_t cyclesAtSave = snes.GetMasterCycles();

    for (int i = 0; i < 50; ++i) static_cast<void>(snes.Step());
    const uint64_t cyclesRun1 = snes.GetMasterCycles();
    const uint16_t pcRun1     = snes.GetCPU().GetPC();

    SaveStateReader r(w.Buffer());
    snes.LoadState(r);
    ASSERT_EQ(snes.GetMasterCycles(), cyclesAtSave) << "SNES LoadState did not restore cycle counter";

    for (int i = 0; i < 50; ++i) static_cast<void>(snes.Step());
    const uint64_t cyclesRun2 = snes.GetMasterCycles();
    const uint16_t pcRun2     = snes.GetCPU().GetPC();

    EXPECT_EQ(cyclesRun1, cyclesRun2) << "SNES master cycles diverged after state restore";
    EXPECT_EQ(pcRun1, pcRun2)         << "SNES CPU PC diverged after state restore";
}

// ─────────────────────────────────────────────────────────────────────────────
// Genesis determinism
// ─────────────────────────────────────────────────────────────────────────────

class GenesisDeterminism : public ::testing::Test {
protected:
    const std::vector<uint8_t> rom_ = MakeGenesisRom();
};

TEST_F(GenesisDeterminism, FrameDeterminism) {
    Genesis a, b;
    a.Load(rom_);
    b.Load(rom_);

    a.RunFrame();
    b.RunFrame();

    // Genesis VDP: 320×224 RGBA8 (NTSC active region)
    constexpr size_t kGenesisBytes = 320 * 224 * 4;
    const uint64_t ha = FrameHash(a.GetVDP().GetFramebuffer(), kGenesisBytes);
    const uint64_t hb = FrameHash(b.GetVDP().GetFramebuffer(), kGenesisBytes);

    EXPECT_EQ(ha, hb) << "Genesis framebuffer differs between two equivalent runs";
    EXPECT_EQ(a.GetVDP().FrameCount(), b.GetVDP().FrameCount());
}

TEST_F(GenesisDeterminism, SaveStateDeterminism) {
    Genesis gen;
    gen.Load(rom_);

    for (int i = 0; i < 50; ++i) static_cast<void>(gen.Step());

    SaveStateWriter w;
    gen.SaveState(w);
    const uint64_t cyclesAtSave = gen.GetTotalCycles();

    for (int i = 0; i < 50; ++i) static_cast<void>(gen.Step());
    const uint64_t cyclesRun1 = gen.GetTotalCycles();
    const uint32_t pcRun1     = gen.GetCPU().GetPC();

    SaveStateReader r(w.Buffer());
    gen.LoadState(r);
    ASSERT_EQ(gen.GetTotalCycles(), cyclesAtSave) << "Genesis LoadState did not restore cycle counter";

    for (int i = 0; i < 50; ++i) static_cast<void>(gen.Step());
    const uint64_t cyclesRun2 = gen.GetTotalCycles();
    const uint32_t pcRun2     = gen.GetCPU().GetPC();

    EXPECT_EQ(cyclesRun1, cyclesRun2) << "Genesis master cycles diverged after state restore";
    EXPECT_EQ(pcRun1, pcRun2)         << "Genesis CPU PC diverged after state restore";
}

// ─────────────────────────────────────────────────────────────────────────────
// Game Boy determinism
// ─────────────────────────────────────────────────────────────────────────────

/// GB uses its own State struct rather than ISaveStateable, so we call the
/// GB-specific SaveState()/LoadState(const State&) API.
class GBDeterminism : public ::testing::Test {
protected:
    GB sys_;
    const std::vector<uint8_t> rom_ = MakeGbRom();
    void SetUp() override { sys_.Load(rom_); }
};

TEST_F(GBDeterminism, FrameDeterminism) {
    GB a, b;
    a.Load(rom_);
    b.Load(rom_);

    constexpr int kSteps = 20000;
    for (int i = 0; i < kSteps; ++i) static_cast<void>(a.Step());
    for (int i = 0; i < kSteps; ++i) static_cast<void>(b.Step());

    constexpr size_t kGBBytes =
        GBEmulator::GBPPU::kFramebufferWidth * GBEmulator::GBPPU::kFramebufferHeight * sizeof(uint32_t);

    const uint64_t ha = FrameHash(a.GetFramebuffer(), kGBBytes);
    const uint64_t hb = FrameHash(b.GetFramebuffer(), kGBBytes);

    EXPECT_EQ(ha, hb) << "GB framebuffer differs between two equivalent runs";
    EXPECT_EQ(a.GetFrameCount(), b.GetFrameCount());
}

TEST_F(GBDeterminism, SaveStateDeterminism) {
    for (int i = 0; i < 50; ++i) static_cast<void>(sys_.Step());

    const GB::State saved = sys_.SaveState();
    const uint32_t frameAtSave = sys_.GetFrameCount();

    for (int i = 0; i < 50; ++i) static_cast<void>(sys_.Step());
    const uint32_t frameRun1 = sys_.GetFrameCount();
    const uint16_t pcRun1 = sys_.GetCPU()->GetPC();

    sys_.LoadState(saved);
    ASSERT_EQ(sys_.GetFrameCount(), frameAtSave) << "GB LoadState did not restore frame count";

    for (int i = 0; i < 50; ++i) static_cast<void>(sys_.Step());
    const uint32_t frameRun2 = sys_.GetFrameCount();
    const uint16_t pcRun2 = sys_.GetCPU()->GetPC();

    EXPECT_EQ(frameRun1, frameRun2) << "GB frame count diverged after state restore";
    EXPECT_EQ(pcRun1, pcRun2) << "GB CPU PC diverged after state restore";
}
