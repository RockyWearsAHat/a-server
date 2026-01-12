#include <gtest/gtest.h>

#include "emulator/gba/ARM7TDMI.h"
#include "emulator/gba/GBAMemory.h"

using namespace AIO::Emulator::GBA;

TEST(BIOSTest, IRQVectorBranchesToTrampoline) {
  GBAMemory mem;

  // BIOS region is 0x00000000-0x00003FFF.
  // At 0x18, our HLE BIOS installs a branch to the IRQ
  // trampoline in BIOS space. The current trampoline base
  // is 0x00003F00.
  constexpr uint32_t kIrqTrampolineBase = 0x00003F00u;

  // B immediate uses PC-relative: target = (pc+8) + (imm24<<2)
  // with pc=0x18. Compute the expected encoding here so this
  // test stays aligned with the documented layout.
  const uint32_t imm24 = (kIrqTrampolineBase - 0x00000020u) >> 2;
  const uint32_t expected = 0xEA000000u | (imm24 & 0x00FFFFFFu);
  EXPECT_EQ(mem.Read32(0x00000018u), expected);
}

TEST(BIOSTest, IRQTrampolineInstructionsPresent) {
  GBAMemory mem;

  // Verify the exact instruction words we install at the
  // IRQ trampoline base. These implement a small IRQ
  // dispatcher that:
  //  - saves volatile regs on SP_irq
  //  - switches to System mode so the user handler runs
  //    on the System stack
  //  - calls the handler at [0x03FFFFFC] (mirror of
  //    0x03007FFC)
  //  - switches back to IRQ mode
  //  - clears REG_IF using the mask at 0x03007FF4
  //  - restores regs and returns via SUBS PC, LR, #4

  constexpr uint32_t base = 0x00003F00u;

  EXPECT_EQ(mem.Read32(base + 0x00), 0xE92D500Fu); // STMDB SP!, {R0-R3,R12,LR}
  EXPECT_EQ(mem.Read32(base + 0x04), 0xE3A02404u); // MOV   R2, #0x04000000
  EXPECT_EQ(mem.Read32(base + 0x08), 0xE10F3000u); // MRS   R3, CPSR
  EXPECT_EQ(mem.Read32(base + 0x0C), 0xE3C3301Fu); // BIC   R3, R3, #0x1F
  EXPECT_EQ(mem.Read32(base + 0x10), 0xE383301Fu); // ORR   R3, R3, #0x1F (SYS)
  EXPECT_EQ(mem.Read32(base + 0x14), 0xE129F003u); // MSR   CPSR_c, R3

  EXPECT_EQ(mem.Read32(base + 0x18), 0xE28FE000u); // ADD   LR, PC, #0
  EXPECT_EQ(mem.Read32(base + 0x1C), 0xE512F004u); // LDR   PC, [R2, #-4]

  EXPECT_EQ(mem.Read32(base + 0x20), 0xE10F3000u); // MRS   R3, CPSR
  EXPECT_EQ(mem.Read32(base + 0x24), 0xE3C3301Fu); // BIC   R3, R3, #0x1F
  EXPECT_EQ(mem.Read32(base + 0x28), 0xE3833012u); // ORR   R3, R3, #0x12 (IRQ)
  EXPECT_EQ(mem.Read32(base + 0x2C), 0xE129F003u); // MSR   CPSR_c, R3

  EXPECT_EQ(mem.Read32(base + 0x30), 0xE3A02404u); // MOV   R2, #0x04000000
  EXPECT_EQ(mem.Read32(base + 0x34), 0xE59F1010u); // LDR   R1, [PC, #16]
  EXPECT_EQ(mem.Read32(base + 0x38), 0xE1D110B0u); // LDRH  R1, [R1]
  EXPECT_EQ(mem.Read32(base + 0x3C), 0xE2820F80u); // ADD   R0, R2, #0x200
  EXPECT_EQ(mem.Read32(base + 0x40), 0xE1C010B2u); // STRH  R1, [R0, #2]

  EXPECT_EQ(mem.Read32(base + 0x44), 0xE8BD500Fu); // LDMIA SP!, {R0-R3,R12,LR}
  EXPECT_EQ(mem.Read32(base + 0x48), 0xE25EF004u); // SUBS  PC, LR, #4

  // Literal pool used by the trampoline.
  EXPECT_EQ(mem.Read32(base + 0x4C), 0x03007FF4u);
}

TEST(BIOSTest, ResetInitializesIRQHandlerPointer) {
  GBAMemory mem;
  mem.Reset();

  // GBAMemory::Reset() initializes 0x03007FFC to point at 0x00003FF0 (dummy IRQ
  // handler).
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
  // Verify our timing model responds to WAITCNT and distinguishes sequential
  // accesses.
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

    // WAITCNT=0x4317 -> WS0 Ncode=1 (N=3), Sbit=1 (S=1) => first=1+3=4,
    // seq=1+1=2.
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

  // Reads from BIOS while executing from ROM should return open-bus data
  // derived from the current fetch.
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

TEST(BIOSTest, IRQReturnRestoresThumbState) {
  GBAMemory mem;
  mem.Reset();

  ARM7TDMI cpu(mem);
  mem.SetCPU(&cpu);

  // Two simple Thumb instructions at ROM base:
  // 0x08000000: MOVS r0, #0
  // 0x08000002: MOVS r0, #1
  mem.LoadGamePak(std::vector<uint8_t>{0x00, 0x20, 0x01, 0x20});

  cpu.SetThumbMode(true);
  cpu.SetRegister(15, 0x08000000u);

  // Enable VBlank IRQ (bit0), but don't mark it pending yet.
  mem.Write16(0x04000208u, 0x0001u); // IME
  mem.Write16(0x04000200u, 0x0001u); // IE

  // Execute one Thumb instruction so PC advances to the next halfword.
  cpu.Step();
  EXPECT_EQ(cpu.GetRegister(15), 0x08000002u);
  EXPECT_TRUE(cpu.IsThumbModeFlag());
  EXPECT_NE(cpu.GetCPSR() & 0x20u, 0u);

  // Mark IRQ pending (IF is write-1-to-clear on real hardware, so force it via
  // internal helper).
  mem.WriteIORegisterInternal(0x0202u, 0x0001u); // IF

  // Next Step() should take the IRQ before executing the next ROM instruction.
  cpu.Step();
  EXPECT_EQ(cpu.GetRegister(15),
            0x00003F00u); // BIOS IRQ trampoline after the vector branch
  EXPECT_FALSE(cpu.IsThumbModeFlag());
  EXPECT_EQ(cpu.GetCPSR() & 0x20u, 0u);

  // Run a bounded number of steps; BIOS trampoline should return to ROM
  // quickly.
  for (int i = 0; i < 64 && cpu.GetRegister(15) < 0x08000000u; ++i) {
    cpu.Step();
  }

  EXPECT_EQ(cpu.GetRegister(15), 0x08000002u);
  EXPECT_TRUE(cpu.IsThumbModeFlag());
  EXPECT_NE(cpu.GetCPSR() & 0x20u, 0u);
}
