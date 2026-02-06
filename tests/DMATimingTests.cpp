/**
 * DMA Timing Tests
 *
 * Tests for GBA DMA wait states and cycle timing based on GBATEK
 * specifications. Reference: https://problemkaputt.de/gbatek.htm#gbamemorymap
 *
 * Memory regions and their DMA characteristics:
 *   Region 0: BIOS       (0x00000000) - 0 wait states
 *   Region 2: EWRAM      (0x02000000) - 2N/2S (16-bit), 5N/5S (32-bit)
 *   Region 3: IWRAM      (0x03000000) - 0 wait states (fastest)
 *   Region 4: I/O        (0x04000000) - 0 wait states
 *   Region 5: Palette    (0x05000000) - 0N/0S (16-bit), 1N/1S (32-bit)
 *   Region 6: VRAM       (0x06000000) - 0N/0S (16-bit), 1N/1S (32-bit)
 *   Region 7: OAM        (0x07000000) - 0 wait states
 *   Region 8-D: ROM      (0x08000000+) - Configurable via WAITCNT
 */

#include <cstdint>
#include <gtest/gtest.h>

namespace {

// These must match the values in GBAMemory.cpp
constexpr int8_t kDmaWaitNonseq16[16] = {0, 0, 2, 0, 0, 0, 0, 0,
                                         4, 4, 4, 4, 4, 4, 4, 0};
constexpr int8_t kDmaWaitSeq16[16] = {0, 0, 2, 0, 0, 0, 0, 0,
                                      2, 2, 4, 4, 8, 8, 4, 0};
constexpr int8_t kDmaWaitNonseq32[16] = {0, 0, 5, 0, 0,  1,  1, 0,
                                         7, 7, 9, 9, 13, 13, 9, 0};
constexpr int8_t kDmaWaitSeq32[16] = {0, 0, 5, 0, 0,  1,  1, 0,
                                      5, 5, 9, 9, 17, 17, 9, 0};

inline int GetDmaCyclesPerWord(uint32_t srcRegion, uint32_t dstRegion,
                               bool is32Bit, bool isFirst) {
  srcRegion &= 0xF;
  dstRegion &= 0xF;
  if (is32Bit) {
    if (isFirst) {
      return kDmaWaitNonseq32[srcRegion] + kDmaWaitNonseq32[dstRegion];
    }
    return kDmaWaitSeq32[srcRegion] + kDmaWaitSeq32[dstRegion];
  } else {
    if (isFirst) {
      return kDmaWaitNonseq16[srcRegion] + kDmaWaitNonseq16[dstRegion];
    }
    return kDmaWaitSeq16[srcRegion] + kDmaWaitSeq16[dstRegion];
  }
}

// Calculate total DMA cycles for a transfer
inline int CalculateDmaTotalCycles(uint32_t srcAddr, uint32_t dstAddr,
                                   uint32_t count, bool is32Bit) {
  uint32_t srcRegion = (srcAddr >> 24) & 0xF;
  uint32_t dstRegion = (dstAddr >> 24) & 0xF;

  // Base overhead: 2 cycles for DMA setup
  int total = 2;

  // First word uses non-sequential timing
  if (count > 0) {
    total += GetDmaCyclesPerWord(srcRegion, dstRegion, is32Bit, true);
  }

  // Remaining words use sequential timing
  if (count > 1) {
    total +=
        (count - 1) * GetDmaCyclesPerWord(srcRegion, dstRegion, is32Bit, false);
  }

  return total;
}

} // namespace

class DMAWaitStateTest : public ::testing::Test {};

// =============================================================================
// Wait State Array Value Tests (GBATEK Reference)
// =============================================================================

TEST_F(DMAWaitStateTest, BiosRegion_HasZeroWaitStates) {
  // BIOS (region 0) should have 0 wait states
  EXPECT_EQ(kDmaWaitNonseq16[0], 0);
  EXPECT_EQ(kDmaWaitSeq16[0], 0);
  EXPECT_EQ(kDmaWaitNonseq32[0], 0);
  EXPECT_EQ(kDmaWaitSeq32[0], 0);
}

TEST_F(DMAWaitStateTest, EwramRegion_Has2Or5WaitStates) {
  // EWRAM (region 2): 2 wait states for 16-bit, 5 for 32-bit (2+2+1)
  EXPECT_EQ(kDmaWaitNonseq16[2], 2);
  EXPECT_EQ(kDmaWaitSeq16[2], 2);
  EXPECT_EQ(kDmaWaitNonseq32[2], 5);
  EXPECT_EQ(kDmaWaitSeq32[2], 5);
}

