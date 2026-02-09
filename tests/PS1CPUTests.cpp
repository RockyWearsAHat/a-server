#include "emulator/ps1/PS1Constants.h"
#include "emulator/ps1/PS1Memory.h"
#include "emulator/ps1/R3000A.h"
#include <gtest/gtest.h>

using namespace AIO::Emulator::PS1;

class PS1CPUTest : public ::testing::Test {
protected:
  void SetUp() override {
    memory = std::make_unique<PS1Memory>();
    cpu = std::make_unique<R3000A>(*memory);
  }

  void WriteBIOSInstruction(uint32_t offset, uint32_t instruction) {
    memory->WriteBIOS32(offset, instruction);
  }

  // MIPS instruction encoding helpers
  static constexpr uint32_t EncodeRType(uint32_t rs, uint32_t rt, uint32_t rd,
                                        uint32_t shamt, uint32_t funct) {
    return (rs << 21) | (rt << 16) | (rd << 11) | (shamt << 6) | funct;
  }

  static constexpr uint32_t EncodeIType(uint32_t opcode, uint32_t rs,
                                        uint32_t rt, uint16_t imm) {
    return (opcode << 26) | (rs << 21) | (rt << 16) | imm;
  }

  static constexpr uint32_t EncodeJType(uint32_t opcode, uint32_t target) {
    return (opcode << 26) | (target & 0x3FFFFFF);
  }

  // Common instruction encodings
  static constexpr uint32_t NOP = 0;
  static constexpr uint32_t ADDU(uint32_t rd, uint32_t rs, uint32_t rt) {
    return EncodeRType(rs, rt, rd, 0, 0x21);
  }
  static constexpr uint32_t ADDIU(uint32_t rt, uint32_t rs, uint16_t imm) {
    return EncodeIType(0x09, rs, rt, imm);
  }
  static constexpr uint32_t ORI(uint32_t rt, uint32_t rs, uint16_t imm) {
    return EncodeIType(0x0D, rs, rt, imm);
  }
  static constexpr uint32_t LUI(uint32_t rt, uint16_t imm) {
    return EncodeIType(0x0F, 0, rt, imm);
  }
  static constexpr uint32_t SW(uint32_t rt, uint32_t base, uint16_t offset) {
    return EncodeIType(0x2B, base, rt, offset);
  }
  static constexpr uint32_t LW(uint32_t rt, uint32_t base, uint16_t offset) {
    return EncodeIType(0x23, base, rt, offset);
  }
  static constexpr uint32_t AND(uint32_t rd, uint32_t rs, uint32_t rt) {
    return EncodeRType(rs, rt, rd, 0, 0x24);
  }
  static constexpr uint32_t OR(uint32_t rd, uint32_t rs, uint32_t rt) {
    return EncodeRType(rs, rt, rd, 0, 0x25);
  }
  static constexpr uint32_t XOR(uint32_t rd, uint32_t rs, uint32_t rt) {
    return EncodeRType(rs, rt, rd, 0, 0x26);
  }
  static constexpr uint32_t SLT(uint32_t rd, uint32_t rs, uint32_t rt) {
    return EncodeRType(rs, rt, rd, 0, 0x2A);
  }
  static constexpr uint32_t SLTU(uint32_t rd, uint32_t rs, uint32_t rt) {
    return EncodeRType(rs, rt, rd, 0, 0x2B);
  }
  static constexpr uint32_t SLL(uint32_t rd, uint32_t rt, uint32_t shamt) {
    return EncodeRType(0, rt, rd, shamt, 0x00);
  }
  static constexpr uint32_t SRL(uint32_t rd, uint32_t rt, uint32_t shamt) {
    return EncodeRType(0, rt, rd, shamt, 0x02);
  }
  static constexpr uint32_t SRA(uint32_t rd, uint32_t rt, uint32_t shamt) {
    return EncodeRType(0, rt, rd, shamt, 0x03);
  }
  static constexpr uint32_t JR(uint32_t rs) {
    return EncodeRType(rs, 0, 0, 0, 0x08);
  }
  static constexpr uint32_t JALR(uint32_t rd, uint32_t rs) {
    return EncodeRType(rs, 0, rd, 0, 0x09);
  }
  static constexpr uint32_t BEQ(uint32_t rs, uint32_t rt, int16_t offset) {
    return EncodeIType(0x04, rs, rt, static_cast<uint16_t>(offset));
  }
  static constexpr uint32_t BNE(uint32_t rs, uint32_t rt, int16_t offset) {
    return EncodeIType(0x05, rs, rt, static_cast<uint16_t>(offset));
  }
  static constexpr uint32_t SUB(uint32_t rd, uint32_t rs, uint32_t rt) {
    return EncodeRType(rs, rt, rd, 0, 0x22);
  }
  static constexpr uint32_t SUBU(uint32_t rd, uint32_t rs, uint32_t rt) {
    return EncodeRType(rs, rt, rd, 0, 0x23);
  }

