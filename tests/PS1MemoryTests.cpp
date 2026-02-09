#include "emulator/ps1/PS1Constants.h"
#include "emulator/ps1/PS1Memory.h"
#include <gtest/gtest.h>

using namespace AIO::Emulator::PS1;

class PS1MemoryTest : public ::testing::Test {
protected:
  void SetUp() override { memory = std::make_unique<PS1Memory>(); }
  std::unique_ptr<PS1Memory> memory;
};

// ─── RAM Read/Write ────────────────────────────────────────────────────

TEST_F(PS1MemoryTest, WriteRAM32_ReadRAM32_RoundTrip) {
  memory->WriteRAM32(0x100, 0xDEADBEEF);
  EXPECT_EQ(memory->ReadRAM32(0x100), 0xDEADBEEFu);
}

TEST_F(PS1MemoryTest, WriteRAM32_Wraps_Within_2MB) {
  memory->WriteRAM32(0x0, 0x12345678);
  // RAM is 2MB, offset should wrap
  EXPECT_EQ(memory->ReadRAM32(0x0), 0x12345678u);
}

TEST_F(PS1MemoryTest, MultipleRAMWrites_Independent) {
  memory->WriteRAM32(0x100, 0xAAAAAAAA);
  memory->WriteRAM32(0x200, 0xBBBBBBBB);
  EXPECT_EQ(memory->ReadRAM32(0x100), 0xAAAAAAAAu);
  EXPECT_EQ(memory->ReadRAM32(0x200), 0xBBBBBBBBu);
}

// ─── BIOS Access ───────────────────────────────────────────────────────

TEST_F(PS1MemoryTest, WriteBIOS32_ReadBack_Via_Bus) {
  memory->WriteBIOS32(0x0, 0xCAFEBABE);
  // BIOS is at 0x1FC00000 (KSEG1 mapped = 0xBFC00000)
  uint32_t value = memory->Read32(0xBFC00000);
  EXPECT_EQ(value, 0xCAFEBABEu);
}

TEST_F(PS1MemoryTest, BIOSReadViaKSEG0) {
  memory->WriteBIOS32(0x10, 0x11223344);
  // KSEG0 maps 0x9FC00000 → physical 0x1FC00000
  uint32_t value = memory->Read32(0x9FC00010);
  EXPECT_EQ(value, 0x11223344u);
}

// ─── Scratchpad ────────────────────────────────────────────────────────

TEST_F(PS1MemoryTest, Scratchpad_ReadWrite_RoundTrip) {
  memory->WriteScratchpad32(0x0, 0xFEEDFACE);
  EXPECT_EQ(memory->ReadScratchpad32(0x0), 0xFEEDFACEu);
}

TEST_F(PS1MemoryTest, Scratchpad_MultipleOffsets) {
  memory->WriteScratchpad32(0x00, 0x11111111);
  memory->WriteScratchpad32(0x04, 0x22222222);
  memory->WriteScratchpad32(0x08, 0x33333333);
  EXPECT_EQ(memory->ReadScratchpad32(0x00), 0x11111111u);
  EXPECT_EQ(memory->ReadScratchpad32(0x04), 0x22222222u);
  EXPECT_EQ(memory->ReadScratchpad32(0x08), 0x33333333u);
}

// ─── Address Translation ───────────────────────────────────────────────

TEST_F(PS1MemoryTest, KUSEG_ReadWriteRAM) {
  memory->Write32(0x00000100, 0xAABBCCDD);
  EXPECT_EQ(memory->Read32(0x00000100), 0xAABBCCDDu);
}

TEST_F(PS1MemoryTest, KSEG0_MapsToRAM) {
  memory->Write32(0x80000100, 0x12345678);
  // Should be same physical location as KUSEG
  EXPECT_EQ(memory->Read32(0x00000100), 0x12345678u);
}

TEST_F(PS1MemoryTest, KSEG1_MapsToRAM) {
  memory->Write32(0xA0000100, 0xABCDEF01);
  EXPECT_EQ(memory->Read32(0x00000100), 0xABCDEF01u);
}

TEST_F(PS1MemoryTest, AllMirrors_SamePhysical) {
  memory->Write32(0x00000200, 0xCAFE0001);
  EXPECT_EQ(memory->Read32(0x80000200), 0xCAFE0001u);
  EXPECT_EQ(memory->Read32(0xA0000200), 0xCAFE0001u);
}

// ─── Byte/Halfword Access ──────────────────────────────────────────────

TEST_F(PS1MemoryTest, Write8_Read8) {
  memory->Write8(0x00000300, 0x42);
  EXPECT_EQ(memory->Read8(0x00000300), 0x42u);
}

TEST_F(PS1MemoryTest, Write16_Read16) {
  memory->Write16(0x00000300, 0x1234);
  EXPECT_EQ(memory->Read16(0x00000300), 0x1234u);
}

// ─── Cache Isolation ───────────────────────────────────────────────────

TEST_F(PS1MemoryTest, CacheIsolation_DefaultOff) {
  EXPECT_FALSE(memory->IsCacheIsolated());
}

TEST_F(PS1MemoryTest, CacheIsolation_Toggle) {
  memory->SetCacheIsolated(true);
  EXPECT_TRUE(memory->IsCacheIsolated());
  memory->SetCacheIsolated(false);
  EXPECT_FALSE(memory->IsCacheIsolated());
}

// ─── GetRAMPointer ─────────────────────────────────────────────────────

TEST_F(PS1MemoryTest, GetRAMPointer_NotNull) {
  EXPECT_NE(memory->GetRAMPointer(), nullptr);
}

TEST_F(PS1MemoryTest, GetRAMPointer_ReflectsWrites) {
  memory->WriteRAM32(0x0, 0xDEADC0DE);
  auto *ptr = reinterpret_cast<const uint32_t *>(memory->GetRAMPointer());
  EXPECT_EQ(ptr[0], 0xDEADC0DEu);
}

// ─── Reset ──────────────────────────────────────────────────────────────

TEST_F(PS1MemoryTest, Reset_ClearsRAM) {
  memory->WriteRAM32(0x100, 0xFFFFFFFF);
  memory->Reset();
  EXPECT_EQ(memory->ReadRAM32(0x100), 0u);
}

// ─── Debug ──────────────────────────────────────────────────────────────

TEST_F(PS1MemoryTest, GetDebugSummary_NotEmpty) {
  EXPECT_FALSE(memory->GetDebugSummary().empty());
}

TEST_F(PS1MemoryTest, DumpState_WritesOutput) {
  std::ostringstream os;
  memory->DumpState(os);
  EXPECT_FALSE(os.str().empty());
}
