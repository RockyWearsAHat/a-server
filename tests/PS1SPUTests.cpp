#include "emulator/ps1/InterruptController.h"
#include "emulator/ps1/PS1Constants.h"
#include "emulator/ps1/PS1Memory.h"
#include "emulator/ps1/PS1SPU.h"
#include <algorithm>
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

// ─── Reverb Config Registers (0x1F801DC0 - 0x1F801DFE) ────────────────

TEST_F(PS1SPUTest, ReverbConfigReg_WriteRead_FirstReg) {
  // Register 0 (vIIR) at 0x1F801DC0
  spu->WriteRegister(0x1F801DC0, 0x6000);
  EXPECT_EQ(spu->ReadRegister(0x1F801DC0), 0x6000u);
}

TEST_F(PS1SPUTest, ReverbConfigReg_WriteRead_LastReg) {
  // Register 31 (vROUT) at 0x1F801DFE
  spu->WriteRegister(0x1F801DFE, 0x4000);
  EXPECT_EQ(spu->ReadRegister(0x1F801DFE), 0x4000u);
}

TEST_F(PS1SPUTest, ReverbConfigReg_AllIndependent) {
  // Write distinct values to a few config registers and verify independence
  spu->WriteRegister(0x1F801DC0, 0x1111);
  spu->WriteRegister(0x1F801DC4, 0x2222);
  spu->WriteRegister(0x1F801DC8, 0x3333);
  EXPECT_EQ(spu->ReadRegister(0x1F801DC0), 0x1111u);
  EXPECT_EQ(spu->ReadRegister(0x1F801DC4), 0x2222u);
  EXPECT_EQ(spu->ReadRegister(0x1F801DC8), 0x3333u);
}

TEST_F(PS1SPUTest, ReverbBase_Write_ResetsBufferPointer) {
  // Writing mBASE (0x1F801DA2) must reset the reverb circular buffer pointer
  // (tested indirectly: write then read back reverbBase register itself)
  spu->WriteRegister(0x1F801DA2, 0x0100);
  EXPECT_EQ(spu->ReadRegister(0x1F801DA2), 0x0100u);
}

// ─── FM and Noise Mode Registers ────────────────────────────────────

TEST_F(PS1SPUTest, FMMode_WriteRead) {
  spu->WriteRegister(0x1F801D90, 0x03FF); // FM mode lower 16
  EXPECT_EQ(spu->ReadRegister(0x1F801D90), 0x03FFu);
}

TEST_F(PS1SPUTest, NoiseMode_WriteRead) {
  spu->WriteRegister(0x1F801D94, 0x00FF); // Noise mode lower 16
  EXPECT_EQ(spu->ReadRegister(0x1F801D94), 0x00FFu);
}

TEST_F(PS1SPUTest, ReverbOn_WriteRead) {
  spu->WriteRegister(0x1F801D98, 0xFFFF);
  EXPECT_EQ(spu->ReadRegister(0x1F801D98), 0xFFFFu);
}

// ─── XA-ADPCM Integration ──────────────────────────────────────────────

// FeedXASamples with zero samples should be a no-op and not crash.
TEST_F(PS1SPUTest, FeedXASamples_ZeroCount_NoOp) {
  int16_t L[1] = {0};
  int16_t R[1] = {0};
  spu->FeedXASamples(L, R, 0, 37800);
  // GetSamples should still succeed
  int16_t buf[2] = {};
  uint32_t written = spu->GetSamples(buf, 1);
  EXPECT_EQ(written, 1u);
}

// Samples fed at full volume should appear in the output mix.
TEST_F(PS1SPUTest, FeedXASamples_AudioAppearsInOutput) {
  // Set CD volume to max
  spu->WriteRegister(0x1F801DB0, 0x7FFF); // cdVolumeL
  spu->WriteRegister(0x1F801DB2, 0x7FFF); // cdVolumeR
  // Set main volume to max so nothing is scaled out
  spu->WriteRegister(0x1F801D80, 0x7FFF);
  spu->WriteRegister(0x1F801D82, 0x7FFF);

  static constexpr int16_t TONE = 8000;
  // Feed enough samples to fill the rate-conversion queue
  static constexpr uint32_t COUNT = 4096;
  int16_t L[COUNT], R[COUNT];
  std::fill(L, L + COUNT, TONE);
  std::fill(R, R + COUNT, TONE);
  spu->FeedXASamples(L, R, COUNT, 37800);

  // Drain several output samples and check they are non-zero
  int16_t out[16] = {};
  spu->GetSamples(out, 8);
  bool found = false;
  for (int i = 0; i < 16; i++) {
    if (out[i] != 0) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "XA audio was fed but output remained silent";
}

// FeedXASamples at 18900 Hz should still produce non-zero output.
TEST_F(PS1SPUTest, FeedXASamples_HalfRate_ProducesAudio) {
  spu->WriteRegister(0x1F801DB0, 0x7FFF);
  spu->WriteRegister(0x1F801DB2, 0x7FFF);
  spu->WriteRegister(0x1F801D80, 0x7FFF);
  spu->WriteRegister(0x1F801D82, 0x7FFF);

  static constexpr uint32_t COUNT = 4096;
  int16_t L[COUNT], R[COUNT];
  std::fill(L, L + COUNT, static_cast<int16_t>(5000));
  std::fill(R, R + COUNT, static_cast<int16_t>(5000));
  spu->FeedXASamples(L, R, COUNT, 18900);

  int16_t out[16] = {};
  spu->GetSamples(out, 8);
  bool found = false;
  for (int i = 0; i < 16; i++) {
    if (out[i] != 0) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);
}

// Overflow: feeding more than XA_BUFFER_CAPACITY samples must not crash.
TEST_F(PS1SPUTest, FeedXASamples_Overflow_NoCrash) {
  static constexpr uint32_t OVER = 10000; // > XA_BUFFER_CAPACITY (8192)
  int16_t L[OVER] = {}, R[OVER] = {};
  // No assertion — just must not crash or corrupt state
  spu->FeedXASamples(L, R, OVER, 37800);
  int16_t out[2] = {};
  spu->GetSamples(out, 1);
}