  std::unique_ptr<PS1Memory> memory;
  std::unique_ptr<R3000A> cpu;
};

// ─── Reset State ────────────────────────────────────────────────────────

TEST_F(PS1CPUTest, ResetState) {
  cpu->Reset();
  EXPECT_EQ(cpu->GetPC(), CPU::RESET_VECTOR);
  for (uint32_t i = 0; i < 32; i++) {
    EXPECT_EQ(cpu->GetRegister(i), 0u) << "Register " << i;
  }
  EXPECT_EQ(cpu->GetHI(), 0u);
  EXPECT_EQ(cpu->GetLO(), 0u);
}

TEST_F(PS1CPUTest, R0AlwaysZero) {
  cpu->SetRegister(0, 0xDEADBEEF);
  EXPECT_EQ(cpu->GetRegister(0), 0u);
}

// ─── ALU Operations ────────────────────────────────────────────────────

TEST_F(PS1CPUTest, ADDIU_LoadsImmediate) {
  // addiu $1, $0, 42
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, ADDIU(1, 0, 42));
  WriteBIOSInstruction(pc + 4, NOP);
  cpu->Step();
  EXPECT_EQ(cpu->GetRegister(1), 42u);
}

TEST_F(PS1CPUTest, ADDIU_SignExtends) {
  // addiu $1, $0, 0xFFFE  (-2 sign-extended)
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, ADDIU(1, 0, 0xFFFE));
  WriteBIOSInstruction(pc + 4, NOP);
  cpu->Step();
  EXPECT_EQ(cpu->GetRegister(1), 0xFFFFFFFE);
}

TEST_F(PS1CPUTest, LUI_LoadsUpperImmediate) {
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, LUI(1, 0x1234));
  WriteBIOSInstruction(pc + 4, NOP);
  cpu->Step();
  EXPECT_EQ(cpu->GetRegister(1), 0x12340000u);
}

TEST_F(PS1CPUTest, ORI_LogicalOr) {
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, LUI(1, 0x1234));
  WriteBIOSInstruction(pc + 4, ORI(1, 1, 0x5678));
  WriteBIOSInstruction(pc + 8, NOP);
  cpu->Step(); // LUI
  cpu->Step(); // ORI
  EXPECT_EQ(cpu->GetRegister(1), 0x12345678u);
}

TEST_F(PS1CPUTest, ADDU_AddsTwoRegisters) {
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, ADDIU(1, 0, 10));     // $1 = 10
  WriteBIOSInstruction(pc + 4, ADDIU(2, 0, 20)); // $2 = 20
  WriteBIOSInstruction(pc + 8, ADDU(3, 1, 2));   // $3 = $1 + $2
  WriteBIOSInstruction(pc + 12, NOP);
  cpu->Step();
  cpu->Step();
  cpu->Step();
  EXPECT_EQ(cpu->GetRegister(3), 30u);
}

TEST_F(PS1CPUTest, SUBU_SubtractsTwoRegisters) {
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, ADDIU(1, 0, 50));
  WriteBIOSInstruction(pc + 4, ADDIU(2, 0, 20));
  WriteBIOSInstruction(pc + 8, SUBU(3, 1, 2));
  WriteBIOSInstruction(pc + 12, NOP);
  cpu->Step();
  cpu->Step();
  cpu->Step();
  EXPECT_EQ(cpu->GetRegister(3), 30u);
}

