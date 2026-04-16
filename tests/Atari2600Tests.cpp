#include "emulator/atari2600/Atari2600Memory.h"
#include "emulator/atari2600/MOS6507.h"
#include "emulator/atari2600/PIA6532.h"
#include "emulator/atari2600/TIA.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

using Atari2600::Atari2600Memory;
using Atari2600::MOS6507;
using Atari2600::PIA6532;
using Atari2600::TIA;

TEST(Atari2600PIATests, RamRoundTrip) {
  PIA6532 pia;
  pia.Write(0x0080, 0x5A);
  EXPECT_EQ(pia.Read(0x0080), 0x5A);
}

TEST(Atari2600PIATests, PortDirectionMixesInputAndOutput) {
  PIA6532 pia;

  pia.SetPortA(0xF0);
  pia.Write(0x0281, 0x0F); // SWACNT: lower nibble output, upper nibble input
  pia.Write(0x0280, 0x05); // SWCHA output value

  const uint8_t swcha = pia.Read(0x0280);
  EXPECT_EQ(swcha, 0xF5);
}

TEST(Atari2600TIATests, FrameReadyAfterFullFrameTicks) {
  TIA tia;
  tia.ClearFrameReady();

  for (int i = 0; i < 228 * 262; ++i) {
    tia.Tick();
  }

  EXPECT_TRUE(tia.IsFrameReady());
  tia.ClearFrameReady();
  EXPECT_FALSE(tia.IsFrameReady());
}

TEST(Atari2600MemoryTests, F8BankSwitchingSelectsExpectedBank) {
  TIA tia;
  PIA6532 pia;
  Atari2600Memory mem(tia, pia);

  std::vector<uint8_t> rom(0x2000, 0x00);
  rom[0x100] = 0x11;          // Bank 0 at offset 0x100
  rom[0x1000 + 0x100] = 0x22; // Bank 1 at offset 0x100
  mem.LoadROM(rom);

  EXPECT_EQ(mem.Read8(0x1100), 0x11);
  static_cast<void>(mem.Read8(0x1FF9));
  EXPECT_EQ(mem.Read8(0x1100), 0x22);
  static_cast<void>(mem.Read8(0x1FF8));
  EXPECT_EQ(mem.Read8(0x1100), 0x11);
}

TEST(Atari2600CPUTests, ResetVectorAndLdaImmediateExecution) {
  TIA tia;
  PIA6532 pia;
  Atari2600Memory mem(tia, pia);

  std::vector<uint8_t> rom(0x1000, 0xEA); // NOP fill
  rom[0x000] = 0xA9;                      // LDA #$42
  rom[0x001] = 0x42;
  rom[0xFFC] = 0x00; // Reset vector low  -> 0x1000
  rom[0xFFD] = 0x10; // Reset vector high
  mem.LoadROM(rom);

  MOS6507 cpu(mem);
  cpu.Reset();

  EXPECT_EQ(cpu.GetPC(), 0x1000);
  const int cycles = cpu.Step();
  EXPECT_EQ(cycles, 2);
  EXPECT_EQ(cpu.GetA(), 0x42);
  EXPECT_EQ(cpu.GetPC(), 0x1002);
}

} // namespace