TEST_F(DMAWaitStateTest, IwramRegion_HasZeroWaitStates) {
  // IWRAM (region 3): 0 wait states - fastest memory
  EXPECT_EQ(kDmaWaitNonseq16[3], 0);
  EXPECT_EQ(kDmaWaitSeq16[3], 0);
  EXPECT_EQ(kDmaWaitNonseq32[3], 0);
  EXPECT_EQ(kDmaWaitSeq32[3], 0);
}

TEST_F(DMAWaitStateTest, IoRegion_HasZeroWaitStates) {
  // I/O (region 4): 0 wait states
  EXPECT_EQ(kDmaWaitNonseq16[4], 0);
  EXPECT_EQ(kDmaWaitSeq16[4], 0);
  EXPECT_EQ(kDmaWaitNonseq32[4], 0);
  EXPECT_EQ(kDmaWaitSeq32[4], 0);
}

TEST_F(DMAWaitStateTest, PaletteRegion_Has0Or1WaitStates) {
  // Palette (region 5): 0 for 16-bit, 1 for 32-bit (requires 2x16-bit access)
  EXPECT_EQ(kDmaWaitNonseq16[5], 0);
  EXPECT_EQ(kDmaWaitSeq16[5], 0);
  EXPECT_EQ(kDmaWaitNonseq32[5], 1);
  EXPECT_EQ(kDmaWaitSeq32[5], 1);
}

TEST_F(DMAWaitStateTest, VramRegion_Has0Or1WaitStates) {
  // VRAM (region 6): 0 for 16-bit, 1 for 32-bit (requires 2x16-bit access)
  EXPECT_EQ(kDmaWaitNonseq16[6], 0);
  EXPECT_EQ(kDmaWaitSeq16[6], 0);
  EXPECT_EQ(kDmaWaitNonseq32[6], 1);
  EXPECT_EQ(kDmaWaitSeq32[6], 1);
}

TEST_F(DMAWaitStateTest, OamRegion_HasZeroWaitStates) {
  // OAM (region 7): 0 wait states
  EXPECT_EQ(kDmaWaitNonseq16[7], 0);
  EXPECT_EQ(kDmaWaitSeq16[7], 0);
  EXPECT_EQ(kDmaWaitNonseq32[7], 0);
  EXPECT_EQ(kDmaWaitSeq32[7], 0);
}

TEST_F(DMAWaitStateTest, RomRegion8_HasDefaultWaitStates) {
  // ROM Wait State 0 (region 8-9): Default 4N/2S (16-bit), 7N/5S (32-bit)
  EXPECT_EQ(kDmaWaitNonseq16[8], 4);
  EXPECT_EQ(kDmaWaitSeq16[8], 2);
  EXPECT_EQ(kDmaWaitNonseq32[8], 7);
  EXPECT_EQ(kDmaWaitSeq32[8], 5);
}

TEST_F(DMAWaitStateTest, RomRegion10_HasDefaultWaitStates) {
  // ROM Wait State 1 (region 10-11): Default 4N/4S (16-bit), 9N/9S (32-bit)
  EXPECT_EQ(kDmaWaitNonseq16[10], 4);
  EXPECT_EQ(kDmaWaitSeq16[10], 4);
  EXPECT_EQ(kDmaWaitNonseq32[10], 9);
  EXPECT_EQ(kDmaWaitSeq32[10], 9);
}

TEST_F(DMAWaitStateTest, RomRegion12_HasDefaultWaitStates) {
  // ROM Wait State 2 (region 12-13): Default 4N/8S (16-bit), 13N/17S (32-bit)
  EXPECT_EQ(kDmaWaitNonseq16[12], 4);
  EXPECT_EQ(kDmaWaitSeq16[12], 8);
  EXPECT_EQ(kDmaWaitNonseq32[12], 13);
  EXPECT_EQ(kDmaWaitSeq32[12], 17);
}

// =============================================================================
// GetDmaCyclesPerWord Function Tests
// =============================================================================

TEST_F(DMAWaitStateTest, IwramToVram_32bit_FirstAccess) {
  // IWRAM (3) -> VRAM (6), 32-bit, first access
  // Expected: kDmaWaitNonseq32[3] + kDmaWaitNonseq32[6] = 0 + 1 = 1
  int cycles = GetDmaCyclesPerWord(3, 6, true, true);
  EXPECT_EQ(cycles, 1) << "IWRAM->VRAM 32-bit first access should be 1 cycle";
}

