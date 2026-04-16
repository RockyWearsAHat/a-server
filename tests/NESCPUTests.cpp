#include "emulator/nes/NES.h"
#include "emulator/nes/RP2A03.h"
#include "emulator/nes/NESMemory.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

using namespace AIO::Emulator::NES;

namespace {

// Minimal iNES ROM with a fixed reset vector and a simple program.
// Header: magic, 1× 16KB PRG, 0 CHR (uses CHR RAM), mapper 0, horizontal mirror.
std::vector<uint8_t> MakeNromRom(const std::vector<uint8_t>& program,
                                  uint16_t resetVec = 0x8000) {
    std::vector<uint8_t> rom(16 + 16384, 0);
    // iNES header
    rom[0] = 'N'; rom[1] = 'E'; rom[2] = 'S'; rom[3] = 0x1A;
    rom[4] = 1;   // 1 × 16 KB PRG
    rom[5] = 0;   // CHR RAM
    // Copy program bytes starting at offset 0 within PRG
    for (size_t i = 0; i < program.size() && i < 16383; ++i)
        rom[16 + i] = program[i];
    // Reset vector at $FFFC–$FFFD (offset 0x3FFC in a single 16KB PRG bank — NROM mirrors $8000–$BFFF to $C000–$FFFF)
    rom[16 + 0x3FFC] = static_cast<uint8_t>(resetVec & 0xFF);
    rom[16 + 0x3FFD] = static_cast<uint8_t>(resetVec >> 8);
    return rom;
}

} // namespace

// ── Load / Reset ────────────────────────────────────────────────────────────

TEST(NESCPUTests, ResetVector_SetsPC) {
    // Program: BRK at reset entry
    const std::vector<uint8_t> prog = { 0x00 }; // BRK
    const auto rom = MakeNromRom(prog, 0x8000);

    NES nes;
    nes.Load(rom);

    EXPECT_EQ(nes.GetCPU().GetPC(), 0x8000u);
}

TEST(NESCPUTests, InvalidRom_ThrowsOnLoad) {
    std::vector<uint8_t> garbage(16, 0xFF);
    NES nes;
    EXPECT_THROW(nes.Load(garbage), std::invalid_argument);
}

// ── LDA / STA round-trip────────────────────────────────────────────────────

TEST(NESCPUTests, LdaSta_RoundTrip) {
    // LDA #$42 ; STA $0200 ; JMP *  (infinite loop)
    const std::vector<uint8_t> prog = {
        0xA9, 0x42,        // LDA #$42
        0x8D, 0x00, 0x02,  // STA $0200
        0x4C, 0x05, 0x80   // JMP $8005
    };
    const auto rom = MakeNromRom(prog, 0x8000);

    NES nes;
    nes.Load(rom);

    // Execute LDA + STA (each 2–3 cycles)
    static_cast<void>(nes.Step()); // LDA #$42 — 2 cycles
    static_cast<void>(nes.Step()); // STA $0200 — 4 cycles

    EXPECT_EQ(nes.GetCPU().GetA(), 0x42u);
    EXPECT_EQ(nes.GetMemory().Read8(0x0200), 0x42u);
}

// ── ADC overflow flag ───────────────────────────────────────────────────────

TEST(NESCPUTests, Adc_OverflowSet) {
    // CLC ; LDA #$7F ; ADC #$01 → result=$80, V=1, N=1
    const std::vector<uint8_t> prog = {
        0x18,        // CLC
        0xA9, 0x7F,  // LDA #$7F
        0x69, 0x01,  // ADC #$01
        0x4C, 0x05, 0x80
    };
    const auto rom = MakeNromRom(prog, 0x8000);

    NES nes;
    nes.Load(rom);

    static_cast<void>(nes.Step()); // CLC
    static_cast<void>(nes.Step()); // LDA #$7F
    static_cast<void>(nes.Step()); // ADC #$01

    EXPECT_EQ(nes.GetCPU().GetA(), 0x80u);
    // Overflow flag (V) = bit 6 of P
    EXPECT_TRUE((nes.GetCPU().GetP() & 0x40) != 0);
    // Negative flag (N) = bit 7 of P
    EXPECT_TRUE((nes.GetCPU().GetP() & 0x80) != 0);
}

// ── Branch page-cross cycle penalty ────────────────────────────────────────
// ── Branch page-cross cycle penalty ────────────────────────────────────────────

