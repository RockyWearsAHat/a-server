#include "emulator/ps1/InterruptController.h"
#include "emulator/ps1/PS1Constants.h"
#include "emulator/ps1/PS1Memory.h"
#include "emulator/ps1/PS1SPU.h"
#include <gtest/gtest.h>

using namespace AIO::Emulator::PS1;

class PS1SPUTest : public ::testing::Test {
protected:
  void SetUp() override {
    irq = std::make_unique<InterruptController>();
    memory = std::make_unique<PS1Memory>();
    spu = std::make_unique<PS1SPU>(*memory, *irq);
  }
  std::unique_ptr<InterruptController> irq;
  std::unique_ptr<PS1Memory> memory;
  std::unique_ptr<PS1SPU> spu;
};

// ─── Initial State ──────────────────────────────────────────────────────

TEST_F(PS1SPUTest, InitialState_StatusRegister) {
  uint16_t status = spu->ReadRegister(0x1F801DAE);
  EXPECT_EQ(status & 0x3F, 0u);
}

// ─── Voice Register Access ─────────────────────────────────────────────

TEST_F(PS1SPUTest, WriteVoiceVolume_ReadBack) {
  spu->WriteRegister(0x1F801C00, 0x3FFF);
  EXPECT_EQ(spu->ReadRegister(0x1F801C00), 0x3FFFu);
}

TEST_F(PS1SPUTest, WriteVoicePitch_ReadBack) {
  spu->WriteRegister(0x1F801C04, 0x1000);
  EXPECT_EQ(spu->ReadRegister(0x1F801C04), 0x1000u);
}

TEST_F(PS1SPUTest, MultipleVoices_Independent) {
  spu->WriteRegister(0x1F801C00, 0x1111);
  spu->WriteRegister(0x1F801C10, 0x2222);

  EXPECT_EQ(spu->ReadRegister(0x1F801C00), 0x1111u);
  EXPECT_EQ(spu->ReadRegister(0x1F801C10), 0x2222u);
}

// ─── Global Registers ──────────────────────────────────────────────────

TEST_F(PS1SPUTest, WriteMainVolume_ReadBack) {
  spu->WriteRegister(0x1F801D80, 0x3FFF);
  spu->WriteRegister(0x1F801D82, 0x3FFF);
  EXPECT_EQ(spu->ReadRegister(0x1F801D80), 0x3FFFu);
  EXPECT_EQ(spu->ReadRegister(0x1F801D82), 0x3FFFu);
}

TEST_F(PS1SPUTest, SPUControl_ReadWrite) {
  spu->WriteRegister(0x1F801DAA, 0xC000);
  EXPECT_EQ(spu->ReadRegister(0x1F801DAA), 0xC000u);
}

// ─── DMA Interface ──────────────────────────────────────────────────────

TEST_F(PS1SPUTest, DMAWrite_IncreasesTransferAddress) {
  spu->WriteRegister(0x1F801DA6, 0x100);
  spu->DMAWrite(0x1234);
  spu->DMAWrite(0x5678);
}

TEST_F(PS1SPUTest, DMARead_ReturnsData) {
  spu->WriteRegister(0x1F801DA6, 0x100);
  spu->DMAWrite(0xABCD);

  spu->WriteRegister(0x1F801DA6, 0x100);
  uint16_t value = spu->DMARead();
  EXPECT_EQ(value, 0xABCDu);
}

// ─── Audio Output ──────────────────────────────────────────────────────

TEST_F(PS1SPUTest, GetSamples_ProducesSilenceInitially) {
  int16_t buffer[64] = {};
  uint32_t samplesWritten = spu->GetSamples(buffer, 32);
  EXPECT_EQ(samplesWritten, 32u);

  bool allZero = true;
  for (uint32_t i = 0; i < samplesWritten * 2; i++) {
    if (buffer[i] != 0) {
      allZero = false;
      break;
    }
  }
  EXPECT_TRUE(allZero);
}

// ─── Key On/Off ────────────────────────────────────────────────────────

TEST_F(PS1SPUTest, KeyOn_SetsVoiceActive) {
  spu->WriteRegister(0x1F801D88, 0x0001);
}

TEST_F(PS1SPUTest, KeyOff_SetsVoiceRelease) {
  spu->WriteRegister(0x1F801D88, 0x0001);
  spu->WriteRegister(0x1F801D8C, 0x0001);
}

// ─── Reset ──────────────────────────────────────────────────────────────

TEST_F(PS1SPUTest, Reset_ClearsState) {
  spu->WriteRegister(0x1F801C00, 0x3FFF);
  spu->Reset();
  EXPECT_EQ(spu->ReadRegister(0x1F801C00), 0u);
}

// ─── Debug ──────────────────────────────────────────────────────────────

TEST_F(PS1SPUTest, GetDebugSummary_NotEmpty) {
  EXPECT_FALSE(spu->GetDebugSummary().empty());
}
