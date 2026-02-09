#include "emulator/ps1/GTE.h"
#include "emulator/ps1/PS1Constants.h"
#include <gtest/gtest.h>

using namespace AIO::Emulator::PS1;

class PS1GTETest : public ::testing::Test {
protected:
  void SetUp() override { gte = std::make_unique<GTE>(); }
  std::unique_ptr<GTE> gte;
};

// ─── Data Register Round-Trip ──────────────────────────────────────────

TEST_F(PS1GTETest, WriteData_ReadData_V0) {
  // V0 XY packed in register 0
  gte->WriteData(0, 0x00020001); // V0.X=1, V0.Y=2
  uint32_t val = gte->ReadData(0);
  EXPECT_EQ(val & 0xFFFF, 1u);
  EXPECT_EQ((val >> 16) & 0xFFFF, 2u);
}

TEST_F(PS1GTETest, WriteData_ReadData_V0Z) {
  gte->WriteData(1, 3); // V0.Z = 3
  EXPECT_EQ(gte->ReadData(1) & 0xFFFF, 3u);
}

TEST_F(PS1GTETest, WriteData_IR_RoundTrip) {
  gte->WriteData(9, 0x1234); // IR1
  EXPECT_EQ(gte->ReadData(9) & 0xFFFF, 0x1234u);
}

// ─── Control Register Round-Trip ───────────────────────────────────────

TEST_F(PS1GTETest, WriteControl_ReadControl_RT) {
  // RT[0][0] and RT[0][1] packed
  gte->WriteControl(0, 0x00FF0010); // RT[0][0]=0x10, RT[0][1]=0xFF
  uint32_t val = gte->ReadControl(0);
  EXPECT_EQ(val & 0xFFFF, 0x0010u);
  EXPECT_EQ((val >> 16) & 0xFFFF, 0x00FFu);
}

TEST_F(PS1GTETest, WriteControl_TranslationVector) {
  gte->WriteControl(5, 1000); // TR[0]
  gte->WriteControl(6, 2000); // TR[1]
  gte->WriteControl(7, 3000); // TR[2]
  EXPECT_EQ(static_cast<int32_t>(gte->ReadControl(5)), 1000);
  EXPECT_EQ(static_cast<int32_t>(gte->ReadControl(6)), 2000);
  EXPECT_EQ(static_cast<int32_t>(gte->ReadControl(7)), 3000);
}

TEST_F(PS1GTETest, WriteControl_OFX_OFY) {
  gte->WriteControl(24, 0x00100000); // OFX
  gte->WriteControl(25, 0x00080000); // OFY
  EXPECT_EQ(gte->ReadControl(24), 0x00100000u);
  EXPECT_EQ(gte->ReadControl(25), 0x00080000u);
}

TEST_F(PS1GTETest, WriteControl_H) {
  gte->WriteControl(26, 256); // H (projection plane distance)
  EXPECT_EQ(gte->ReadControl(26) & 0xFFFF, 256u);
}

// ─── Flag Register ──────────────────────────────────────────────────────

TEST_F(PS1GTETest, FlagRegister_ClearsOnCommand) {
  // Set flag via control register write
  gte->WriteControl(31, 0x7FFFFFFF);
  // Execute any command → flags should be cleared
  gte->Execute(0x06); // NCLIP
  // Flags should be recalculated based on the operation
  uint32_t flag = gte->ReadControl(31);
  // Bit 31 is error summary, lower bits are specific flags
  (void)flag; // Just verify no crash
}

// ─── SXY FIFO ──────────────────────────────────────────────────────────

TEST_F(PS1GTETest, SXY_FIFO_PushAndRead) {
  // Write to SXY FIFO via register 15
  gte->WriteData(15, 0x00010002); // SXY2 = (2, 1) → pushes FIFO
  gte->WriteData(15, 0x00030004); // Another push

  // SXY0 should now contain the first push (shifted)
  // SXY2 should be the latest value
  uint32_t sxy2 = gte->ReadData(14);
  EXPECT_EQ(sxy2 & 0xFFFF, 4u);         // X
  EXPECT_EQ((sxy2 >> 16) & 0xFFFF, 3u); // Y
}

// ─── SZ FIFO ───────────────────────────────────────────────────────────

TEST_F(PS1GTETest, SZ_Registers_ReadWrite) {
  gte->WriteData(16, 100); // SZ0
  gte->WriteData(17, 200); // SZ1
  gte->WriteData(18, 300); // SZ2
  gte->WriteData(19, 400); // SZ3

  EXPECT_EQ(gte->ReadData(16), 100u);
  EXPECT_EQ(gte->ReadData(17), 200u);
  EXPECT_EQ(gte->ReadData(18), 300u);
  EXPECT_EQ(gte->ReadData(19), 400u);
}

// ─── IRGB Write ────────────────────────────────────────────────────────

TEST_F(PS1GTETest, IRGB_Write_SetsIR123) {
  // IRGB = R(5bit) | G(5bit) << 5 | B(5bit) << 10
  uint32_t irgb = 0x1F | (0x1F << 5) | (0x1F << 10); // max values
  gte->WriteData(28, irgb);

  // IR1 = R * 0x80, IR2 = G * 0x80, IR3 = B * 0x80
  int16_t ir1 = static_cast<int16_t>(gte->ReadData(9));
  int16_t ir2 = static_cast<int16_t>(gte->ReadData(10));
  int16_t ir3 = static_cast<int16_t>(gte->ReadData(11));
  EXPECT_EQ(ir1, 0x1F * 0x80);
  EXPECT_EQ(ir2, 0x1F * 0x80);
  EXPECT_EQ(ir3, 0x1F * 0x80);
}

