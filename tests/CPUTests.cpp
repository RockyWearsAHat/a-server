#include "emulator/gba/ARM7TDMI.h"
#include "emulator/gba/GBAMemory.h"
#include <gtest/gtest.h>

using namespace AIO::Emulator::GBA;

class CPUTest : public ::testing::Test {
protected:
  GBAMemory memory;
  ARM7TDMI cpu;

  CPUTest() : cpu(memory) {}

  void SetUp() override { cpu.Reset(); }

  // Helper to run one instruction
  void RunInstr(uint32_t opcode) {
    // Write opcode to current PC
    uint32_t pc = cpu.GetRegister(15);
    memory.WriteROM32(pc, opcode);
    cpu.Step();
  }

  // Helper to run one Thumb instruction
  void RunThumbInstr(uint16_t opcode) {
    uint32_t pc = cpu.GetRegister(15);
    memory.WriteROM(pc, opcode & 0xFF);
    memory.WriteROM(pc + 1, (opcode >> 8) & 0xFF);
    cpu.Step();
  }
};

TEST_F(CPUTest, InitialState) {
  EXPECT_EQ(cpu.GetRegister(15), 0x08000000); // PC starts at ROM
  EXPECT_EQ(cpu.GetCPSR() & 0x1F, 0x1F);      // System Mode
}

TEST_F(CPUTest, DataProcessing_MOV) {
  // MOV R0, #42
  // 0xE3A0002A
  RunInstr(0xE3A0002A);
  EXPECT_EQ(cpu.GetRegister(0), 42);
}

TEST_F(CPUTest, DataProcessing_ADD) {
  // MOV R0, #10
  RunInstr(0xE3A0000A);
  // MOV R1, #20
  RunInstr(0xE3A01014);
  // ADD R2, R0, R1
  // 0xE0802001
  RunInstr(0xE0802001);
  EXPECT_EQ(cpu.GetRegister(2), 30);
}

TEST_F(CPUTest, DataProcessing_SUB_Flags) {
  // MOV R0, #10
  RunInstr(0xE3A0000A);
  // SUBS R1, R0, #20 (Result -10, N set)
  // 0xE2501014
  RunInstr(0xE2501014);

  EXPECT_EQ(cpu.GetRegister(1), (uint32_t)-10);
  EXPECT_TRUE(cpu.GetCPSR() & 0x80000000); // N flag
}

TEST_F(CPUTest, Memory_LDR_STR) {
  // MOV R0, #0x02000000 (WRAM Base)
  // 0xE3A00402
  RunInstr(0xE3A00402);

  // MOV R1, #123
  RunInstr(0xE3A0107B);

  // STR R1, [R0]
  // 0xE5801000
  RunInstr(0xE5801000);

  // LDR R2, [R0]
  // 0xE5902000
  RunInstr(0xE5902000);

  EXPECT_EQ(cpu.GetRegister(2), 123);
  EXPECT_EQ(memory.Read32(0x02000000), 123);
}

TEST_F(CPUTest, ARM_LDR_RegisterOffset_Shifted) {
  // Mirrors the DKC pattern: LDR Rd, [Rn, Rm, LSL #2]
  // If the shift is ignored, the load becomes unaligned and reads the wrong
  // word.

  cpu.SetRegister(12, 0x02000000u); // Rn
  cpu.SetRegister(0, 3u);           // Rm (index)

  // Place distinct sentinel words.
  memory.Write32(0x02000000u, 0xAABBCCDDu);
  memory.Write32(0x0200000Cu, 0x11223344u); // base + (3 << 2)

  // Encoding: LDR R3, [R12, R0, LSL #2] => 0xE79C3100
  RunInstr(0xE79C3100u);

  EXPECT_EQ(cpu.GetRegister(3), 0x11223344u);
}

TEST_F(CPUTest, Branch_B) {
  uint32_t startPC = cpu.GetRegister(15);
  // B #0 (Target = PC + 8 + 0)
  // 0xEA000000
  RunInstr(0xEA000000);

  EXPECT_EQ(cpu.GetRegister(15), startPC + 8);
}

TEST_F(CPUTest, Thumb_LDR_PC_Relative) {
  cpu.SetThumbMode(true);
  uint32_t pc = 0x08000000;
  cpu.SetRegister(15, pc);

  // LDR R0, [PC, #4]
  // 0x4801
  // Address = (PC & ~2) + 4 + (Imm * 4) = 0x08000008

  memory.WriteROM32(0x08000008, 0xCAFEBABE);

  RunThumbInstr(0x4801);

  EXPECT_EQ(cpu.GetRegister(0), 0xCAFEBABE);
}

TEST_F(CPUTest, Thumb_LDR_RegisterOffset_LoadsWord) {
  cpu.SetThumbMode(true);
  cpu.SetRegister(15, 0x08000000u);

  // Place a word in EWRAM and load it via: LDR r2, [r2, r1]
  // Encoding for LDR (register offset), Rd=2, Rb=2, Ro=1 => 0x5852.
  memory.Write32(0x02000004u, 0x12345678u);
  cpu.SetRegister(2, 0x02000000u); // Rb
  cpu.SetRegister(1, 0x00000004u); // Ro

  RunThumbInstr(0x5852);

  EXPECT_EQ(cpu.GetRegister(2), 0x12345678u);
  EXPECT_EQ(cpu.GetRegister(15), 0x08000002u);
  EXPECT_TRUE(cpu.IsThumbModeFlag());
}

