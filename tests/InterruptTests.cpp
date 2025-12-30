#include <gtest/gtest.h>

#include "emulator/gba/GBAMemory.h"
#include "emulator/gba/IORegs.h"
#include "emulator/gba/PPU.h"

using namespace AIO::Emulator::GBA;

namespace {
constexpr uint16_t kKeyA = 1u << 0;
constexpr uint16_t kKeyB = 1u << 1;

uint16_t KeyInputValueForPressed(uint16_t pressedMask) {
  // KEYINPUT is active-low. Bits 0..9 are keys.
  pressedMask &= 0x03FFu;
  return (uint16_t)(0x03FFu & (uint16_t)~pressedMask);
}

uint16_t ReadIF(GBAMemory &mem) { return mem.Read16(IORegs::REG_IF); }
} // namespace

TEST(InterruptTest, IF_Write1ToClear_ClearsOnlyWrittenOnes) {
  GBAMemory mem;
  mem.Reset();

  PPU ppu(mem);

  // Enable VBlank IRQ in DISPSTAT (bit 3). (Bits 0..2 are read-only.)
  mem.Write16(IORegs::REG_DISPSTAT, 0x0008u);

  // Configure keypad IRQ: OR mode, enable, mask = A.
  const uint16_t keycnt = (uint16_t)(kKeyA | 0x4000u);
  mem.Write16(IORegs::REG_KEYCNT, keycnt);

  // Enter VBlank once so IF.VBLANK is set.
  ppu.Update(1232 * 160);
  EXPECT_NE((ReadIF(mem) & InterruptFlags::VBLANK), 0u);

  // Press A so IF.KEYPAD is set.
  mem.SetKeyInput(KeyInputValueForPressed(kKeyA));
  EXPECT_NE((ReadIF(mem) & InterruptFlags::KEYPAD), 0u);

  // Writing 0 should not clear anything.
  const uint16_t ifBefore = ReadIF(mem);
  mem.Write16(IORegs::REG_IF, 0x0000u);
  EXPECT_EQ(ReadIF(mem), ifBefore);

  // Clear only KEYPAD via halfword W1C.
  mem.Write16(IORegs::REG_IF, InterruptFlags::KEYPAD);
  EXPECT_EQ((ReadIF(mem) & InterruptFlags::KEYPAD), 0u);
  EXPECT_NE((ReadIF(mem) & InterruptFlags::VBLANK), 0u);

  // Clear only VBLANK via byte W1C (low byte bit0).
  mem.Write8(IORegs::REG_IF, 0x01u);
  EXPECT_EQ(ReadIF(mem), 0u);
}

TEST(InterruptTest, KeypadIRQ_ORMode_TriggersOnAnyMaskedKey) {
  GBAMemory mem;
  mem.Reset();

  // OR mode (bit15=0), IRQ enable (bit14=1), mask A|B.
  mem.Write16(IORegs::REG_KEYCNT, (uint16_t)(kKeyA | kKeyB | 0x4000u));

  // Press A only => triggers.
  mem.SetKeyInput(KeyInputValueForPressed(kKeyA));
  EXPECT_NE((ReadIF(mem) & InterruptFlags::KEYPAD), 0u);

  // Clear for next check.
  mem.Write16(IORegs::REG_IF, InterruptFlags::KEYPAD);
  EXPECT_EQ((ReadIF(mem) & InterruptFlags::KEYPAD), 0u);

  // Press none => no trigger.
  mem.SetKeyInput(KeyInputValueForPressed(0u));
  EXPECT_EQ((ReadIF(mem) & InterruptFlags::KEYPAD), 0u);
}

TEST(InterruptTest, KeypadIRQ_ANDMode_TriggersOnlyWhenAllMaskedKeysPressed) {
  GBAMemory mem;
  mem.Reset();

  // AND mode (bit15=1), IRQ enable (bit14=1), mask A|B.
  mem.Write16(IORegs::REG_KEYCNT, (uint16_t)(kKeyA | kKeyB | 0xC000u));

  // Press A only => no trigger.
  mem.SetKeyInput(KeyInputValueForPressed(kKeyA));
  EXPECT_EQ((ReadIF(mem) & InterruptFlags::KEYPAD), 0u);

  // Press A+B => triggers.
  mem.SetKeyInput(KeyInputValueForPressed((uint16_t)(kKeyA | kKeyB)));
  EXPECT_NE((ReadIF(mem) & InterruptFlags::KEYPAD), 0u);
}

TEST(InterruptTest, KeypadIRQ_MaskZeroDoesNotTrigger) {
  GBAMemory mem;
  mem.Reset();

  // IRQ enable set, but mask is zero => no IRQ.
  mem.Write16(IORegs::REG_KEYCNT, 0x4000u);

  mem.SetKeyInput(KeyInputValueForPressed(kKeyA));
  EXPECT_EQ((ReadIF(mem) & InterruptFlags::KEYPAD), 0u);
}