TEST_F(DMAWaitStateTest, IwramToVram_32bit_SequentialAccess) {
  // IWRAM (3) -> VRAM (6), 32-bit, sequential access
  // Expected: kDmaWaitSeq32[3] + kDmaWaitSeq32[6] = 0 + 1 = 1
  int cycles = GetDmaCyclesPerWord(3, 6, true, false);
  EXPECT_EQ(cycles, 1)
      << "IWRAM->VRAM 32-bit sequential access should be 1 cycle";
}

TEST_F(DMAWaitStateTest, IwramToVram_16bit_FirstAccess) {
  // IWRAM (3) -> VRAM (6), 16-bit, first access
  // Expected: kDmaWaitNonseq16[3] + kDmaWaitNonseq16[6] = 0 + 0 = 0
  int cycles = GetDmaCyclesPerWord(3, 6, false, true);
  EXPECT_EQ(cycles, 0) << "IWRAM->VRAM 16-bit first access should be 0 cycles";
}

TEST_F(DMAWaitStateTest, RomToIwram_32bit_FirstAccess) {
  // ROM (8) -> IWRAM (3), 32-bit, first access
  // Expected: kDmaWaitNonseq32[8] + kDmaWaitNonseq32[3] = 7 + 0 = 7
  int cycles = GetDmaCyclesPerWord(8, 3, true, true);
  EXPECT_EQ(cycles, 7) << "ROM->IWRAM 32-bit first access should be 7 cycles";
}

TEST_F(DMAWaitStateTest, RomToIwram_32bit_SequentialAccess) {
  // ROM (8) -> IWRAM (3), 32-bit, sequential access
  // Expected: kDmaWaitSeq32[8] + kDmaWaitSeq32[3] = 5 + 0 = 5
  int cycles = GetDmaCyclesPerWord(8, 3, true, false);
  EXPECT_EQ(cycles, 5)
      << "ROM->IWRAM 32-bit sequential access should be 5 cycles";
}

TEST_F(DMAWaitStateTest, EwramToVram_32bit_FirstAccess) {
  // EWRAM (2) -> VRAM (6), 32-bit, first access
  // Expected: kDmaWaitNonseq32[2] + kDmaWaitNonseq32[6] = 5 + 1 = 6
  int cycles = GetDmaCyclesPerWord(2, 6, true, true);
  EXPECT_EQ(cycles, 6) << "EWRAM->VRAM 32-bit first access should be 6 cycles";
}

TEST_F(DMAWaitStateTest, EwramToVram_32bit_SequentialAccess) {
  // EWRAM (2) -> VRAM (6), 32-bit, sequential access
  // Expected: kDmaWaitSeq32[2] + kDmaWaitSeq32[6] = 5 + 1 = 6
  int cycles = GetDmaCyclesPerWord(2, 6, true, false);
  EXPECT_EQ(cycles, 6)
      << "EWRAM->VRAM 32-bit sequential access should be 6 cycles";
}

TEST_F(DMAWaitStateTest, VramToVram_32bit) {
  // VRAM (6) -> VRAM (6), 32-bit
  // First: 1 + 1 = 2, Sequential: 1 + 1 = 2
  EXPECT_EQ(GetDmaCyclesPerWord(6, 6, true, true), 2);
  EXPECT_EQ(GetDmaCyclesPerWord(6, 6, true, false), 2);
}

TEST_F(DMAWaitStateTest, IwramToIwram_32bit) {
  // IWRAM (3) -> IWRAM (3), 32-bit - fastest possible DMA
  // First: 0 + 0 = 0, Sequential: 0 + 0 = 0
  EXPECT_EQ(GetDmaCyclesPerWord(3, 3, true, true), 0);
  EXPECT_EQ(GetDmaCyclesPerWord(3, 3, true, false), 0);
}

TEST_F(DMAWaitStateTest, RomToRom_32bit) {
  // ROM (8) -> ROM (8), 32-bit - slowest common DMA
  // First: 7 + 7 = 14, Sequential: 5 + 5 = 10
  EXPECT_EQ(GetDmaCyclesPerWord(8, 8, true, true), 14);
  EXPECT_EQ(GetDmaCyclesPerWord(8, 8, true, false), 10);
}

// =============================================================================
// Total DMA Cycle Calculation Tests
// =============================================================================

TEST_F(DMAWaitStateTest, TotalCycles_IwramToVram_1024Words_32bit) {
  // Classic NES series scenario: IWRAM 0x03000000 -> VRAM 0x06006000
  // 1024 words, 32-bit
  // Base: 2 cycles
  // First word: 0 + 1 = 1 cycle
  // Remaining 1023 words: 1023 * (0 + 1) = 1023 cycles
  // Total: 2 + 1 + 1023 = 1026 cycles
  int total = CalculateDmaTotalCycles(0x03000000, 0x06006000, 1024, true);
  EXPECT_EQ(total, 1026) << "IWRAM->VRAM 1024x32-bit should take 1026 cycles";
}