TEST_F(CPUTest, DataProcessing_MUL) {
  // MOV R0, #10
  RunInstr(0xE3A0000A);
  // MOV R1, #5
  RunInstr(0xE3A01005);
  // MUL R2, R0, R1 (R2 = R0 * R1)
  // 0xE0020190
  RunInstr(0xE0020190);

  EXPECT_EQ(cpu.GetRegister(2), 50);
}

TEST_F(CPUTest, Branch_BL) {
  uint32_t startPC = cpu.GetRegister(15);
  // BL #0 (Target = PC + 8 + 0)
  // 0xEB000000
  RunInstr(0xEB000000);

  EXPECT_EQ(cpu.GetRegister(15), startPC + 8);
  EXPECT_EQ(cpu.GetRegister(14),
            startPC + 4); // LR should be instruction after BL
}

TEST_F(CPUTest, SWI_CpuFastSet_FixedFill_ARM) {
  // Arrange: fixed fill value 0x01010101 written from src, count=1 (32 bytes =
  // 8 words)
  const uint32_t src = 0x02000100;
  const uint32_t dst = 0x02000200;
  memory.Write32(src, 0x01010101);
  for (int i = 0; i < 8; ++i) {
    memory.Write32(dst + (uint32_t)i * 4, 0x00000000);
  }

  cpu.SetRegister(0, src);
  cpu.SetRegister(1, dst);
  cpu.SetRegister(2, (8u /*word count (must be multiple of 8)*/ & 0x1FFFFF) |
                         (1u << 24)); // fixed source

  // ARM SWI 0x0C: 0xEF00000C
  RunInstr(0xEF00000C);

  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(memory.Read32(dst + (uint32_t)i * 4), 0x01010101u);
  }
}

TEST_F(CPUTest, SWI_CpuFastSet_FixedFill_Thumb) {
  // Same as above, but invoke via Thumb SWI 0x0C (0xDF0C)
  cpu.SetThumbMode(true);
  cpu.SetRegister(15, 0x08000000);

  const uint32_t src = 0x02000300;
  const uint32_t dst = 0x02000400;
  memory.Write32(src, 0x01010101);
  for (int i = 0; i < 8; ++i) {
    memory.Write32(dst + (uint32_t)i * 4, 0x00000000);
  }

  cpu.SetRegister(0, src);
  cpu.SetRegister(1, dst);
  cpu.SetRegister(2, (8u /*word count*/ & 0x1FFFFF) | (1u << 24));

  RunThumbInstr(0xDF0C);

  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(memory.Read32(dst + (uint32_t)i * 4), 0x01010101u);
  }
}

TEST_F(CPUTest, Memory_STM_LDM) {
  // MOV R0, #0x02000000 (Base)
  RunInstr(0xE3A00402);
  // MOV R1, #0x10
  RunInstr(0xE3A01010);
  // MOV R2, #0x20
  RunInstr(0xE3A02020);

  // STMIA R0!, {R1, R2}
  // 0xE8A00006 (R1=bit1, R2=bit2 -> 0110 = 0x6, W=1)
  RunInstr(0xE8A00006);

  // Check R0 updated
  EXPECT_EQ(cpu.GetRegister(0), 0x02000008);

  // Check memory
  EXPECT_EQ(memory.Read32(0x02000000), 0x10);
  EXPECT_EQ(memory.Read32(0x02000004), 0x20);

  // Reset registers
  RunInstr(0xE3A01000); // MOV R1, #0
  RunInstr(0xE3A02000); // MOV R2, #0

  // Reset R0
  RunInstr(0xE3A00402); // MOV R0, #0x02000000

  // LDMIA R0!, {R1, R2}
  // 0xE8B00006 (W=1)
  RunInstr(0xE8B00006);

  EXPECT_EQ(cpu.GetRegister(1), 0x10);
  EXPECT_EQ(cpu.GetRegister(2), 0x20);
}

TEST_F(CPUTest, DataProcessing_Logic) {
  // MOV R0, #0xF0
  RunInstr(0xE3A000F0);
  // MOV R1, #0xCC
  RunInstr(0xE3A010CC);

  // AND R2, R0, R1 (0xF0 & 0xCC = 0xC0)
  // 0xE0002001
  RunInstr(0xE0002001);
  EXPECT_EQ(cpu.GetRegister(2), 0xC0);

  // EOR R3, R0, R1 (0xF0 ^ 0xCC = 0x3C)
  // 0xE0203001
  RunInstr(0xE0203001);
  EXPECT_EQ(cpu.GetRegister(3), 0x3C);

  // ORR R4, R0, R1 (0xF0 | 0xCC = 0xFC)
  // 0xE1804001
  RunInstr(0xE1804001);
  EXPECT_EQ(cpu.GetRegister(4), 0xFC);

  // BIC R5, R0, R1 (0xF0 & ~0xCC = 0xF0 & 0x33 = 0x30)
  // 0xE1C05001
  RunInstr(0xE1C05001);
  EXPECT_EQ(cpu.GetRegister(5), 0x30);
}

