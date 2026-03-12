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

TEST_F(PS1GPUTest, DisplayWidth_UsesDisplayRangeForCurrentMode) {
  gpu->WriteGP1(0x08000001); // GP1(08h) — 320-pixel mode

  const uint32_t x1 = 0x260;
  const uint32_t x2 = x1 + 160 * 8;
  gpu->WriteGP1(0x06000000 | x1 | (x2 << 12));

  EXPECT_EQ(gpu->GetDisplayWidth(), 160u);
}

TEST_F(PS1GPUTest, DisplayHeight_UsesVerticalDisplayRange) {
  const uint32_t y1 = 0x20;
  const uint32_t y2 = 0x90;
  gpu->WriteGP1(0x07000000 | y1 | (y2 << 10));

  EXPECT_EQ(gpu->GetDisplayHeight(), y2 - y1);
}

TEST_F(PS1GPUTest, TexturedRect_XFlipSamplesTextureBackwards) {
  gpu->WriteGP0(0xE3000000);
  gpu->WriteGP0(0xE4007FFF);
  gpu->WriteGP0(0xE1001100);

  gpu->WriteVRAM(4, 8, 0x001F);
  gpu->WriteVRAM(5, 8, 0x03E0);

  gpu->WriteGP0(0x65000000);
  gpu->WriteGP0(0x00000000);
  gpu->WriteGP0((8u << 8) | 4u);
  gpu->WriteGP0((1u << 16) | 2u);

  EXPECT_EQ(gpu->ReadVRAM(0, 0), 0x001F);
  EXPECT_EQ(gpu->ReadVRAM(1, 0), 0x0000);
}

TEST_F(PS1GPUTest, TexturedRect_YFlipSamplesTextureBackwards) {
  gpu->WriteGP0(0xE3000000);
  gpu->WriteGP0(0xE4007FFF);
  gpu->WriteGP0(0xE1002100);

  gpu->WriteVRAM(12, 6, 0x03E0);
  gpu->WriteVRAM(12, 7, 0x7C00);

  gpu->WriteGP0(0x65000000);
  gpu->WriteGP0(0x00000000);
  gpu->WriteGP0((7u << 8) | 12u);
  gpu->WriteGP0((2u << 16) | 1u);

  EXPECT_EQ(gpu->ReadVRAM(0, 0), 0x7C00);
  EXPECT_EQ(gpu->ReadVRAM(0, 1), 0x03E0);
}

TEST_F(PS1GPUTest, MonoRect_IgnoresGarbageBitsInVariableSizeWord) {
  gpu->WriteGP0(0xE3000000);
  gpu->WriteGP0(0xE4007FFF);

  gpu->WriteGP0(0x60FF0000);
  gpu->WriteGP0(0x00000000);
  gpu->WriteGP0((1u << 16) | 0x0401u);

  EXPECT_EQ(gpu->ReadVRAM(0, 0), 0x7C00u);
  EXPECT_EQ(gpu->ReadVRAM(1, 0), 0x0000u);
}

TEST_F(PS1GPUTest, TexturedRect_IgnoresGarbageBitsInVariableSizeWord) {
  gpu->WriteGP0(0xE3000000);
  gpu->WriteGP0(0xE4007FFF);
  gpu->WriteGP0(0xE1001100);

  gpu->WriteVRAM(4, 8, 0x001F);

  gpu->WriteGP0(0x65000000);
  gpu->WriteGP0(0x00000000);
  gpu->WriteGP0((8u << 8) | 4u);
  gpu->WriteGP0((1u << 16) | 0x0401u);

  EXPECT_EQ(gpu->ReadVRAM(0, 0), 0x001Fu);
  EXPECT_EQ(gpu->ReadVRAM(1, 0), 0x0000u);
}