TEST_F(DMAWaitStateTest, TotalCycles_RomToIwram_1024Words_32bit) {
  // ROM 0x08000000 -> IWRAM 0x03000000
  // 1024 words, 32-bit
  // Base: 2 cycles
  // First word: 7 + 0 = 7 cycles
  // Remaining 1023 words: 1023 * (5 + 0) = 5115 cycles
  // Total: 2 + 7 + 5115 = 5124 cycles
  int total = CalculateDmaTotalCycles(0x08000000, 0x03000000, 1024, true);
  EXPECT_EQ(total, 5124) << "ROM->IWRAM 1024x32-bit should take 5124 cycles";
}

TEST_F(DMAWaitStateTest, TotalCycles_EwramToVram_256Words_32bit) {
  // EWRAM 0x02000000 -> VRAM 0x06000000
  // 256 words, 32-bit
  // Base: 2 cycles
  // First word: 5 + 1 = 6 cycles
  // Remaining 255 words: 255 * (5 + 1) = 1530 cycles
  // Total: 2 + 6 + 1530 = 1538 cycles
  int total = CalculateDmaTotalCycles(0x02000000, 0x06000000, 256, true);
  EXPECT_EQ(total, 1538) << "EWRAM->VRAM 256x32-bit should take 1538 cycles";
}

TEST_F(DMAWaitStateTest, TotalCycles_IwramToOam_128Words_32bit) {
  // IWRAM 0x03000000 -> OAM 0x07000000
  // 128 words, 32-bit (typical OAM DMA)
  // Base: 2 cycles
  // First word: 0 + 0 = 0 cycles
  // Remaining 127 words: 127 * (0 + 0) = 0 cycles
  // Total: 2 + 0 + 0 = 2 cycles
  int total = CalculateDmaTotalCycles(0x03000000, 0x07000000, 128, true);
  EXPECT_EQ(total, 2)
      << "IWRAM->OAM 128x32-bit should take 2 cycles (zero wait states)";
}

TEST_F(DMAWaitStateTest, TotalCycles_RomToVram_4096Words_16bit) {
  // ROM 0x08000000 -> VRAM 0x06000000
  // 4096 halfwords, 16-bit (typical tileset DMA)
  // Base: 2 cycles
  // First word: 4 + 0 = 4 cycles
  // Remaining 4095 words: 4095 * (2 + 0) = 8190 cycles
  // Total: 2 + 4 + 8190 = 8196 cycles
  int total = CalculateDmaTotalCycles(0x08000000, 0x06000000, 4096, false);
  EXPECT_EQ(total, 8196) << "ROM->VRAM 4096x16-bit should take 8196 cycles";
}

TEST_F(DMAWaitStateTest, TotalCycles_SingleWord) {
  // Single word transfer should only add first-word timing
  // IWRAM -> VRAM, 1 word, 32-bit
  // Base: 2 + first: 1 = 3 cycles
  int total = CalculateDmaTotalCycles(0x03000000, 0x06000000, 1, true);
  EXPECT_EQ(total, 3)
      << "Single word DMA should be 2 (base) + 1 (word) = 3 cycles";
}

TEST_F(DMAWaitStateTest, TotalCycles_ZeroWords) {
  // Zero word transfer - just base overhead
  int total = CalculateDmaTotalCycles(0x03000000, 0x06000000, 0, true);
  EXPECT_EQ(total, 2)
      << "Zero word DMA should be 2 cycles (base overhead only)";
}

// =============================================================================
// Region Extraction Tests
// =============================================================================

TEST_F(DMAWaitStateTest, RegionExtraction_CorrectlyMasked) {
  // Verify addresses map to correct regions
  EXPECT_EQ((0x00000000u >> 24) & 0xF, 0u);  // BIOS
  EXPECT_EQ((0x02000000u >> 24) & 0xF, 2u);  // EWRAM
  EXPECT_EQ((0x02FFFFFF >> 24) & 0xF, 2u);   // EWRAM (end)
  EXPECT_EQ((0x03000000u >> 24) & 0xF, 3u);  // IWRAM
  EXPECT_EQ((0x03007FFF >> 24) & 0xF, 3u);   // IWRAM (end)
  EXPECT_EQ((0x04000000u >> 24) & 0xF, 4u);  // I/O
  EXPECT_EQ((0x05000000u >> 24) & 0xF, 5u);  // Palette
  EXPECT_EQ((0x06000000u >> 24) & 0xF, 6u);  // VRAM
  EXPECT_EQ((0x06017FFF >> 24) & 0xF, 6u);   // VRAM (end)
  EXPECT_EQ((0x07000000u >> 24) & 0xF, 7u);  // OAM
  EXPECT_EQ((0x08000000u >> 24) & 0xF, 8u);  // ROM WS0
  EXPECT_EQ((0x0A000000u >> 24) & 0xF, 10u); // ROM WS1
  EXPECT_EQ((0x0C000000u >> 24) & 0xF, 12u); // ROM WS2
  EXPECT_EQ((0x0E000000u >> 24) & 0xF, 14u); // SRAM
}

