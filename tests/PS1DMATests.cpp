#include "emulator/ps1/InterruptController.h"
#include "emulator/ps1/PS1Constants.h"
#include "emulator/ps1/PS1DMA.h"
#include "emulator/ps1/PS1GPU.h"
#include "emulator/ps1/PS1Memory.h"
#include "emulator/ps1/PS1SPU.h"
#include <gtest/gtest.h>

using namespace AIO::Emulator::PS1;

class PS1DMATest : public ::testing::Test {
protected:
  void SetUp() override {
    irq = std::make_unique<InterruptController>();
    memory = std::make_unique<PS1Memory>();
    gpu = std::make_unique<PS1GPU>(*memory);
    spu = std::make_unique<PS1SPU>(*memory);
    dma = std::make_unique<PS1DMA>(*memory, *gpu, *spu, *irq);
  }
  std::unique_ptr<InterruptController> irq;
  std::unique_ptr<PS1Memory> memory;
  std::unique_ptr<PS1GPU> gpu;
  std::unique_ptr<PS1SPU> spu;
  std::unique_ptr<PS1DMA> dma;
};

// ─── Initial State ──────────────────────────────────────────────────────

TEST_F(PS1DMATest, InitialState_DPCRDefault) {
  // DPCR default = 0x07654321 (all channels priority defaults)
  uint32_t dpcr = dma->Read32(0x1F8010F0);
  EXPECT_EQ(dpcr, 0x07654321u);
}

TEST_F(PS1DMATest, InitialState_DICRZero) {
  uint32_t dicr = dma->Read32(0x1F8010F4);
  EXPECT_EQ(dicr, 0u);
}

// ─── Channel Register Read/Write ───────────────────────────────────────

TEST_F(PS1DMATest, WriteBaseAddr_ReadBack) {
  // DMA channel 2 (GPU) base address
  dma->Write32(0x1F8010A0, 0x00012000);
  EXPECT_EQ(dma->Read32(0x1F8010A0), 0x00012000u);
}

TEST_F(PS1DMATest, WriteBlockControl_ReadBack) {
  dma->Write32(0x1F8010A4, 0x00040001);
  EXPECT_EQ(dma->Read32(0x1F8010A4), 0x00040001u);
}

// ─── DPCR ──────────────────────────────────────────────────────────────

TEST_F(PS1DMATest, WriteDPCR_ReadBack) {
  dma->Write32(0x1F8010F0, 0x88888888);
  EXPECT_EQ(dma->Read32(0x1F8010F0), 0x88888888u);
}

// ─── DICR ──────────────────────────────────────────────────────────────

TEST_F(PS1DMATest, WriteDICR_ReadBack) {
  dma->Write32(0x1F8010F4, 0x00FF0000);
  uint32_t dicr = dma->Read32(0x1F8010F4);
  // Only specific bits are writable
  EXPECT_NE(dicr, 0u);
}

// ─── OTC (Channel 6) ──────────────────────────────────────────────────

TEST_F(PS1DMATest, OTC_ClearsOrderingTable) {
  // Enable channel 6 in DPCR
  dma->Write32(0x1F8010F0, 0x08888888);

  // Set up OTC DMA — fills ordering table in RAM
  uint32_t baseAddr = 0x100;
  uint32_t numEntries = 4;

  dma->Write32(0x1F8010E0, baseAddr);   // Base addr
  dma->Write32(0x1F8010E4, numEntries); // Block control
  dma->Write32(0x1F8010E8, 0x11000002); // Control: from RAM, manual, start

  // OTC builds a linked list from baseAddr downward:
  // [baseAddr] -> [baseAddr-4] -> ... -> [baseAddr-(n-1)*4] = 0x00FFFFFF
  // First entry points to next entry below it
  uint32_t firstEntry = memory->ReadRAM32(baseAddr);
  EXPECT_EQ(firstEntry, (baseAddr - 4) & 0x00FFFFFFu);

  // Last entry (lowest address) is the terminator
  uint32_t lastAddr = baseAddr - (numEntries - 1) * 4;
  uint32_t lastEntry = memory->ReadRAM32(lastAddr);
  EXPECT_EQ(lastEntry, 0x00FFFFFFu);
}

// ─── Reset ──────────────────────────────────────────────────────────────

TEST_F(PS1DMATest, Reset_RestoresDefaults) {
  dma->Write32(0x1F8010F0, 0xFFFFFFFF);
  dma->Reset();
  EXPECT_EQ(dma->Read32(0x1F8010F0), 0x07654321u);
}

// ─── Debug ──────────────────────────────────────────────────────────────

TEST_F(PS1DMATest, GetDebugSummary_NotEmpty) {
  EXPECT_FALSE(dma->GetDebugSummary().empty());
}