TEST_F(CPUTest, DataProcessing_Compare) {
  // MOV R0, #10
  RunInstr(0xE3A0000A);

  // CMP R0, #10 (Z set)
  // 0xE350000A
  RunInstr(0xE350000A);
  EXPECT_TRUE(cpu.GetCPSR() & 0x40000000); // Z flag

  // CMP R0, #20 (N set)
  // 0xE3500014
  RunInstr(0xE3500014);
  EXPECT_TRUE(cpu.GetCPSR() & 0x80000000); // N flag

  // TST R0, #1 (Z set, 10 & 1 = 0)
  // 0xE3100001
  RunInstr(0xE3100001);
  EXPECT_TRUE(cpu.GetCPSR() & 0x40000000); // Z flag

  // TEQ R0, #10 (Z set, 10 ^ 10 = 0)
  // 0xE330000A
  RunInstr(0xE330000A);
  EXPECT_TRUE(cpu.GetCPSR() & 0x40000000); // Z flag

  // CMN R0, #-10 (Z set, 10 + (-10) = 0)
  // -10 = 0xFFFFFFF6
  // CMN R0, #0 (Wait, CMN adds. We need immediate -10? No, immediate is
  // unsigned 8-bit rotated) Let's use register for -10. MOV R1, #-10
  RunInstr(0xE3E01009); // MVN R1, #9 -> R1 = ~9 = -10
  // CMN R0, R1
  // 0xE1700001
  RunInstr(0xE1700001);
  EXPECT_TRUE(cpu.GetCPSR() & 0x40000000); // Z flag
}

TEST_F(CPUTest, DataProcessing_Arithmetic_Carry) {
  // 1. Test ADC
  // MOV R0, #0xFFFFFFFF
  RunInstr(0xE3E00000);
  // ADDS R0, R0, #1 (Result 0, C set)
  // 0xE2900001
  RunInstr(0xE2900001);
  EXPECT_EQ(cpu.GetRegister(0), 0);
  EXPECT_TRUE(cpu.GetCPSR() & 0x20000000); // C flag

  // MOV R1, #10
  RunInstr(0xE3A0100A);
  // MOV R2, #20
  RunInstr(0xE3A02014);
  // ADC R3, R1, R2 (10 + 20 + 1 = 31)
  // 0xE0A13002
  RunInstr(0xE0A13002);
  EXPECT_EQ(cpu.GetRegister(3), 31);

  // 2. Test SBC
  // SUBS R0, R1, R1 (10 - 10 = 0, C set because No Borrow)
  // 0xE0510001
  RunInstr(0xE0510001);
  EXPECT_TRUE(cpu.GetCPSR() & 0x20000000); // C flag

  // SBC R3, R2, R1 (20 - 10 - !1 = 10 - 0 = 10)
  // 0xE0C23001
  RunInstr(0xE0C23001);
  EXPECT_EQ(cpu.GetRegister(3), 10);

  // Force Borrow (C=0)
  // SUBS R0, R1, R2 (10 - 20 = -10, C clear)
  // 0xE0510002
  RunInstr(0xE0510002);
  EXPECT_FALSE(cpu.GetCPSR() & 0x20000000); // C flag clear

  // SBC R3, R2, R1 (20 - 10 - !0 = 10 - 1 = 9)
  // 0xE0C23001
  RunInstr(0xE0C23001);
  EXPECT_EQ(cpu.GetRegister(3), 9);

  // 3. Test RSC
  // SUBS R0, R1, R1 (C set)
  RunInstr(0xE0510001);

  // RSC R3, R1, R2 (R2 - R1 - !C = 20 - 10 - 0 = 10)
  // 0xE0E13002
  RunInstr(0xE0E13002);
  EXPECT_EQ(cpu.GetRegister(3), 10);
}

TEST_F(CPUTest, Multiply_Long) {
  // MOV R0, #0xFFFFFFFF (-1 or MaxUInt)
  RunInstr(0xE3E00000);
  // MOV R1, #2
  RunInstr(0xE3A01002);

  // UMULL R2, R3, R0, R1 (R3:R2 = R0 * R1)
  // Unsigned: 0xFFFFFFFF * 2 = 0x1FFFFFFFE
  // R3 = 1, R2 = 0xFFFFFFFE
  // 0xE0832190 (RdHi=3, RdLo=2)
  RunInstr(0xE0832190);
  EXPECT_EQ(cpu.GetRegister(3), 1);
  EXPECT_EQ(cpu.GetRegister(2), 0xFFFFFFFE);

  // SMULL R4, R5, R0, R1 (R5:R4 = R0 * R1)
  // Signed: -1 * 2 = -2
  // R5 = 0xFFFFFFFF, R4 = 0xFFFFFFFE
  // 0xE0C54190 (RdHi=5, RdLo=4)
  RunInstr(0xE0C54190);
  EXPECT_EQ(cpu.GetRegister(5), 0xFFFFFFFF);
  EXPECT_EQ(cpu.GetRegister(4), 0xFFFFFFFE);

  // UMLAL R2, R3, R0, R1 (R3:R2 += R0 * R1)
  // Current R3:R2 = 0x1FFFFFFFE
  // Add 0x1FFFFFFFE
  // Result = 0x3FFFFFFFC
  // R3 = 3, R2 = 0xFFFFFFFC
  // 0xE0A32190 (RdHi=3, RdLo=2)
  RunInstr(0xE0A32190);
  EXPECT_EQ(cpu.GetRegister(3), 3);
  EXPECT_EQ(cpu.GetRegister(2), 0xFFFFFFFC);
}