TEST_F(PS1CPUTest, AND_BitwiseAnd) {
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, ADDIU(1, 0, 0xFF));
  WriteBIOSInstruction(pc + 4, ADDIU(2, 0, 0x0F));
  WriteBIOSInstruction(pc + 8, AND(3, 1, 2));
  WriteBIOSInstruction(pc + 12, NOP);
  cpu->Step();
  cpu->Step();
  cpu->Step();
  EXPECT_EQ(cpu->GetRegister(3), 0x0Fu);
}

TEST_F(PS1CPUTest, OR_BitwiseOr) {
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, ADDIU(1, 0, 0xF0));
  WriteBIOSInstruction(pc + 4, ADDIU(2, 0, 0x0F));
  WriteBIOSInstruction(pc + 8, OR(3, 1, 2));
  WriteBIOSInstruction(pc + 12, NOP);
  cpu->Step();
  cpu->Step();
  cpu->Step();
  EXPECT_EQ(cpu->GetRegister(3), 0xFFu);
}

TEST_F(PS1CPUTest, XOR_BitwiseXor) {
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, ADDIU(1, 0, 0xFF));
  WriteBIOSInstruction(pc + 4, ADDIU(2, 0, 0x0F));
  WriteBIOSInstruction(pc + 8, XOR(3, 1, 2));
  WriteBIOSInstruction(pc + 12, NOP);
  cpu->Step();
  cpu->Step();
  cpu->Step();
  EXPECT_EQ(cpu->GetRegister(3), 0xF0u);
}

TEST_F(PS1CPUTest, SLT_SetOnLessThan_Signed) {
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, ADDIU(1, 0, 5));
  WriteBIOSInstruction(pc + 4, ADDIU(2, 0, 10));
  WriteBIOSInstruction(pc + 8, SLT(3, 1, 2));  // 5 < 10 → 1
  WriteBIOSInstruction(pc + 12, SLT(4, 2, 1)); // 10 < 5 → 0
  WriteBIOSInstruction(pc + 16, NOP);
  cpu->Step();
  cpu->Step();
  cpu->Step();
  cpu->Step();
  EXPECT_EQ(cpu->GetRegister(3), 1u);
  EXPECT_EQ(cpu->GetRegister(4), 0u);
}

// ─── Shift Operations ──────────────────────────────────────────────────

TEST_F(PS1CPUTest, SLL_ShiftLeftLogical) {
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, ADDIU(1, 0, 1));
  WriteBIOSInstruction(pc + 4, SLL(2, 1, 4)); // 1 << 4 = 16
  WriteBIOSInstruction(pc + 8, NOP);
  cpu->Step();
  cpu->Step();
  EXPECT_EQ(cpu->GetRegister(2), 16u);
}

TEST_F(PS1CPUTest, SRL_ShiftRightLogical) {
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, ADDIU(1, 0, 0x80));
  WriteBIOSInstruction(pc + 4, SRL(2, 1, 4)); // 0x80 >> 4 = 8
  WriteBIOSInstruction(pc + 8, NOP);
  cpu->Step();
  cpu->Step();
  EXPECT_EQ(cpu->GetRegister(2), 8u);
}

TEST_F(PS1CPUTest, SRA_ShiftRightArithmetic_PreservesSign) {
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  // Load 0xFFFFFF00 into $1 (using LUI + ORI)
  WriteBIOSInstruction(pc, LUI(1, 0xFFFF));
  WriteBIOSInstruction(pc + 4, ORI(1, 1, 0xFF00));
  WriteBIOSInstruction(pc + 8, SRA(2, 1, 8));
  WriteBIOSInstruction(pc + 12, NOP);
  cpu->Step();
  cpu->Step();
  cpu->Step();
  EXPECT_EQ(cpu->GetRegister(2), 0xFFFFFFFFu);
}

// ─── Branch/Jump ────────────────────────────────────────────────────────

