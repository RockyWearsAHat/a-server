#include "emulator/ps1/PS1Constants.h"
#include "emulator/ps1/PS1GPU.h"
#include "emulator/ps1/PS1Memory.h"
#include <gtest/gtest.h>

using namespace AIO::Emulator::PS1;

class PS1GPUTest : public ::testing::Test {
protected:
  void SetUp() override {
    memory = std::make_unique<PS1Memory>();
    gpu = std::make_unique<PS1GPU>(*memory);
  }
  std::unique_ptr<PS1Memory> memory;
  std::unique_ptr<PS1GPU> gpu;
};

// ─── Initial State ──────────────────────────────────────────────────────

TEST_F(PS1GPUTest, InitialState_NotInVBlank) {
  // After reset, scanline should start at 0
  EXPECT_EQ(gpu->GetScanline(), 0u);
}

TEST_F(PS1GPUTest, GetFramebuffer_NotNull) {
  EXPECT_NE(gpu->GetFramebuffer(), nullptr);
}

TEST_F(PS1GPUTest, DisplayDimensions_Default) {
  EXPECT_GT(gpu->GetDisplayWidth(), 0u);
  EXPECT_GT(gpu->GetDisplayHeight(), 0u);
}

// ─── GPUSTAT ────────────────────────────────────────────────────────────

TEST_F(PS1GPUTest, ReadGPUSTAT_ReturnsValidValue) {
  uint32_t stat = gpu->ReadGPUSTAT();
  // Bit 26 should be set (ready to receive cmd)
  EXPECT_NE(stat & (1 << 26), 0u);
}

TEST_F(PS1GPUTest, GPUSTAT_ReadyToSendVRAM_Initially) {
  uint32_t stat = gpu->ReadGPUSTAT();
  // Bit 27 — ready to send VRAM data
  EXPECT_NE(stat & (1 << 27), 0u);
}

// ─── GP1 Commands ──────────────────────────────────────────────────────

TEST_F(PS1GPUTest, GP1_Reset) {
  gpu->WriteGP1(0x00000000); // GP1(00h) — Reset
  uint32_t stat = gpu->ReadGPUSTAT();
  EXPECT_NE(stat & (1 << 26), 0u); // Should still be ready
}

TEST_F(PS1GPUTest, GP1_DisplayEnable) {
  gpu->WriteGP1(0x03000000); // GP1(03h) — Display enable (bit 0 = 0 → enable)
  uint32_t stat = gpu->ReadGPUSTAT();
  // Bit 23 should be 0 (display enabled)
  EXPECT_EQ(stat & (1 << 23), 0u);
}

TEST_F(PS1GPUTest, GP1_DisplayDisable) {
  gpu->WriteGP1(0x03000001); // GP1(03h) — Display disable (bit 0 = 1)
  uint32_t stat = gpu->ReadGPUSTAT();
  EXPECT_NE(stat & (1 << 23), 0u);
}

TEST_F(PS1GPUTest, GP1_DMADirection) {
  gpu->WriteGP1(0x04000002); // GP1(04h) — DMA Direction = 2 (GPU → CPU)
  uint32_t stat = gpu->ReadGPUSTAT();
  uint32_t dmaDir = (stat >> 29) & 3;
  EXPECT_EQ(dmaDir, 2u);
}

// ─── GP0 Commands ──────────────────────────────────────────────────────

TEST_F(PS1GPUTest, GP0_NOP_DoesNotCrash) {
  gpu->WriteGP0(0x00000000); // NOP
}

TEST_F(PS1GPUTest, GP0_FillRect_DoesNotCrash) {
  gpu->WriteGP0(0x02000000); // Fill rect command (color = black)
  gpu->WriteGP0(0x00000000); // Top-left (0,0)
  gpu->WriteGP0(0x00100010); // Size (16x16)
}

TEST_F(PS1GPUTest, GP0_MonoTriangle_DoesNotCrash) {
  gpu->WriteGP0(0x20FF0000); // Mono triangle, color = red
  gpu->WriteGP0(0x00100010); // Vertex 0 (16,16)
  gpu->WriteGP0(0x00200010); // Vertex 1 (16,32)
  gpu->WriteGP0(0x00100020); // Vertex 2 (32,16)
}

TEST_F(PS1GPUTest, GP0_MonoQuad_DoesNotCrash) {
  gpu->WriteGP0(0x2800FF00); // Mono quad, color = green
  gpu->WriteGP0(0x00100010); // Vertex 0
  gpu->WriteGP0(0x00200010); // Vertex 1
  gpu->WriteGP0(0x00100020); // Vertex 2
  gpu->WriteGP0(0x00200020); // Vertex 3
}

// ─── Tick / VBlank ──────────────────────────────────────────────────────

TEST_F(PS1GPUTest, Tick_IncrementsScanline) {
  gpu->Tick(2171 * 5); // 2171 CPU cycles per scanline (NTSC)
  EXPECT_GT(gpu->GetScanline(), 0u);
}

TEST_F(PS1GPUTest, VBlank_OccursAtCorrectScanline) {
  // NTSC: VBlank starts at scanline 240, tick 250 scanlines to enter VBlank
  for (int i = 0; i < 250; i++) {
    gpu->Tick(2171); // One scanline worth of CPU cycles
  }
  EXPECT_TRUE(gpu->InVBlank());
}

// ─── DMA Interface ──────────────────────────────────────────────────────

TEST_F(PS1GPUTest, DMAReady_WhenDirectionSet) {
  // DMA direction defaults to 0 (off), so not ready initially
  EXPECT_FALSE(gpu->DMAReady());

  // GP1(04h) = 0x02: set DMA direction to CPU->GP0
  gpu->WriteGP1(0x04000002);
  EXPECT_TRUE(gpu->DMAReady());
}

TEST_F(PS1GPUTest, DMAWrite_DoesNotCrash) {
  gpu->DMAWrite(0x02000000); // Treat as GP0 command
}

// ─── Reset ──────────────────────────────────────────────────────────────

TEST_F(PS1GPUTest, Reset_ClearsState) {
  gpu->Tick(100000);
  gpu->Reset();
  EXPECT_EQ(gpu->GetScanline(), 0u);
}

// ─── Debug ──────────────────────────────────────────────────────────────

TEST_F(PS1GPUTest, GetDebugSummary_NotEmpty) {
  EXPECT_FALSE(gpu->GetDebugSummary().empty());
}