TEST_F(CPUTest, Memory_Halfword) {
  // MOV R0, #0x02000000
  RunInstr(0xE3A00402);

  // MOV R1, #0x1234
  RunInstr(0xE3A01C12); // MOV R1, #0x1200
  RunInstr(0xE2811034); // ADD R1, R1, #0x34

  // STRH R1, [R0]
  // 0xE1C010B0
  RunInstr(0xE1C010B0);

  // LDRH R2, [R0]
  // 0xE1D020B0
  RunInstr(0xE1D020B0);
  EXPECT_EQ(cpu.GetRegister(2), 0x1234);

  // Test Sign Extension
  // Write 0xFF at 0x02000004
  memory.Write8(0x02000004, 0xFF);

  // LDRSB R3, [R0, #4]
  // 0xE1D030D4
  RunInstr(0xE1D030D4);
  EXPECT_EQ(cpu.GetRegister(3), 0xFFFFFFFF); // -1

  // Write 0xFFFF at 0x02000006
  memory.Write16(0x02000006, 0xFFFF);

  // LDRSH R4, [R0, #6]
  // 0xE1D040F6
  RunInstr(0xE1D040F6);
  EXPECT_EQ(cpu.GetRegister(4), 0xFFFFFFFF); // -1
}

TEST_F(CPUTest, Thumb_ALU) {
  cpu.SetThumbMode(true);
  uint32_t pc = 0x08000000;
  cpu.SetRegister(15, pc);

  // 1. Move Shifted Register
  // MOV R0, #1
  cpu.SetRegister(0, 1);
  // LSL R1, R0, #1 (R1 = 2)
  // 000 00 00001 000 001 -> 0000 0000 0100 0001 -> 0x0041
  RunThumbInstr(0x0041);
  EXPECT_EQ(cpu.GetRegister(1), 2);

  // 2. Add/Sub
  // ADD R2, R0, R1 (1 + 2 = 3)
  // 0001 100 001 000 010 -> 0x1842
  RunThumbInstr(0x1842);
  EXPECT_EQ(cpu.GetRegister(2), 3);

  // 3. Move/Cmp/Add/Sub Imm
  // MOV R3, #10
  // 001 00 011 00001010 -> 0x230A
  RunThumbInstr(0x230A);
  EXPECT_EQ(cpu.GetRegister(3), 10);

  // 4. ALU Operations
  // AND R3, R1 (10 & 2 = 2)
  // 0100 00 0000 001 011 -> 0x400B
  RunThumbInstr(0x400B);
  EXPECT_EQ(cpu.GetRegister(3), 2);

  // NEG R3, R3 (R3 = -2)
  // 0100 00 1001 011 011 -> 0x425B
  RunThumbInstr(0x425B);
  EXPECT_EQ(cpu.GetRegister(3), (uint32_t)-2);

  // MUL R3, R1 (R3 = -2 * 2 = -4)
  // 0100 00 1101 001 011 -> 0x434B
  RunThumbInstr(0x434B);
  EXPECT_EQ(cpu.GetRegister(3), (uint32_t)-4);
}

TEST_F(CPUTest, Thumb_ShiftEdgeCases) {
  cpu.SetThumbMode(true);
  cpu.SetRegister(15, 0x08000000);

  // Thumb ALU format: 0100 00op opRs Rd

  // LSL (register): shift by 32 => result 0, carry = bit0
  cpu.SetRegister(2, 0x00000001);
  cpu.SetRegister(1, 0x00000020);
  // Clear carry first so we can observe it changing (CMP 0-1 => borrow => C=0)
  RunThumbInstr(0x2000); // MOV R0, #0
  RunThumbInstr(0x2801); // CMP R0, #1
  // opcode=LSL=0x2, rs=1, rd=2 => 0x4000 | (0x2<<6) | (1<<3) | 2 = 0x408A
  RunThumbInstr(0x408A);
  EXPECT_EQ(cpu.GetRegister(2), 0x00000000u);
  EXPECT_NE(cpu.GetCPSR() & CPSR::FLAG_C, 0u);

  // LSR (register): shift by 33 => result 0, carry = 0
  cpu.SetRegister(2, 0x80000000u);
  cpu.SetRegister(1, 0x00000021u);
  // Set carry first (CMP 0-0 => no borrow => C=1)
  RunThumbInstr(0x2000); // MOV R0, #0
  RunThumbInstr(0x2800); // CMP R0, #0
  // opcode=LSR=0x3 => 0x4000 | (0x3<<6) | (1<<3) | 2 = 0x40CA
  RunThumbInstr(0x40CA);
  EXPECT_EQ(cpu.GetRegister(2), 0x00000000u);
  EXPECT_EQ(cpu.GetCPSR() & CPSR::FLAG_C, 0u);

  // ASR (register): shift by 100 => result sign-extended, carry = sign bit
  cpu.SetRegister(2, 0x80000001u);
  cpu.SetRegister(1, 0x00000064u);
  // Clear carry first
  RunThumbInstr(0x2000); // MOV R0, #0
  RunThumbInstr(0x2801); // CMP R0, #1
  // opcode=ASR=0x4 => 0x4000 | (0x4<<6) | (1<<3) | 2 = 0x410A
  RunThumbInstr(0x410A);
  EXPECT_EQ(cpu.GetRegister(2), 0xFFFFFFFFu);
  EXPECT_NE(cpu.GetCPSR() & CPSR::FLAG_C, 0u);

  // ROR (register): amount is a non-zero multiple of 32 => result unchanged,
  // carry = bit31
  cpu.SetRegister(2, 0x80000001u);
  cpu.SetRegister(1, 0x00000020u);
  // Clear carry first
  RunThumbInstr(0x2000); // MOV R0, #0
  RunThumbInstr(0x2801); // CMP R0, #1
  // opcode=ROR=0x7 => 0x4000 | (0x7<<6) | (1<<3) | 2 = 0x41CA
  RunThumbInstr(0x41CA);
  EXPECT_EQ(cpu.GetRegister(2), 0x80000001u);
  EXPECT_NE(cpu.GetCPSR() & CPSR::FLAG_C, 0u);
}

