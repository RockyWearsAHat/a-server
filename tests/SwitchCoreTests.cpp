#include "emulator/switch/CpuCore.h"
#include "emulator/switch/MemoryManager.h"

#include <gtest/gtest.h>

using AIO::Emulator::Switch::CpuCore;
using AIO::Emulator::Switch::MemoryManager;

TEST(SwitchMemoryManagerTests, MapReadWriteAndOverlapProtection) {
  MemoryManager mem;

  ASSERT_TRUE(mem.MapMemory(0x1000, 0x100, 0x3));
  mem.Write32(0x1010, 0xDEADBEEF);
  EXPECT_EQ(mem.Read32(0x1010), 0xDEADBEEF);

  EXPECT_FALSE(mem.MapMemory(0x1080, 0x80, 0x3));
}

TEST(SwitchMemoryManagerTests, MapRejectsContainingOverlapAndAllowsAdjacentRegion) {
  MemoryManager mem;

  ASSERT_TRUE(mem.MapMemory(0x2000, 0x100, 0x3));
  EXPECT_FALSE(mem.MapMemory(0x1F00, 0x400, 0x3));
  EXPECT_TRUE(mem.MapMemory(0x2100, 0x80, 0x3));
}

TEST(SwitchMemoryManagerTests, PermissionsAreEnforcedForWrites) {
  MemoryManager mem;

  ASSERT_TRUE(mem.MapMemory(0x3000, 0x100, 0x1));
  mem.Write32(0x3000, 0xAABBCCDD);

  // Read-only region should ignore writes.
  EXPECT_EQ(mem.Read32(0x3000), 0u);
}

TEST(SwitchCpuCoreTests, RunAdvancesPcByInstructionWidth) {
  MemoryManager mem;
  ASSERT_TRUE(mem.MapMemory(0x0, 0x1000, 0x7));

  // ARM64 NOP (D503201F).
  mem.Write32(0x0, 0xD503201F);
  mem.Write32(0x4, 0xD503201F);

  CpuCore cpu(mem);
  cpu.Reset();
  cpu.SetPC(0x0);

  cpu.Run(2);
  EXPECT_EQ(cpu.GetPC(), 0x8);
}