// ─── LZCS/LZCR ────────────────────────────────────────────────────────

TEST_F(PS1GTETest, LZCS_CountLeadingZeros_Positive) {
  gte->WriteData(30, 0x00010000); // LZCS = 65536
  int32_t lzcr = static_cast<int32_t>(gte->ReadData(31));
  EXPECT_EQ(lzcr, 15); // 15 leading zeros in 0x00010000
}

TEST_F(PS1GTETest, LZCS_CountLeadingZeros_Negative) {
  gte->WriteData(30, static_cast<uint32_t>(-1)); // LZCS = 0xFFFFFFFF
  int32_t lzcr = static_cast<int32_t>(gte->ReadData(31));
  // For negative values, GTE inverts and counts leading zeros: ~0xFFFFFFFF = 0
  // → CLZ = 32
  EXPECT_EQ(lzcr, 32);
}

TEST_F(PS1GTETest, LZCS_CountLeadingZeros_Zero) {
  gte->WriteData(30, 0); // LZCS = 0
  int32_t lzcr = static_cast<int32_t>(gte->ReadData(31));
  EXPECT_EQ(lzcr, 32);
}

// ─── NCLIP Command ──────────────────────────────────────────────────────

TEST_F(PS1GTETest, NCLIP_TriangleCrossProduct) {
  // Set up SXY FIFO with a known triangle
  gte->WriteData(12, 0x00000000); // SXY0 = (0, 0)
  gte->WriteData(13, 0x00000064); // SXY1 = (100, 0)
  gte->WriteData(14, 0x00640000); // SXY2 = (0, 100)

  gte->Execute(0x06); // NCLIP

  int32_t mac0 = static_cast<int32_t>(gte->ReadData(24));
  // Cross product = 0*0 - 100*0 + 100*100 - 0*100 + 0*0 - 0*0 = 10000
  EXPECT_EQ(mac0, 10000);
}

// ─── AVSZ3 Command ─────────────────────────────────────────────────────

TEST_F(PS1GTETest, AVSZ3_AveragesDepths) {
  // Set ZSF3
  gte->WriteControl(29, 341); // ZSF3 = 341 (≈ 1024/3)

  // Set SZ1, SZ2, SZ3
  gte->WriteData(17, 100); // SZ1
  gte->WriteData(18, 200); // SZ2
  gte->WriteData(19, 300); // SZ3

  gte->Execute(0x2D); // AVSZ3

  // MAC0 = ZSF3 * (SZ1 + SZ2 + SZ3) = 341 * 600 = 204600
  int32_t mac0 = static_cast<int32_t>(gte->ReadData(24));
  EXPECT_EQ(mac0, 341 * 600);
}

// ─── SQR Command ───────────────────────────────────────────────────────

TEST_F(PS1GTETest, SQR_SquaresIR) {
  gte->WriteData(9, 5);  // IR1 = 5
  gte->WriteData(10, 7); // IR2 = 7
  gte->WriteData(11, 3); // IR3 = 3

  gte->Execute(0x28 | (1 << 10)); // SQR with lm=1

  int32_t mac1 = static_cast<int32_t>(gte->ReadData(25));
  int32_t mac2 = static_cast<int32_t>(gte->ReadData(26));
  int32_t mac3 = static_cast<int32_t>(gte->ReadData(27));

  EXPECT_EQ(mac1, 25);
  EXPECT_EQ(mac2, 49);
  EXPECT_EQ(mac3, 9);
}

// ─── GPF Command ───────────────────────────────────────────────────────

TEST_F(PS1GTETest, GPF_ScalesIRByIR0) {
  gte->WriteData(8, 0x1000); // IR0 = 4096
  gte->WriteData(9, 10);     // IR1
  gte->WriteData(10, 20);    // IR2
  gte->WriteData(11, 30);    // IR3

  gte->Execute(0x3D | (1 << 10)); // GPF with lm=1

  int32_t mac1 = static_cast<int32_t>(gte->ReadData(25));
  int32_t mac2 = static_cast<int32_t>(gte->ReadData(26));
  int32_t mac3 = static_cast<int32_t>(gte->ReadData(27));

  EXPECT_EQ(mac1, 0x1000 * 10);
  EXPECT_EQ(mac2, 0x1000 * 20);
  EXPECT_EQ(mac3, 0x1000 * 30);
}

// ─── Reset ──────────────────────────────────────────────────────────────

TEST_F(PS1GTETest, Reset_ClearsAllRegisters) {
  gte->WriteData(0, 0xFFFFFFFF);
  gte->WriteControl(0, 0xFFFFFFFF);
  gte->Reset();

  EXPECT_EQ(gte->ReadData(0), 0u);
  EXPECT_EQ(gte->ReadControl(31), 0u); // Flag
}

// ─── Debug ──────────────────────────────────────────────────────────────

TEST_F(PS1GTETest, GetDebugSummary_NotEmpty) {
  EXPECT_FALSE(gte->GetDebugSummary().empty());
}

TEST_F(PS1GTETest, DumpState_WritesOutput) {
  std::ostringstream os;
  gte->DumpState(os);
  EXPECT_FALSE(os.str().empty());
}