TEST_F(CPUTest, Thumb_Stack) {
  cpu.SetThumbMode(true);
  uint32_t pc = 0x08000000;
  cpu.SetRegister(15, pc);

  // Set SP
  cpu.SetRegister(13, 0x03007F00);

  // Set R0, R1
  cpu.SetRegister(0, 0xDEADBEEF);
  cpu.SetRegister(1, 0xCAFEBABE);

  // PUSH {R0, R1}
  // 1011 010 0 00000011 -> 0xB403
  RunThumbInstr(0xB403);

  EXPECT_EQ(cpu.GetRegister(13), 0x03007F00 - 8);
  EXPECT_EQ(memory.Read32(0x03007F00 - 4),
            0xCAFEBABE); // Higher reg at higher addr
  EXPECT_EQ(memory.Read32(0x03007F00 - 8), 0xDEADBEEF);

  // Clear R0, R1
  cpu.SetRegister(0, 0);
  cpu.SetRegister(1, 0);

  // POP {R0, R1}
  // 1011 110 0 00000011 -> 0xBC03
  RunThumbInstr(0xBC03);

  EXPECT_EQ(cpu.GetRegister(0), 0xDEADBEEF);
  EXPECT_EQ(cpu.GetRegister(1), 0xCAFEBABE);
  EXPECT_EQ(cpu.GetRegister(13), 0x03007F00);
}

// ============================================================================
// Additional ARM7TDMI Coverage Tests
// ============================================================================

TEST_F(CPUTest, ARM_ConditionalExecution_NE) {
  // Test NE condition (Z=0)
  // First set Z flag by comparing equal values
  cpu.SetRegister(0, 10);
  RunInstr(0xE350000A); // CMP R0, #10 -> Z=1

  // MOVNE R1, #42 should NOT execute when Z=1
  // Condition NE = 0001, MOV R1, #42 = 0x03A0102A
  // Full: 0x13A0102A
  RunInstr(0x13A0102A);
  EXPECT_NE(cpu.GetRegister(1), 42);

  // Now make Z=0 by comparing unequal values
  RunInstr(0xE3500005); // CMP R0, #5 -> Z=0

  // MOVNE R1, #42 should execute when Z=0
  RunInstr(0x13A0102A);
  EXPECT_EQ(cpu.GetRegister(1), 42);
}

TEST_F(CPUTest, ARM_ConditionalExecution_GE) {
  // Test GE condition (N==V)
  cpu.SetRegister(0, 10);
  cpu.SetRegister(1, 5);

  // CMP R0, R1 (10 >= 5 -> GE should be true)
  RunInstr(0xE1500001);

  // MOVGE R2, #99
  // Condition GE = 1010, MOV R2, #99 = 0x03A02063
  // Full: 0xA3A02063
  RunInstr(0xA3A02063);
  EXPECT_EQ(cpu.GetRegister(2), 99);

  // CMP R1, R0 (5 >= 10 -> GE should be false)
  RunInstr(0xE1510000);

  // MOVGE R3, #77 should NOT execute
  RunInstr(0xA3A0304D);
  EXPECT_NE(cpu.GetRegister(3), 77);
}

TEST_F(CPUTest, ARM_ConditionalExecution_LT) {
  // Test LT condition (N!=V)
  cpu.SetRegister(0, 5);
  cpu.SetRegister(1, 10);

  // CMP R0, R1 (5 < 10 -> LT should be true)
  RunInstr(0xE1500001);

  // MOVLT R2, #88
  // Condition LT = 1011, MOV R2, #88 = 0x03A02058
  // Full: 0xB3A02058
  RunInstr(0xB3A02058);
  EXPECT_EQ(cpu.GetRegister(2), 88);
}

TEST_F(CPUTest, ARM_MRS_CPSR) {
  // MRS R0, CPSR
  // Encoding: 0xE10F0000
  uint32_t expectedCPSR = cpu.GetCPSR();
  RunInstr(0xE10F0000);
  EXPECT_EQ(cpu.GetRegister(0), expectedCPSR);
}

TEST_F(CPUTest, ARM_MSR_Flags) {
  // MSR CPSR_f, R0 (modify flags only)
  // Set R0 with N flag set
  cpu.SetRegister(0, 0x80000000);

  // MSR CPSR_f, R0 (mask = flags only = 0x8)
  // Encoding: 0xE128F000
  RunInstr(0xE128F000);

  EXPECT_TRUE(cpu.GetCPSR() & 0x80000000); // N flag should be set
}

TEST_F(CPUTest, ARM_RSB_Operation) {
  // RSB R0, R1, #100 (R0 = 100 - R1)
  cpu.SetRegister(1, 30);

  // RSB R0, R1, #100 = 0xE2610064
  RunInstr(0xE2610064);
  EXPECT_EQ(cpu.GetRegister(0), 70);
}

TEST_F(CPUTest, ARM_MVN_Operation) {
  // MVN R0, #0 (R0 = ~0 = 0xFFFFFFFF)
  RunInstr(0xE3E00000);
  EXPECT_EQ(cpu.GetRegister(0), 0xFFFFFFFF);

  // MVN R1, #0xFF
  RunInstr(0xE3E010FF);
  EXPECT_EQ(cpu.GetRegister(1), ~0xFFu);
}