TEST_F(PS1GPUTest, CopyRectVRAMtoVRAM_HandlesOverlappingRegions) {
  gpu->WriteVRAM(10, 10, 0x001F);
  gpu->WriteVRAM(11, 10, 0x03E0);
  gpu->WriteVRAM(12, 10, 0x7C00);

  gpu->WriteGP0(0x80000000);
  gpu->WriteGP0((10u << 16) | 10u);
  gpu->WriteGP0((10u << 16) | 11u);
  gpu->WriteGP0((1u << 16) | 3u);

  EXPECT_EQ(gpu->ReadVRAM(11, 10), 0x001Fu);
  EXPECT_EQ(gpu->ReadVRAM(12, 10), 0x03E0u);
  EXPECT_EQ(gpu->ReadVRAM(13, 10), 0x7C00u);
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

TEST_F(PS1GPUTest, GP0_MonoTriangle_IsWindingInvariant) {
  gpu->WriteGP0(0xE3000000);
  gpu->WriteGP0(0xE4007FFF);

  gpu->WriteGP0(0x20FF0000); // Mono triangle, color = red
  gpu->WriteGP0(0x00000000); // Vertex 0 = (0,0)
  gpu->WriteGP0(0x00000002); // Vertex 1 = (2,0)
  gpu->WriteGP0(0x00020000); // Vertex 2 = (0,2)

  const uint16_t ccwEdge = gpu->ReadVRAM(1, 0);
  const uint16_t ccwInterior = gpu->ReadVRAM(0, 1);

  gpu->Reset();
  gpu->WriteGP0(0xE3000000);
  gpu->WriteGP0(0xE4007FFF);

  gpu->WriteGP0(0x20FF0000); // Same triangle with reversed winding
  gpu->WriteGP0(0x00000000); // Vertex 0 = (0,0)
  gpu->WriteGP0(0x00020000); // Vertex 1 = (0,2)
  gpu->WriteGP0(0x00000002); // Vertex 2 = (2,0)

  EXPECT_EQ(gpu->ReadVRAM(1, 0), ccwEdge);
  EXPECT_EQ(gpu->ReadVRAM(0, 1), ccwInterior);
}

TEST_F(PS1GPUTest, GP0_MonoTriangles_UseLowerRightEdgeOwnership) {
  gpu->WriteGP0(0xE3000000);
  gpu->WriteGP0(0xE4007FFF);

  gpu->WriteGP0(0x20FF0000); // Blue triangle
  gpu->WriteGP0(0x00000000); // (0,0)
  gpu->WriteGP0(0x00000002); // (2,0)
  gpu->WriteGP0(0x00020000); // (0,2)

  gpu->WriteGP0(0x2000FF00); // Green triangle sharing the same diagonal
  gpu->WriteGP0(0x00000002); // (2,0)
  gpu->WriteGP0(0x00020000); // (0,2)
  gpu->WriteGP0(0x00020002); // (2,2)

  EXPECT_EQ(gpu->ReadVRAM(1, 1), 0x03E0u);
}

TEST_F(PS1GPUTest, GP0_TexturedTriangles_UseLowerRightEdgeOwnership) {
  gpu->WriteGP0(0xE3000000);
  gpu->WriteGP0(0xE4007FFF);
  gpu->WriteGP0(0xE1001100);

  gpu->WriteVRAM(4, 8, 0x001F);
  gpu->WriteVRAM(5, 8, 0x03E0);

  gpu->WriteGP0(0x25000000); // Raw textured triangle
  gpu->WriteGP0(0x00000000); // (0,0)
  gpu->WriteGP0((8u << 8) | 4u);
  gpu->WriteGP0(0x00000002); // (2,0)
  gpu->WriteGP0((0x1100u << 16) | (8u << 8) | 4u);
  gpu->WriteGP0(0x00020000); // (0,2)
  gpu->WriteGP0((8u << 8) | 4u);

  gpu->WriteGP0(0x25000000); // Raw textured triangle sharing the same diagonal
  gpu->WriteGP0(0x00000002); // (2,0)
  gpu->WriteGP0((8u << 8) | 5u);
  gpu->WriteGP0(0x00020000); // (0,2)
  gpu->WriteGP0((0x1100u << 16) | (8u << 8) | 5u);
  gpu->WriteGP0(0x00020002); // (2,2)
  gpu->WriteGP0((8u << 8) | 5u);

  EXPECT_EQ(gpu->ReadVRAM(1, 1), 0x03E0u);
}

TEST_F(PS1GPUTest, GP0_MonoTriangle_RendersWhenTallTriangleCrossesVisibleArea) {
  gpu->WriteGP0(0xE3000000);
  gpu->WriteGP0(0xE407FFFF);

  gpu->WriteGP0(0x20FF0000); // Red triangle
  gpu->WriteGP0((static_cast<uint32_t>(-100) & 0x7FF) << 16 | 100u);
  gpu->WriteGP0((500u << 16) | 50u);
  gpu->WriteGP0((500u << 16) | 150u);

  EXPECT_EQ(gpu->ReadVRAM(100, 300), 0x7C00u);
}

TEST_F(PS1GPUTest, LatchedFramebuffer_UsesContiguousDisplaySnapshot) {
  gpu->WriteGP1(0x08000002); // 512-pixel mode
  gpu->WriteGP1(0x05000200); // Display start X = 512
  gpu->WriteVRAM(512, 0, 0x001Fu);
  gpu->WriteVRAM(513, 0, 0x03E0u);

  gpu->LatchDisplayBuffer();

  const uint16_t *latched = gpu->GetFramebuffer();
  ASSERT_NE(latched, nullptr);
  EXPECT_EQ(gpu->GetFramebufferStride(), gpu->GetDisplayWidth());
  EXPECT_EQ(latched[0], 0x001Fu);
  EXPECT_EQ(latched[1], 0x03E0u);

  gpu->WriteVRAM(512, 0, 0x7C00u);
  EXPECT_EQ(latched[0], 0x001Fu);

  gpu->LatchDisplayBuffer();
  EXPECT_EQ(gpu->GetFramebuffer()[0], 0x7C00u);
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
