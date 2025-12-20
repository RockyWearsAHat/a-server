#include <gtest/gtest.h>

#include "emulator/gba/ARM7TDMI.h"
#include "emulator/gba/GBAMemory.h"


using namespace AIO::Emulator::GBA;

TEST(BIOSTest, IRQVectorBranchesToTrampoline) {
    GBAMemory mem;

    // BIOS region is 0x00000000-0x00003FFF.
    // At 0x18, our HLE BIOS installs a branch to 0x180.
    // Bytes written: 0x58 0x00 0x00 0xEA => 0xEA000058.
    EXPECT_EQ(mem.Read32(0x00000018), 0xEA000058u);
}

TEST(BIOSTest, IRQTrampolineInstructionsPresent) {
    GBAMemory mem;

    // Verify the exact instruction words we install at 0x180.
    EXPECT_EQ(mem.Read32(0x00000180), 0xE92D500Fu); // STMDB SP!, {R0-R3,R12,LR}
    EXPECT_EQ(mem.Read32(0x00000184), 0xE3A00404u); // MOV R0, #0x04000000
    EXPECT_EQ(mem.Read32(0x00000188), 0xE28FE000u); // ADD LR, PC, #0
    EXPECT_EQ(mem.Read32(0x0000018C), 0xE510F004u); // LDR PC, [R0, #-4]
    EXPECT_EQ(mem.Read32(0x00000190), 0xE59F1008u); // LDR R1, [PC, #8]
    EXPECT_EQ(mem.Read32(0x00000194), 0xE1D110B0u); // LDRH R1, [R1]
    EXPECT_EQ(mem.Read32(0x00000198), 0xE1C012B2u); // STRH R1, [R0, #0x202]
    EXPECT_EQ(mem.Read32(0x0000019C), 0xE8BD500Fu); // LDMIA SP!, {R0-R3,R12,LR}
    EXPECT_EQ(mem.Read32(0x000001A0), 0xE25EF004u); // SUBS PC, LR, #4

    // Literal pool used by the trampoline.
    EXPECT_EQ(mem.Read32(0x000001A4), 0x03007FF4u);
}

TEST(BIOSTest, ResetInitializesIRQHandlerPointer) {
    GBAMemory mem;
    mem.Reset();

    // GBAMemory::Reset() initializes 0x03007FFC to point at 0x00003FF0 (dummy IRQ handler).
    EXPECT_EQ(mem.Read32(0x03007FFCu), 0x00003FF0u);
}

TEST(BIOSTest, ResetInitializesCommonIODefaults) {
    GBAMemory mem;
    mem.Reset();

    // KEYINPUT defaults to all released.
    EXPECT_EQ(mem.Read16(0x04000130u), 0x03FFu);

    // Interrupts disabled on boot.
    EXPECT_EQ(mem.Read16(0x04000200u), 0x0000u); // IE
    EXPECT_EQ(mem.Read16(0x04000208u), 0x0000u); // IME

    // SOUNDCNT_X master enable is set by HLE init.
    EXPECT_EQ(mem.Read8(0x04000084u) & 0x80u, 0x80u);

    // SOUNDBIAS defaults to 0x0200.
    EXPECT_EQ(mem.Read16(0x04000088u), 0x0200u);

    // POSTFLG is set by BIOS after boot.
    EXPECT_EQ(mem.Read8(0x04000300u), 0x01u);

    // WAITCNT is initialized by BIOS; common post-BIOS default is 0x4317.
    EXPECT_EQ(mem.Read16(0x04000204u), 0x4317u);
}

TEST(BIOSTest, WAITCNTAffectsGamePakTiming) {
    // Verify our timing model responds to WAITCNT and distinguishes sequential accesses.
    {
        GBAMemory mem;
        mem.Reset();
        mem.Write16(0x04000204u, 0x0000u);

        // WAITCNT=0 -> WS0 N=4, S=2 => first=1+4=5, next sequential=1+2=3.
        EXPECT_EQ(mem.GetAccessCycles(0x08000000u, 2), 5);
        EXPECT_EQ(mem.GetAccessCycles(0x08000002u, 2), 3);
    }

    {
        GBAMemory mem;
        mem.Reset();
        mem.Write16(0x04000204u, 0x4317u);

        // WAITCNT=0x4317 -> WS0 Ncode=1 (N=3), Sbit=1 (S=1) => first=1+3=4, seq=1+1=2.
        EXPECT_EQ(mem.GetAccessCycles(0x08000000u, 2), 4);
        EXPECT_EQ(mem.GetAccessCycles(0x08000002u, 2), 2);
    }
}

TEST(BIOSTest, BIOSReadOutsideBIOSReturnsOpenBus) {
    GBAMemory mem;
    ARM7TDMI cpu(mem);
    mem.SetCPU(&cpu);

    // Install a known instruction word at 0x08000000.
    // ROM is little-endian: word 0x11223344 => bytes 44 33 22 11.
    mem.LoadGamePak(std::vector<uint8_t>{0x44, 0x33, 0x22, 0x11});

    cpu.SetThumbMode(false);
    cpu.SetRegister(15, 0x08000000);

    // Reads from BIOS while executing from ROM should return open-bus data derived from the current fetch.
    EXPECT_EQ(mem.Read8(0x00000000u), 0x44u);
    EXPECT_EQ(mem.Read8(0x00000001u), 0x33u);
    EXPECT_EQ(mem.Read8(0x00000002u), 0x22u);
    EXPECT_EQ(mem.Read8(0x00000003u), 0x11u);
}

TEST(BIOSTest, CpuFastSetCopies32ByteBlocks) {
    GBAMemory mem;
    ARM7TDMI cpu(mem);
    mem.SetCPU(&cpu);

    // Thumb SWI 0x0C instruction placed in IWRAM.
    mem.Write16(0x03000000u, 0xDF0Cu);
    cpu.SetThumbMode(true);
    cpu.SetRegister(15, 0x03000000u);

    const uint32_t src = 0x02000000u;
    const uint32_t dst = 0x02000100u;

    // Source: 8 words (32 bytes).
    for (uint32_t i = 0; i < 8; ++i) {
        mem.Write32(src + i * 4, 0xA0B00000u + i);
        mem.Write32(dst + i * 4, 0x00000000u);
    }

    cpu.SetRegister(0, src);
    cpu.SetRegister(1, dst);
    cpu.SetRegister(2, 1u); // 1 block => 32 bytes => 8 words

    cpu.Step();

    for (uint32_t i = 0; i < 8; ++i) {
        EXPECT_EQ(mem.Read32(dst + i * 4), 0xA0B00000u + i);
    }
}

TEST(BIOSTest, CpuFastSetFixedSourceFillsBlocks) {
    GBAMemory mem;
    ARM7TDMI cpu(mem);
    mem.SetCPU(&cpu);

    mem.Write16(0x03000000u, 0xDF0Cu);
    cpu.SetThumbMode(true);
    cpu.SetRegister(15, 0x03000000u);

    const uint32_t src = 0x02000200u;
    const uint32_t dst = 0x02000300u;

    mem.Write32(src, 0xDEADBEEFu);
    for (uint32_t i = 0; i < 8; ++i) {
        mem.Write32(dst + i * 4, 0x00000000u);
    }

    cpu.SetRegister(0, src);
    cpu.SetRegister(1, dst);
    cpu.SetRegister(2, 1u | (1u << 24)); // fixed source, 1 block

    cpu.Step();

    for (uint32_t i = 0; i < 8; ++i) {
        EXPECT_EQ(mem.Read32(dst + i * 4), 0xDEADBEEFu);
    }
}