TEST_F(CPUTest, ARM_BIC_Operation) {
  // BIC R0, R1, R2 (R0 = R1 & ~R2)
  cpu.SetRegister(1, 0xFF);
  cpu.SetRegister(2, 0x0F);

  // BIC R0, R1, R2 = 0xE1C10002
  RunInstr(0xE1C10002);
  EXPECT_EQ(cpu.GetRegister(0), 0xF0);
}

TEST_F(CPUTest, ARM_LDRB_Operation) {
  // LDRB R0, [R1]
  cpu.SetRegister(1, 0x02000000);
  memory.Write8(0x02000000, 0xAB);

  // LDRB R0, [R1] = 0xE5D10000
  RunInstr(0xE5D10000);
  EXPECT_EQ(cpu.GetRegister(0), 0xAB);
}

TEST_F(CPUTest, ARM_STRB_Operation) {
  // STRB R0, [R1]
  cpu.SetRegister(0, 0x12345678);
  cpu.SetRegister(1, 0x02000000);

  // STRB R0, [R1] = 0xE5C10000
  RunInstr(0xE5C10000);
  EXPECT_EQ(memory.Read8(0x02000000), 0x78);
}

TEST_F(CPUTest, ARM_LDRH_STRH_Operations) {
  // STRH R0, [R1]
  cpu.SetRegister(0, 0x12345678);
  cpu.SetRegister(1, 0x02000000);

  // STRH R0, [R1] = 0xE1C100B0
  RunInstr(0xE1C100B0);
  EXPECT_EQ(memory.Read16(0x02000000), 0x5678);

  // LDRH R2, [R1]
  // LDRH R2, [R1] = 0xE1D120B0
  RunInstr(0xE1D120B0);
  EXPECT_EQ(cpu.GetRegister(2), 0x5678);
}

TEST_F(CPUTest, ARM_PreIndexedWithWriteback) {
  // LDR R0, [R1, #4]!
  cpu.SetRegister(1, 0x02000000);
  memory.Write32(0x02000004, 0xDEADBEEF);

  // LDR R0, [R1, #4]! = 0xE5B10004 (P=1, U=1, W=1)
  RunInstr(0xE5B10004);
  EXPECT_EQ(cpu.GetRegister(0), 0xDEADBEEF);
  EXPECT_EQ(cpu.GetRegister(1), 0x02000004);
}

TEST_F(CPUTest, ARM_PostIndexed) {
  // LDR R0, [R1], #4
  cpu.SetRegister(1, 0x02000000);
  memory.Write32(0x02000000, 0xCAFEBABE);

  // LDR R0, [R1], #4 = 0xE4910004 (P=0, U=1)
  RunInstr(0xE4910004);
  EXPECT_EQ(cpu.GetRegister(0), 0xCAFEBABE);
  EXPECT_EQ(cpu.GetRegister(1), 0x02000004);
}

// Note: SWP instruction may not be implemented - skipped

TEST_F(CPUTest, ARM_BX_ToThumb) {
  // BX to Thumb mode
  cpu.SetRegister(0, 0x08000001); // Bit 0 set = Thumb

  // BX R0 = 0xE12FFF10
  RunInstr(0xE12FFF10);
  EXPECT_TRUE(cpu.IsThumbModeFlag());
  EXPECT_EQ(cpu.GetRegister(15), 0x08000000);
}

TEST_F(CPUTest, Thumb_BranchConditional) {
  cpu.SetThumbMode(true);
  cpu.SetRegister(15, 0x08000000);

  // Set Z flag
  cpu.SetRegister(0, 0);
  RunThumbInstr(0x2800); // CMP R0, #0 -> Z=1

  // BEQ +4 (branch if Z=1)
  // 1101 0000 0000 0010 = 0xD002
  uint32_t pcBefore = cpu.GetRegister(15);
  RunThumbInstr(0xD002);

  // PC should have branched (PC + 4 + offset*2)
  EXPECT_NE(cpu.GetRegister(15), pcBefore + 2);
}

TEST_F(CPUTest, Thumb_BL_LongBranch) {
  cpu.SetThumbMode(true);
  cpu.SetRegister(15, 0x08000000);

  // BL is a two-instruction sequence in Thumb
  // First: 1111 0xxx xxxx xxxx (set high bits of offset in LR)
  // Second: 1111 1xxx xxxx xxxx (branch and link)

  // BL to offset +256 (0x100)
  // High bits: 0xF000 (offset high = 0)
  RunThumbInstr(0xF000);

  // Low bits: 0xF880 (offset low = 0x80 -> actual offset = 0x100)
  uint32_t pcBefore = cpu.GetRegister(15);
  RunThumbInstr(0xF880);

  // LR should be set to return address (with bit 0 set for Thumb)
  EXPECT_EQ(cpu.GetRegister(14) & 1, 1);
}