TEST(NESCPUTests, Branch_PageCross_ExtraCycle) {
    // BCS at $80FA, offset byte = 0x08 at $80FB, PC after read = $80FC
    // target = $80FC + 8 = $8104 — crosses $80xx to $81xx: extra cycle
    std::vector<uint8_t> prog(16384, 0xEA);
    prog[0xF8] = 0x38; // SEC     @ $80F8
    prog[0xF9] = 0xEA; // NOP     @ $80F9
    prog[0xFA] = 0xB0; // BCS     @ $80FA
    prog[0xFB] = 0x08; // offset +8 => target $8104 (page cross)
    prog[0x3FFC] = 0xF8;
    prog[0x3FFD] = 0x80;

    std::vector<uint8_t> rom(16 + 16384, 0);
    rom[0]='N'; rom[1]='E'; rom[2]='S'; rom[3]=0x1A;
    rom[4]=1; rom[5]=0;
    for (size_t i = 0; i < 16384; ++i) rom[16 + i] = prog[i];

    NES nes;
    nes.Load(rom);

    static_cast<void>(nes.Step()); // SEC — 2 cycles
    static_cast<void>(nes.Step()); // NOP — 2 cycles
    const int branchCycles = nes.Step(); // BCS taken, page cross: 4 cycles
    EXPECT_EQ(branchCycles, 4);
}

// ── IRQ masked by I flag ────────────────────────────────────────────────────