// =============================================================================
// Comparison Tests: Old vs New Timing
// =============================================================================

TEST_F(DMAWaitStateTest, OldTimingComparison_IwramToVram) {
  // Old timing used flat 4 cycles per 32-bit word
  // New timing uses proper wait states
  constexpr int oldCyclesPerWord = 4;
  constexpr int newCyclesPerWord = 1; // IWRAM(0) + VRAM(1) = 1

  // For 1024 word transfer:
  // Old: 2 + 1024 * 4 = 4098 cycles
  // New: 2 + 1 + 1023 * 1 = 1026 cycles
  int oldTotal = 2 + 1024 * oldCyclesPerWord;
  int newTotal = CalculateDmaTotalCycles(0x03000000, 0x06000000, 1024, true);

  EXPECT_EQ(oldTotal, 4098);
  EXPECT_EQ(newTotal, 1026);
  EXPECT_LT(newTotal, oldTotal)
      << "New timing should be faster for IWRAM->VRAM";

  // New timing is ~4x faster for this case
  double speedup = static_cast<double>(oldTotal) / newTotal;
  EXPECT_NEAR(speedup, 4.0, 0.1)
      << "IWRAM->VRAM should be ~4x faster with proper wait states";
}

TEST_F(DMAWaitStateTest, OldTimingComparison_RomToVram) {
  // For ROM->VRAM, proper timing is actually slower than old flat timing
  constexpr int oldCyclesPerWord = 4;

  // ROM(7) + VRAM(1) = 8 first, ROM(5) + VRAM(1) = 6 sequential
  // For 1024 word transfer:
  // Old: 2 + 1024 * 4 = 4098 cycles
  // New: 2 + 8 + 1023 * 6 = 6148 cycles
  int oldTotal = 2 + 1024 * oldCyclesPerWord;
  int newTotal = CalculateDmaTotalCycles(0x08000000, 0x06000000, 1024, true);

  EXPECT_EQ(oldTotal, 4098);
  EXPECT_EQ(newTotal, 6148);
  EXPECT_GT(newTotal, oldTotal)
      << "ROM->VRAM should be slower with proper wait states";
}

// =============================================================================
// Classic NES Series Specific Tests (OG-DK Scenario)
// =============================================================================

TEST_F(DMAWaitStateTest, ClassicNES_TilemapDMA_Timing) {
  // The problematic DMA in OG-DK:
  // IWRAM 0x03000000 -> VRAM tilemap 0x06006000
  // 1024 words (0x1000 bytes), 32-bit

  int cycles = CalculateDmaTotalCycles(0x03000000, 0x06006000, 1024, true);

  // At 16.78MHz, cycles -> time
  // 1026 cycles at 16.78MHz = ~61 microseconds
  // Frame time = 16.74ms (280,896 cycles)
  // So this DMA takes ~0.37% of a frame

  EXPECT_EQ(cycles, 1026);

  // This is much faster than the old timing (4098 cycles = ~1.5% of frame)
  // But the issue is that this DMA happens BEFORE valid data is in IWRAM
}

TEST_F(DMAWaitStateTest, ClassicNES_Frame2_ScanlinePosition) {
  // With proper timing, the tilemap DMA happens at Frame 2 scanline ~18
  // Frame timing: 1232 cycles per scanline, 228 scanlines per frame

  constexpr int cyclesPerScanline = 1232;
  constexpr int scanlinesPerFrame = 228;
  constexpr int cyclesPerFrame =
      cyclesPerScanline * scanlinesPerFrame; // 280,896

  // If DMA finishes in 1026 cycles, it spans less than 1 scanline
  int scanlinesForDma = (1026 + cyclesPerScanline - 1) / cyclesPerScanline;
  EXPECT_LE(scanlinesForDma, 1)
      << "IWRAM->VRAM DMA should complete within 1 scanline";

  // The issue isn't the DMA duration, but WHEN it's triggered
  // Game triggers it before NES emulator has produced valid data
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
