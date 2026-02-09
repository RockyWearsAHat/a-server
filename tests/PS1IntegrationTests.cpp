#include "emulator/ps1/PS1.h"
#include "emulator/ps1/PS1Constants.h"
#include <gtest/gtest.h>

using namespace AIO::Emulator::PS1;

class PS1IntegrationTest : public ::testing::Test {
protected:
  void SetUp() override { ps1 = std::make_unique<PS1>(); }
  std::unique_ptr<PS1> ps1;
};

// ─── Construction / Initialization ─────────────────────────────────────

TEST_F(PS1IntegrationTest, ConstructsSuccessfully) { EXPECT_NE(ps1, nullptr); }

TEST_F(PS1IntegrationTest, AllComponentsAccessible) {
  EXPECT_NE(&ps1->GetCPU(), nullptr);
  EXPECT_NE(&ps1->GetMemory(), nullptr);
  EXPECT_NE(&ps1->GetGPU(), nullptr);
  EXPECT_NE(&ps1->GetSPU(), nullptr);
  EXPECT_NE(&ps1->GetDMA(), nullptr);
  EXPECT_NE(&ps1->GetInterrupts(), nullptr);
  EXPECT_NE(&ps1->GetTimers(), nullptr);
  EXPECT_NE(&ps1->GetCDROM(), nullptr);
  EXPECT_NE(&ps1->GetController(), nullptr);
  EXPECT_NE(&ps1->GetGTE(), nullptr);
}

TEST_F(PS1IntegrationTest, InitialPC_AtResetVector) {
  EXPECT_EQ(ps1->GetPC(), CPU::RESET_VECTOR);
}

TEST_F(PS1IntegrationTest, BIOS_NotLoaded_Initially) {
  EXPECT_FALSE(ps1->IsBIOSLoaded());
}

TEST_F(PS1IntegrationTest, LoadBIOS_InvalidPath_ReturnsFalse) {
  EXPECT_FALSE(ps1->LoadBIOS("/nonexistent/bios.bin"));
}

TEST_F(PS1IntegrationTest, LoadDisc_InvalidPath_ReturnsFalse) {
  EXPECT_FALSE(ps1->LoadDisc("/nonexistent/game.bin"));
}

// ─── Reset ──────────────────────────────────────────────────────────────

TEST_F(PS1IntegrationTest, Reset_ClearsCycles) {
  // Write a NOP to the reset vector so Step doesn't crash
  ps1->GetMemory().WriteBIOS32(CPU::RESET_VECTOR & 0x1FFFFF, 0);
  ps1->GetMemory().WriteBIOS32((CPU::RESET_VECTOR & 0x1FFFFF) + 4, 0);

  ps1->Step();
  EXPECT_GT(ps1->GetTotalCycles(), 0u);

  ps1->Reset();
  EXPECT_EQ(ps1->GetTotalCycles(), 0u);
  EXPECT_EQ(ps1->GetPC(), CPU::RESET_VECTOR);
}

// ─── Step ──────────────────────────────────────────────────────────────

TEST_F(PS1IntegrationTest, Step_ReturnsCycles) {
  // Write NOP at reset vector
  ps1->GetMemory().WriteBIOS32(CPU::RESET_VECTOR & 0x1FFFFF, 0);
  ps1->GetMemory().WriteBIOS32((CPU::RESET_VECTOR & 0x1FFFFF) + 4, 0);

  int cycles = ps1->Step();
  EXPECT_GT(cycles, 0);
}

TEST_F(PS1IntegrationTest, Step_AdvancesPC) {
  uint32_t startPC = ps1->GetPC();
  ps1->GetMemory().WriteBIOS32(CPU::RESET_VECTOR & 0x1FFFFF, 0);
  ps1->GetMemory().WriteBIOS32((CPU::RESET_VECTOR & 0x1FFFFF) + 4, 0);

  ps1->Step();
  EXPECT_NE(ps1->GetPC(), startPC);
}

TEST_F(PS1IntegrationTest, Step_IncrementsTotalCycles) {
  ps1->GetMemory().WriteBIOS32(CPU::RESET_VECTOR & 0x1FFFFF, 0);
  ps1->GetMemory().WriteBIOS32((CPU::RESET_VECTOR & 0x1FFFFF) + 4, 0);

  uint64_t before = ps1->GetTotalCycles();
  ps1->Step();
  EXPECT_GT(ps1->GetTotalCycles(), before);
}

// ─── Memory Bus Integration ────────────────────────────────────────────