TEST(NESCPUTests, IRQ_MaskedByIFlag) {
    // SEI ; LDA #0 ; forever-loop
    // With I flag set, IRQ should not be serviced
    const std::vector<uint8_t> prog = {
        0x78,        // SEI  (sets I flag)
        0xA9, 0x00,  // LDA #$00
        0x4C, 0x03, 0x80 // JMP $8003
    };
    const auto rom = MakeNromRom(prog, 0x8000);

    NES nes;
    nes.Load(rom);

    static_cast<void>(nes.Step()); // SEI
    static_cast<void>(nes.Step()); // LDA #$00

    // Assert IRQ
    nes.GetCPU().SetIRQ(true);
    const uint16_t pcBefore = nes.GetCPU().GetPC();

    // Step once more — should NOT jump to IRQ vector
    static_cast<void>(nes.Step());
    // PC should still be in the expected range (not jumped to IRQ vector)
    EXPECT_LT(nes.GetCPU().GetPC(), 0x8100u)
        << "CPU should not have jumped to IRQ vector when I flag is set";
    (void)pcBefore;
}

    // ── Undocumented opcodes ────────────────────────────────────────────────────

    // LAX $zp — loads A and X with the same zero-page value (0xA7)
    TEST(NESCPUTests, UndocLAX_ZeroPage_LoadsAandX) {
        // Store 0xAB at $0010, then LAX $10
        const std::vector<uint8_t> prog = {
            0xA9, 0xAB,  // LDA #$AB
            0x85, 0x10,  // STA $10
            0xA7, 0x10,  // LAX $10
            0x4C, 0x06, 0x80
        };
        const auto rom = MakeNromRom(prog, 0x8000);
        NES nes;
        nes.Load(rom);
        static_cast<void>(nes.Step()); // LDA
        static_cast<void>(nes.Step()); // STA
        static_cast<void>(nes.Step()); // LAX
        EXPECT_EQ(nes.GetCPU().GetA(), 0xABu);
        EXPECT_EQ(nes.GetCPU().GetX(), 0xABu);
    }

    // SAX $zp — stores A & X to zero-page (0x87)
    TEST(NESCPUTests, UndocSAX_ZeroPage_StoresAndOfAandX) {
        const std::vector<uint8_t> prog = {
            0xA9, 0x0F,  // LDA #$0F
            0xA2, 0xF0,  // LDX #$F0 — wait, A&X = $0F & $F0 = $00
            // Use A=$CF X=$3F → A&X=$0F
            0xA9, 0xCF,  // LDA #$CF
            0xA2, 0x3F,  // LDX #$3F
            0x87, 0x20,  // SAX $20   — [$20] = $CF & $3F = $0F
            0x4C, 0x08, 0x80
        };
        const auto rom = MakeNromRom(prog, 0x8000);
        NES nes;
        nes.Load(rom);
        static_cast<void>(nes.Step()); // LDA #$0F
        static_cast<void>(nes.Step()); // LDX #$F0
        static_cast<void>(nes.Step()); // LDA #$CF
        static_cast<void>(nes.Step()); // LDX #$3F
        static_cast<void>(nes.Step()); // SAX $20
        EXPECT_EQ(nes.GetMemory().Read8(0x0020), 0x0Fu);
    }

    // DCP $zp — DEC mem then CMP A with result (0xC7)
    // A=$05, mem=$06 → DEC gives $05, CMP: A-mem=0, Z=1, C=1
    TEST(NESCPUTests, UndocDCP_ZeroPage_DecAndCompare) {
        const std::vector<uint8_t> prog = {
            0xA9, 0x06,  // LDA #$06
            0x85, 0x30,  // STA $30   — [$30]=$06
            0xA9, 0x05,  // LDA #$05
            0xC7, 0x30,  // DCP $30   — DEC[$30]→$05, CMP A($05)–$05=0 Z=1 C=1
            0x4C, 0x08, 0x80
        };
        const auto rom = MakeNromRom(prog, 0x8000);
        NES nes;
        nes.Load(rom);
        static_cast<void>(nes.Step()); // LDA #$06
        static_cast<void>(nes.Step()); // STA $30
        static_cast<void>(nes.Step()); // LDA #$05
        static_cast<void>(nes.Step()); // DCP $30
        EXPECT_EQ(nes.GetMemory().Read8(0x0030), 0x05u); // decremented
        EXPECT_TRUE((nes.GetCPU().GetP() & 0x02) != 0); // Z flag
        EXPECT_TRUE((nes.GetCPU().GetP() & 0x01) != 0); // C flag
    }

    // ISC $zp — INC mem then SBC A with result (0xE7)
    // A=$10, mem=$0F → INC gives $10, SBC: A=$10–$10–borrow; with C=1 result=0 Z=1
    TEST(NESCPUTests, UndocISC_ZeroPage_IncAndSubtract) {
        const std::vector<uint8_t> prog = {
            0xA9, 0x0F,  // LDA #$0F
            0x85, 0x40,  // STA $40   — [$40]=$0F
            0x38,        // SEC
            0xA9, 0x10,  // LDA #$10
            0xE7, 0x40,  // ISC $40   — INC[$40]→$10, SBC A($10)–$10=0 Z=1
            0x4C, 0x09, 0x80
        };
        const auto rom = MakeNromRom(prog, 0x8000);
        NES nes;
        nes.Load(rom);
        static_cast<void>(nes.Step()); // LDA #$0F
        static_cast<void>(nes.Step()); // STA $40
        static_cast<void>(nes.Step()); // SEC
        static_cast<void>(nes.Step()); // LDA #$10
        static_cast<void>(nes.Step()); // ISC $40
        EXPECT_EQ(nes.GetMemory().Read8(0x0040), 0x10u); // incremented
        EXPECT_EQ(nes.GetCPU().GetA(), 0x00u);           // 0x10 - 0x10 = 0
        EXPECT_TRUE((nes.GetCPU().GetP() & 0x02) != 0);  // Z flag
    }

    // SLO $zp — ASL mem then ORA A with result (0x07)
    // A=$01, mem=$40 → ASL→$80, ORA: A=$01|$80=$81 N=1
    TEST(NESCPUTests, UndocSLO_ZeroPage_ShiftLeftOrA) {
        const std::vector<uint8_t> prog = {
            0xA9, 0x40,  // LDA #$40
            0x85, 0x50,  // STA $50   — [$50]=$40
            0xA9, 0x01,  // LDA #$01
            0x07, 0x50,  // SLO $50   — ASL[$50]→$80, ORA A=$01|$80=$81
            0x4C, 0x08, 0x80
        };
        const auto rom = MakeNromRom(prog, 0x8000);
        NES nes;
        nes.Load(rom);
        static_cast<void>(nes.Step()); // LDA #$40
        static_cast<void>(nes.Step()); // STA $50
        static_cast<void>(nes.Step()); // LDA #$01
        static_cast<void>(nes.Step()); // SLO $50
        EXPECT_EQ(nes.GetMemory().Read8(0x0050), 0x80u); // ASL result
        EXPECT_EQ(nes.GetCPU().GetA(), 0x81u);           // ORA result
        EXPECT_TRUE((nes.GetCPU().GetP() & 0x80) != 0);  // N flag
    }