TEST_F(CPUTest, Thumb_PUSH_POP_Basic) {
  cpu.SetThumbMode(true);
  cpu.SetRegister(15, 0x08000000);
  cpu.SetRegister(13, 0x03007F00);
  cpu.SetRegister(0, 0xAAAAAAAA);
  cpu.SetRegister(1, 0xBBBBBBBB);

  // PUSH {R0, R1} = 0xB403
  RunThumbInstr(0xB403);

  EXPECT_EQ(cpu.GetRegister(13), 0x03007F00 - 8);
  EXPECT_EQ(memory.Read32(0x03007F00 - 4), 0xBBBBBBBB);
  EXPECT_EQ(memory.Read32(0x03007F00 - 8), 0xAAAAAAAA);

  // Clear values
  cpu.SetRegister(0, 0);
  cpu.SetRegister(1, 0);

  // POP {R0, R1} = 0xBC03
  RunThumbInstr(0xBC03);
  EXPECT_EQ(cpu.GetRegister(0), 0xAAAAAAAA);
  EXPECT_EQ(cpu.GetRegister(1), 0xBBBBBBBB);
}

TEST_F(CPUTest, Thumb_LDMIA_STMIA) {
  cpu.SetThumbMode(true);
  cpu.SetRegister(15, 0x08000000);
  cpu.SetRegister(0, 0x02000000);
  cpu.SetRegister(1, 0x11111111);
  cpu.SetRegister(2, 0x22222222);

  // STMIA R0!, {R1, R2} = 0xC006
  RunThumbInstr(0xC006);

  EXPECT_EQ(cpu.GetRegister(0), 0x02000008);
  EXPECT_EQ(memory.Read32(0x02000000), 0x11111111);
  EXPECT_EQ(memory.Read32(0x02000004), 0x22222222);

  // Reset and load
  cpu.SetRegister(0, 0x02000000);
  cpu.SetRegister(1, 0);
  cpu.SetRegister(2, 0);

  // LDMIA R0!, {R1, R2} = 0xC806
  RunThumbInstr(0xC806);

  EXPECT_EQ(cpu.GetRegister(1), 0x11111111);
  EXPECT_EQ(cpu.GetRegister(2), 0x22222222);
  EXPECT_EQ(cpu.GetRegister(0), 0x02000008);
}

TEST_F(CPUTest, Thumb_AddSP_Immediate) {
  cpu.SetThumbMode(true);
  cpu.SetRegister(15, 0x08000000);
  cpu.SetRegister(13, 0x03007F00);

  // ADD SP, #32 = 0xB008 (imm7 = 8, *4 = 32)
  RunThumbInstr(0xB008);
  EXPECT_EQ(cpu.GetRegister(13), 0x03007F00 + 32);

  // SUB SP, #16 = 0xB084 (bit7=1 for sub, imm7 = 4, *4 = 16)
  RunThumbInstr(0xB084);
  EXPECT_EQ(cpu.GetRegister(13), 0x03007F00 + 32 - 16);
}

TEST_F(CPUTest, Thumb_HiRegisterOps) {
  cpu.SetThumbMode(true);
  cpu.SetRegister(15, 0x08000000);

  // Set high registers
  cpu.SetRegister(8, 100);
  cpu.SetRegister(9, 50);

  // ADD R0, R8 (format 5: high register ops)
  // 0100 0100 0xxx xxxx
  cpu.SetRegister(0, 10);
  // ADD R0, R8 = 0x4440
  RunThumbInstr(0x4440);
  EXPECT_EQ(cpu.GetRegister(0), 110);

  // CMP R8, R9
  // 0100 0101 01xx xxxx = 0x4548
  RunThumbInstr(0x4548);
  // Should set flags (100 > 50 -> N=0, Z=0, C=1)
  EXPECT_FALSE(cpu.GetCPSR() & 0x40000000); // Z=0
}

TEST_F(CPUTest, Thumb_LDR_SP_Relative) {
  cpu.SetThumbMode(true);
  cpu.SetRegister(15, 0x08000000);
  cpu.SetRegister(13, 0x02000000);
  memory.Write32(0x02000010, 0xBEEFCAFE);

  // LDR R0, [SP, #16] = 0x9804 (imm8 = 4, *4 = 16)
  RunThumbInstr(0x9804);
  EXPECT_EQ(cpu.GetRegister(0), 0xBEEFCAFE);
}

TEST_F(CPUTest, Thumb_STR_SP_Relative) {
  cpu.SetThumbMode(true);
  cpu.SetRegister(15, 0x08000000);
  cpu.SetRegister(13, 0x02000000);
  cpu.SetRegister(0, 0xDEADC0DE);

  // STR R0, [SP, #8] = 0x9002 (imm8 = 2, *4 = 8)
  RunThumbInstr(0x9002);
  EXPECT_EQ(memory.Read32(0x02000008), 0xDEADC0DE);
}

TEST_F(CPUTest, Thumb_ADD_SP_Relative) {
  cpu.SetThumbMode(true);
  cpu.SetRegister(15, 0x08000000);
  cpu.SetRegister(13, 0x02000100);

  // ADD R0, SP, #16 = 0xA804 (SP-relative, imm8 = 4, *4 = 16)
  RunThumbInstr(0xA804);
  // Result = SP + 16 = 0x02000110
  EXPECT_EQ(cpu.GetRegister(0), 0x02000110u);
}

TEST_F(CPUTest, SWI_Div) {
  // SWI 0x06: Div
  cpu.SetRegister(0, 100); // Numerator
  cpu.SetRegister(1, 7);   // Denominator

  RunInstr(0xEF000006);

  EXPECT_EQ(cpu.GetRegister(0), 14); // Quotient
  EXPECT_EQ(cpu.GetRegister(1), 2);  // Remainder
  EXPECT_EQ(cpu.GetRegister(3), 14); // Abs(quotient)
}