TEST_F(PS1IntegrationTest, ReadWriteMem32_RoundTrip) {
  ps1->WriteMem32(0x00001000, 0xDEADBEEF);
  EXPECT_EQ(ps1->ReadMem32(0x00001000), 0xDEADBEEFu);
}

// ─── Input ──────────────────────────────────────────────────────────────

TEST_F(PS1IntegrationTest, UpdateInput_DoesNotCrash) {
  ps1->UpdateInput(0xFFFF);
  ps1->UpdateInput(0x0000);
}

// ─── Framebuffer ────────────────────────────────────────────────────────

TEST_F(PS1IntegrationTest, GetFramebuffer_NotNull) {
  EXPECT_NE(ps1->GetFramebuffer(), nullptr);
}

TEST_F(PS1IntegrationTest, GetDisplayDimensions_NonZero) {
  EXPECT_GT(ps1->GetDisplayWidth(), 0u);
  EXPECT_GT(ps1->GetDisplayHeight(), 0u);
}

// ─── CPU-Memory Integration ────────────────────────────────────────────

TEST_F(PS1IntegrationTest, CPU_ExecutesFromBIOS) {
  uint32_t biosOffset = CPU::RESET_VECTOR & 0x1FFFFF;

  // ADDIU $1, $0, 42
  uint32_t addiu = (0x09 << 26) | (0 << 21) | (1 << 16) | 42;
  ps1->GetMemory().WriteBIOS32(biosOffset, addiu);
  ps1->GetMemory().WriteBIOS32(biosOffset + 4, 0); // NOP

  ps1->Step();
  EXPECT_EQ(ps1->GetCPU().GetRegister(1), 42u);
}

TEST_F(PS1IntegrationTest, CPU_WritesToRAM_VisibleViaDebugHelper) {
  uint32_t biosOffset = CPU::RESET_VECTOR & 0x1FFFFF;

  // LUI $1, 0x0000 (base for RAM at KUSEG)
  ps1->GetMemory().WriteBIOS32(biosOffset,
                               (0x0F << 26) | (0 << 21) | (1 << 16) | 0);
  // ADDIU $2, $0, 0x1234
  ps1->GetMemory().WriteBIOS32(biosOffset + 4,
                               (0x09 << 26) | (0 << 21) | (2 << 16) | 0x1234);
  // SW $2, 0x100($1)
  ps1->GetMemory().WriteBIOS32(biosOffset + 8,
                               (0x2B << 26) | (1 << 21) | (2 << 16) | 0x100);
  ps1->GetMemory().WriteBIOS32(biosOffset + 12, 0); // NOP

  ps1->Step(); // LUI
  ps1->Step(); // ADDIU
  ps1->Step(); // SW

  EXPECT_EQ(ps1->ReadMem32(0x100), 0x1234u);
}

// ─── Interrupt Integration ─────────────────────────────────────────────

TEST_F(PS1IntegrationTest, IRQ_VBlankFires_AfterEnoughTicks) {
  // Write a tight loop at reset vector: J 0xBFC00000 + delay slot NOP
  uint32_t biosOffset = CPU::RESET_VECTOR & 0x1FFFFF;
  // J 0xBFC00000: opcode=2, target26 = (0xBFC00000 >> 2) & 0x03FFFFFF =
  // 0x03F00000
  ps1->GetMemory().WriteBIOS32(biosOffset, 0x0BF00000);     // J self
  ps1->GetMemory().WriteBIOS32(biosOffset + 4, 0x00000000); // NOP (delay slot)

  // Enable VBlank IRQ mask
  ps1->GetInterrupts().WriteMask(IRQ::VBLANK);

  // Need 240+ scanlines for VBlank. Each scanline ≈ 2171 CPU cycles.
  for (int i = 0; i < 600000; i++) {
    ps1->Step();
  }

  bool gotVBlank = (ps1->GetInterrupts().ReadStat() & IRQ::VBLANK) != 0;
  EXPECT_TRUE(gotVBlank);
}

// ─── Multi-Step Execution ──────────────────────────────────────────────

TEST_F(PS1IntegrationTest, MultipleSteps_NoAssertionFailure) {
  uint32_t biosOffset = CPU::RESET_VECTOR & 0x1FFFFF;
  // Write a loop of NOPs
  for (int i = 0; i < 1000; i++) {
    ps1->GetMemory().WriteBIOS32(biosOffset + i * 4, 0);
  }

  for (int i = 0; i < 500; i++) {
    ps1->Step();
  }

  EXPECT_GT(ps1->GetTotalCycles(), 0u);
  EXPECT_GT(ps1->GetCPU().GetInstructionCount(), 0u);
}
