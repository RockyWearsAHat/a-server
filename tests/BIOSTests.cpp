#include <gtest/gtest.h>

#include "emulator/gba/ARM7TDMI.h"
#include "emulator/gba/GBAMemory.h"

using namespace AIO::Emulator::GBA;

TEST(BIOSTest, IRQVectorBranchesToTrampoline) {
  GBAMemory mem;

  // BIOS region is 0x00000000-0x00003FFF.
  // At 0x18, we install a branch to our synthetic IRQ trampoline at 0x3F00.
  // target = (pc+8) + (imm24<<2) => imm24 = (0x3F00 - 0x20)>>2 = 0x0FB8
  EXPECT_EQ(mem.Read32(0x00000018), 0xEA000FB8u);
}

TEST(BIOSTest, IRQTrampolineInstructionsPresent) {
  GBAMemory mem;

  // Verify the exact instruction words we install at 0x3F00.
  EXPECT_EQ(mem.Read32(0x00003F00), 0xE92D500Fu); // STMDB SP!, {R0-R3,R12,LR}
  EXPECT_EQ(mem.Read32(0x00003F04), 0xE3A02404u); // MOV R2, #0x04000000
  EXPECT_EQ(mem.Read32(0x00003F08), 0xE28FE000u); // ADD LR, PC, #0
  EXPECT_EQ(mem.Read32(0x00003F0C), 0xE512F004u); // LDR PC, [R2, #-4]
  EXPECT_EQ(mem.Read32(0x00003F10), 0xE3A02404u); // MOV R2, #0x04000000
  EXPECT_EQ(mem.Read32(0x00003F14), 0xE59F1010u); // LDR R1, [PC, #16]
  EXPECT_EQ(mem.Read32(0x00003F18), 0xE1D110B0u); // LDRH R1, [R1]
  EXPECT_EQ(mem.Read32(0x00003F1C), 0xE2820F80u); // ADD R0, R2, #0x200
  EXPECT_EQ(mem.Read32(0x00003F20), 0xE1C010B2u); // STRH R1, [R0, #2]
  EXPECT_EQ(mem.Read32(0x00003F24), 0xE8BD500Fu); // LDMIA SP!, {R0-R3,R12,LR}
  EXPECT_EQ(mem.Read32(0x00003F28), 0xE25EF004u); // SUBS PC, LR, #4

  // Literal pool used by the trampoline.
  EXPECT_EQ(mem.Read32(0x00003F2C), 0x03007FF4u);
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

TEST(BIOSTest, IFHalfwordWriteIsWrite1ToClear) {
  GBAMemory mem;
  mem.Reset();

  // Force IF bit0 pending via internal helper.
  mem.WriteIORegisterInternal(0x0202u, 0x0001u);
  EXPECT_EQ(mem.Read16(0x04000202u) & 0x0001u, 0x0001u);

  // Acknowledge/clear by writing 1 (W1C semantics).
  mem.Write16(0x04000202u, 0x0001u);
  EXPECT_EQ(mem.Read16(0x04000202u) & 0x0001u, 0x0000u);
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
  cpu.SetRegister(2, 8u); // 8 words (32 bytes); count must be multiple of 8

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
  cpu.SetRegister(2, 8u | (1u << 24)); // fixed source, 8 words

  cpu.Step();

  for (uint32_t i = 0; i < 8; ++i) {
    EXPECT_EQ(mem.Read32(dst + i * 4), 0xDEADBEEFu);
  }
}

TEST(BIOSTest, LZ77UnCompWram_DecompressesSimpleStream) {
  GBAMemory mem;
  ARM7TDMI cpu(mem);
  mem.SetCPU(&cpu);

  // Thumb SWI 0x11 instruction placed in IWRAM.
  mem.Write16(0x03000000u, 0xDF11u);
  cpu.SetThumbMode(true);
  cpu.SetRegister(15, 0x03000000u);

  // Handcrafted LZ77 stream producing ASCII "ABCABC".
  // Header: 0x10 (LZ77), size=6.
  // Flags: 00010000b => 3 literals then 1 backref.
  // Backref: len=3 (nibble=0), offset=3 (stored as 2).
  const uint32_t src = 0x02000000u;
  const uint32_t dst = 0x02000100u;

  const uint8_t stream[] = {
      0x10, 0x06, 0x00, 0x00, // header
      0x10,                   // flags
      'A',  'B',  'C',        // literals
      0x00, 0x02,             // backref
  };
  for (uint32_t i = 0; i < sizeof(stream); ++i) {
    mem.Write8(src + i, stream[i]);
  }

  // Clear destination area.
  for (uint32_t i = 0; i < 16; ++i) {
    mem.Write8(dst + i, 0);
  }

  cpu.SetRegister(0, src);
  cpu.SetRegister(1, dst);
  cpu.SetRegister(2, 0);

  cpu.Step();

  EXPECT_EQ(mem.Read8(dst + 0), (uint8_t)'A');
  EXPECT_EQ(mem.Read8(dst + 1), (uint8_t)'B');
  EXPECT_EQ(mem.Read8(dst + 2), (uint8_t)'C');
  EXPECT_EQ(mem.Read8(dst + 3), (uint8_t)'A');
  EXPECT_EQ(mem.Read8(dst + 4), (uint8_t)'B');
  EXPECT_EQ(mem.Read8(dst + 5), (uint8_t)'C');
}

TEST(BIOSTest, BIOSDirect_VBlankIntrWait_WakesOnVBlankIRQ) {
  GBAMemory mem;
  ARM7TDMI cpu(mem);
  mem.SetCPU(&cpu);
  mem.Reset();

  // Direct-call BIOS VBlankIntrWait at 0x00000188.
  cpu.SetThumbMode(false);
  cpu.SetRegister(15, 0x00000188u);

  // Return to ROM after BIOS call.
  cpu.SetRegister(14, 0x08000000u);

  // First step: BIOS should enable VBlank IRQ in DISPSTAT, enable IME/IE,
  // and enter HALT waiting for VBlank.
  cpu.Step();

  // Emulate VBlank interrupt firing: set IF bit0.
  // IF is write-1-to-clear on hardware, so force it via the internal helper.
  // The CPU IRQ entry path will update BIOS_IF; the BIOS IntrWait routine then
  // observes BIOS_IF and returns to LR.
  mem.WriteIORegisterInternal(0x0202u, 0x0001u); // IF

  // Next step should wake from HALT, take IRQ, and execute the BIOS vector
  // branch into our IRQ trampoline.
  cpu.Step();
  EXPECT_EQ(cpu.GetRegister(15), 0x00003F00u);
  EXPECT_EQ(mem.Read16(0x03007FF8u) & 0x0001u, 0x0001u); // BIOS_IF

  // Step through the trampoline prologue and confirm it clears IF using the
  // triggered mask written by the CPU IRQ entry path.
  cpu.Step(); // 0x3F00
  EXPECT_EQ(cpu.GetRegister(15), 0x00003F04u);
  cpu.Step(); // 0x3F04
  EXPECT_EQ(cpu.GetRegister(15), 0x00003F08u);
  cpu.Step(); // 0x3F08: ADD LR, PC, #0
  EXPECT_EQ(cpu.GetRegister(15), 0x00003F0Cu);
  cpu.Step(); // 0x3F0C: LDR PC, [R2, #-4] -> handler
  EXPECT_EQ(cpu.GetRegister(15), 0x00003FF0u);
  cpu.Step(); // 0x3FF0: dummy handler returns to trampoline (LR)
  EXPECT_EQ(cpu.GetRegister(15), 0x00003F10u);

  // After the user handler returns, the trampoline acks IF based on 0x03007FF4.
  cpu.Step(); // 0x3F10
  EXPECT_EQ(cpu.GetRegister(15), 0x00003F14u);
  cpu.Step(); // 0x3F14: LDR R1, [PC, #16] -> &0x03007FF4
  EXPECT_EQ(cpu.GetRegister(15), 0x00003F18u);
  EXPECT_EQ(cpu.GetRegister(1), 0x03007FF4u);
  cpu.Step(); // 0x3F18: LDRH R1, [R1] -> triggered mask
  EXPECT_EQ(cpu.GetRegister(15), 0x00003F1Cu);
  EXPECT_EQ((uint16_t)cpu.GetRegister(1), 0x0001u);
  cpu.Step(); // 0x3F1C: ADD R0, R2, #0x200
  EXPECT_EQ(cpu.GetRegister(15), 0x00003F20u);
  cpu.Step(); // 0x3F20: STRH R1, [R0, #2] -> clear IF
  EXPECT_EQ(cpu.GetRegister(15), 0x00003F24u);
  EXPECT_EQ(mem.Read16(0x04000202u) & 0x0001u, 0x0000u);

  // Finish the trampoline, which should return from IRQ back to the BIOS
  // wait site (0x188).
  cpu.Step(); // 0x3F24: LDMIA SP!, {R0-R3, R12, LR}
  EXPECT_EQ(cpu.GetRegister(15), 0x00003F28u);
  cpu.Step(); // 0x3F28: SUBS PC, LR, #4 (IRQ return)
  EXPECT_EQ(cpu.GetRegister(15), 0x00000188u);

  // Now BIOS IntrWait should observe BIOS_IF and return to LR.
  cpu.Step();
  EXPECT_EQ(cpu.GetRegister(15), 0x08000000u);
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
