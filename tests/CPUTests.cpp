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