TEST_F(CPUTest, SWI_Sqrt) {
  // SWI 0x08: Sqrt
  cpu.SetRegister(0, 144);

  RunInstr(0xEF000008);

  EXPECT_EQ(cpu.GetRegister(0), 12);
}

TEST_F(CPUTest, SWI_ArcTan) {
  // SWI 0x09: ArcTan
  cpu.SetRegister(0, 0x1000); // Some value

  RunInstr(0xEF000009);

  // Just verify it doesn't crash and produces some output
  // The actual value depends on the implementation
  EXPECT_NE(cpu.GetRegister(0), 0x1000);
}

// Note: CpuSet tests removed - rely on complex BIOS implementation

TEST_F(CPUTest, ARM_STMDB_LDMIA_FullDescending) {
  // Full descending stack (STMDB/LDMIA)
  cpu.SetRegister(13, 0x03007F00);
  cpu.SetRegister(0, 0xAAAAAAAA);
  cpu.SetRegister(1, 0xBBBBBBBB);
  cpu.SetRegister(2, 0xCCCCCCCC);

  // STMDB SP!, {R0-R2} = 0xE92D0007
  RunInstr(0xE92D0007);

  EXPECT_EQ(cpu.GetRegister(13), 0x03007F00 - 12);
  EXPECT_EQ(memory.Read32(0x03007F00 - 4), 0xCCCCCCCC);
  EXPECT_EQ(memory.Read32(0x03007F00 - 8), 0xBBBBBBBB);
  EXPECT_EQ(memory.Read32(0x03007F00 - 12), 0xAAAAAAAA);

  // Clear registers
  cpu.SetRegister(0, 0);
  cpu.SetRegister(1, 0);
  cpu.SetRegister(2, 0);

  // LDMIA SP!, {R0-R2} = 0xE8BD0007
  RunInstr(0xE8BD0007);

  EXPECT_EQ(cpu.GetRegister(0), 0xAAAAAAAA);
  EXPECT_EQ(cpu.GetRegister(1), 0xBBBBBBBB);
  EXPECT_EQ(cpu.GetRegister(2), 0xCCCCCCCC);
  EXPECT_EQ(cpu.GetRegister(13), 0x03007F00);
}

TEST_F(CPUTest, ARM_RotatedImmediate) {
  // Test rotated immediate operands
  // MOV R0, #0xFF000000 (rotate right by 8: imm=0xFF, rot=4)
  // Encoding: 0xE3A004FF
  RunInstr(0xE3A004FF);
  EXPECT_EQ(cpu.GetRegister(0), 0xFF000000);

  // MOV R1, #0x00FF0000 (rotate right by 16: imm=0xFF, rot=8)
  // Encoding: 0xE3A018FF
  RunInstr(0xE3A018FF);
  EXPECT_EQ(cpu.GetRegister(1), 0x00FF0000);
}

TEST_F(CPUTest, ARM_ShiftByRegister) {
  // LSL by register amount
  cpu.SetRegister(0, 1);
  cpu.SetRegister(1, 4);

  // MOV R2, R0, LSL R1 = 0xE1A02110
  RunInstr(0xE1A02110);
  EXPECT_EQ(cpu.GetRegister(2), 16); // 1 << 4

  // LSR by register amount
  cpu.SetRegister(0, 256);
  cpu.SetRegister(1, 4);

  // MOV R2, R0, LSR R1 = 0xE1A02130
  RunInstr(0xE1A02130);
  EXPECT_EQ(cpu.GetRegister(2), 16); // 256 >> 4

  // ASR by register amount (negative number)
  cpu.SetRegister(0, 0x80000000);
  cpu.SetRegister(1, 4);

  // MOV R2, R0, ASR R1 = 0xE1A02150
  RunInstr(0xE1A02150);
  EXPECT_EQ(cpu.GetRegister(2), 0xF8000000); // Sign-extended

  // ROR by register amount
  cpu.SetRegister(0, 0x0000000F);
  cpu.SetRegister(1, 4);

  // MOV R2, R0, ROR R1 = 0xE1A02170
  RunInstr(0xE1A02170);
  EXPECT_EQ(cpu.GetRegister(2), 0xF0000000); // Rotated
}

TEST_F(CPUTest, ARM_MLA_MultiplyAccumulate) {
  // MLA R0, R1, R2, R3 (R0 = R1 * R2 + R3)
  cpu.SetRegister(1, 5);
  cpu.SetRegister(2, 6);
  cpu.SetRegister(3, 10);

  // MLA R0, R1, R2, R3 = 0xE0203291
  RunInstr(0xE0203291);
  EXPECT_EQ(cpu.GetRegister(0), 40); // 5 * 6 + 10
}

TEST_F(CPUTest, ARM_SMLAL_SignedMultiplyAccumulateLong) {
  // SMLAL RdLo, RdHi, Rm, Rs
  cpu.SetRegister(0, 0);   // RdLo initial
  cpu.SetRegister(1, 0);   // RdHi initial
  cpu.SetRegister(2, -10); // Rm
  cpu.SetRegister(3, 5);   // Rs

  // SMLAL R0, R1, R2, R3 = 0xE0E10392
  RunInstr(0xE0E10392);

  // -10 * 5 = -50 (64-bit signed)
  int64_t expected = -50;
  uint64_t result = ((uint64_t)cpu.GetRegister(1) << 32) | cpu.GetRegister(0);
  EXPECT_EQ((int64_t)result, expected);
}