TEST_F(PS1CPUTest, BEQ_BranchesTaken) {
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, BEQ(0, 0, 2));       // branch +2 (skip next 2 words)
  WriteBIOSInstruction(pc + 4, ADDIU(1, 0, 1)); // delay slot — executed
  WriteBIOSInstruction(pc + 8, ADDIU(2, 0, 99));  // skipped
  WriteBIOSInstruction(pc + 12, ADDIU(3, 0, 42)); // branch target
  WriteBIOSInstruction(pc + 16, NOP);

  cpu->Step(); // BEQ
  cpu->Step(); // delay slot (ADDIU $1)
  cpu->Step(); // branch target (ADDIU $3)

  EXPECT_EQ(cpu->GetRegister(1), 1u);  // delay slot executed
  EXPECT_EQ(cpu->GetRegister(2), 0u);  // skipped
  EXPECT_EQ(cpu->GetRegister(3), 42u); // branch target
}

TEST_F(PS1CPUTest, BNE_NotTaken) {
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, BNE(0, 0, 2)); // $0 == $0, not taken
  WriteBIOSInstruction(pc + 4,
                       ADDIU(1, 0, 1)); // sequential (delay slot still runs)
  WriteBIOSInstruction(pc + 8, ADDIU(2, 0, 99)); // should execute (not skipped)
  WriteBIOSInstruction(pc + 12, NOP);

  cpu->Step(); // BNE (not taken)
  cpu->Step(); // delay slot
  cpu->Step(); // next instruction

  EXPECT_EQ(cpu->GetRegister(1), 1u);
  EXPECT_EQ(cpu->GetRegister(2), 99u);
}

// ─── Load Delay Slot ───────────────────────────────────────────────────

TEST_F(PS1CPUTest, LoadDelaySlot) {
  // Write a known value to RAM
  memory->WriteRAM32(0x100, 0xDEADBEEF);

  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, LUI(1, 0x0000));      // $1 = 0 (base for RAM)
  WriteBIOSInstruction(pc + 4, LW(2, 1, 0x100)); // $2 = MEM[$1 + 0x100]
  WriteBIOSInstruction(pc + 8,
                       ADDU(3, 2, 0)); // $3 = $2 (in delay slot, uses OLD $2)
  WriteBIOSInstruction(pc + 12,
                       ADDU(4, 2, 0)); // $4 = $2 (after delay, uses NEW $2)
  WriteBIOSInstruction(pc + 16, NOP);

  cpu->Step(); // LUI
  cpu->Step(); // LW (schedules load)
  cpu->Step(); // ADDU $3,$2,$0 — delay slot, $2 still 0
  cpu->Step(); // ADDU $4,$2,$0 — $2 now loaded

  EXPECT_EQ(cpu->GetRegister(3), 0u);          // load not yet visible
  EXPECT_EQ(cpu->GetRegister(4), 0xDEADBEEFu); // load now visible
}

// ─── Multiply/Divide ───────────────────────────────────────────────────

TEST_F(PS1CPUTest, MULT_MultiplyRegisters) {
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, ADDIU(1, 0, 7));
  WriteBIOSInstruction(pc + 4, ADDIU(2, 0, 6));
  WriteBIOSInstruction(pc + 8, EncodeRType(1, 2, 0, 0, 0x18));  // MULT $1,$2
  WriteBIOSInstruction(pc + 12, EncodeRType(0, 0, 3, 0, 0x12)); // MFLO $3
  WriteBIOSInstruction(pc + 16, NOP);
  cpu->Step();
  cpu->Step();
  cpu->Step();
  cpu->Step();
  EXPECT_EQ(cpu->GetRegister(3), 42u);
}

TEST_F(PS1CPUTest, DIV_DivideRegisters) {
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, ADDIU(1, 0, 100));
  WriteBIOSInstruction(pc + 4, ADDIU(2, 0, 7));
  WriteBIOSInstruction(pc + 8, EncodeRType(1, 2, 0, 0, 0x1A)); // DIV $1,$2
  WriteBIOSInstruction(pc + 12,
                       EncodeRType(0, 0, 3, 0, 0x12)); // MFLO $3 (quotient)
  WriteBIOSInstruction(pc + 16,
                       EncodeRType(0, 0, 4, 0, 0x10)); // MFHI $4 (remainder)
  WriteBIOSInstruction(pc + 20, NOP);
  cpu->Step();
  cpu->Step();
  cpu->Step();
  cpu->Step();
  cpu->Step();
  EXPECT_EQ(cpu->GetRegister(3), 14u); // 100 / 7 = 14
  EXPECT_EQ(cpu->GetRegister(4), 2u);  // 100 % 7 = 2
}

