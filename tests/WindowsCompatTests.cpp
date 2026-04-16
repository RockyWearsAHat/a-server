#include "emulator/windows/WinMemory.h"

#include <gtest/gtest.h>

using AIO::Emulator::Windows::WinMemory;

TEST(WindowsCompatTests, AllocateAndReadWriteRoundTrip) {
  WinMemory mem;
  const uint64_t base = mem.Allocate(0x50000000, 0x1000, WinMemory::PAGE_READWRITE);

  ASSERT_NE(base, 0u);
  mem.Write64(base + 0x20, 0x0123456789ABCDEFULL);
  EXPECT_EQ(mem.Read64(base + 0x20), 0x0123456789ABCDEFULL);
}

TEST(WindowsCompatTests, ProtectFailsForUnmappedAddress) {
  WinMemory mem;
  EXPECT_FALSE(mem.Protect(0x12345000, 0x1000, WinMemory::PAGE_EXECUTE_READ));
}

TEST(WindowsCompatTests, StringHelpersRoundTrip) {
  WinMemory mem;
  const uint64_t base = mem.Allocate(0x51000000, 0x1000, WinMemory::PAGE_READWRITE);

  ASSERT_NE(base, 0u);
  const uint64_t end = mem.WriteStringA(base, "kernel32.dll");
  EXPECT_GT(end, base);
  EXPECT_EQ(mem.ReadStringA(base), "kernel32.dll");
}

TEST(WindowsCompatTests, StubAllocatorIsMonotonic) {
  WinMemory mem;
  const uint64_t a = mem.AllocStub();
  const uint64_t b = mem.AllocStub();

  EXPECT_GE(a, WinMemory::kStubBase);
  EXPECT_EQ(b - a, 8u);
}