// ─── COP0 ──────────────────────────────────────────────────────────────

TEST_F(PS1CPUTest, COP0_MTC0_MFC0_RoundTrip) {
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, ADDIU(1, 0, 0x1234));
  // MTC0 $1, $12 (write SR)
  WriteBIOSInstruction(pc + 4,
                       (0x10 << 26) | (0x04 << 21) | (1 << 16) | CPU::COP0::SR);
  // MFC0 $2, $12 (read SR back)
  WriteBIOSInstruction(pc + 8,
                       (0x10 << 26) | (0x00 << 21) | (2 << 16) | CPU::COP0::SR);
  WriteBIOSInstruction(pc + 12, NOP);
  WriteBIOSInstruction(pc + 16, NOP);

  cpu->Step(); // ADDIU
  cpu->Step(); // MTC0
  cpu->Step(); // MFC0 (load delay)
  cpu->Step(); // NOP (result available)

  EXPECT_EQ(cpu->GetRegister(2), 0x1234u);
}

// ─── Store/Load Round Trip ─────────────────────────────────────────────

TEST_F(PS1CPUTest, SW_LW_RoundTrip) {
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, LUI(1, 0x0000));          // base = 0
  WriteBIOSInstruction(pc + 4, ADDIU(2, 0, 0x1234)); // value
  WriteBIOSInstruction(pc + 8, SW(2, 1, 0x200));     // MEM[0x200] = $2
  WriteBIOSInstruction(pc + 12, LW(3, 1, 0x200));    // $3 = MEM[0x200]
  WriteBIOSInstruction(pc + 16, NOP);                // load delay
  WriteBIOSInstruction(pc + 20, NOP);

  cpu->Step();
  cpu->Step();
  cpu->Step();
  cpu->Step();
  cpu->Step();
  EXPECT_EQ(cpu->GetRegister(3), 0x1234u);
}

// ─── PC Advancement ────────────────────────────────────────────────────

TEST_F(PS1CPUTest, PCAdvancesBy4EachStep) {
  uint32_t startPC = cpu->GetPC();
  uint32_t pc = startPC & 0x1FFFFF;
  WriteBIOSInstruction(pc, NOP);
  WriteBIOSInstruction(pc + 4, NOP);
  WriteBIOSInstruction(pc + 8, NOP);

  cpu->Step();
  EXPECT_EQ(cpu->GetPC(), startPC + 4);
  cpu->Step();
  EXPECT_EQ(cpu->GetPC(), startPC + 8);
}

// ─── Instruction Counter ───────────────────────────────────────────────

TEST_F(PS1CPUTest, InstructionCountIncrementsOnStep) {
  uint32_t pc = CPU::RESET_VECTOR & 0x1FFFFF;
  WriteBIOSInstruction(pc, NOP);
  WriteBIOSInstruction(pc + 4, NOP);
  WriteBIOSInstruction(pc + 8, NOP);

  uint64_t before = cpu->GetInstructionCount();
  cpu->Step();
  EXPECT_EQ(cpu->GetInstructionCount(), before + 1);
  cpu->Step();
  EXPECT_EQ(cpu->GetInstructionCount(), before + 2);
}

// ─── Debug Helpers ──────────────────────────────────────────────────────

TEST_F(PS1CPUTest, GetDebugSummary_NotEmpty) {
  auto summary = cpu->GetDebugSummary();
  EXPECT_FALSE(summary.empty());
}

TEST_F(PS1CPUTest, DumpState_WritesOutput) {
  std::ostringstream os;
  cpu->DumpState(os);
  EXPECT_FALSE(os.str().empty());
}
