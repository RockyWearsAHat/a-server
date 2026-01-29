# Plan: Fix OG-DK Complete Garbage Rendering

**Status:** NOT STARTED
**Goal:** Fix OG-DK to render correctly like mGBA

---

## Context

OG-DK shows complete visual garbage vs mGBA clean rendering.

### Root Cause Analysis

1. CPU Instruction Bug - ARM7TDMI has 36% test coverage
2. LZ77 Decompression Bug - SWI 0x11/0x12 needs testing
3. Thumb Format 8 Missing Tests - LDRH/STRH/LDRSB/LDRSH untested

---

## Steps

### Step 1: Add Thumb Format 8 tests to tests/CPUTests.cpp

**Operation:** INSERT_AFTER last TEST_F (around line 895)

```cpp
TEST_F(CPUTest, Thumb_Format8_STRH_RegisterOffset) {
  cpu.SetThumbMode(true);
  cpu.SetRegister(15, 0x08000000u);
  cpu.SetRegister(0, 0x1234u);
  cpu.SetRegister(2, 0x02000000u);
  cpu.SetRegister(3, 0x00000004u);
  // STRH R0, [R2, R3]: 0x52D0
  RunThumbInstr(0x52D0);
  EXPECT_EQ(memory.Read16(0x02000004u), 0x1234u);
}

TEST_F(CPUTest, Thumb_Format8_LDRH_RegisterOffset) {
  cpu.SetThumbMode(true);
  cpu.SetRegister(15, 0x08000000u);
  cpu.SetRegister(2, 0x02000000u);
  cpu.SetRegister(3, 0x00000006u);
  memory.Write16(0x02000006u, 0x5678u);
  // LDRH R0, [R2, R3]: 0x5AD0
  RunThumbInstr(0x5AD0);
  EXPECT_EQ(cpu.GetRegister(0), 0x5678u);
}

TEST_F(CPUTest, Thumb_Format8_LDRSB_RegisterOffset) {
  cpu.SetThumbMode(true);
  cpu.SetRegister(15, 0x08000000u);
  cpu.SetRegister(2, 0x02000000u);
  cpu.SetRegister(3, 0x00000007u);
  memory.Write8(0x02000007u, 0x80u);
  // LDRSB R0, [R2, R3]: 0x56D0
  RunThumbInstr(0x56D0);
  EXPECT_EQ(cpu.GetRegister(0), 0xFFFFFF80u);
}

TEST_F(CPUTest, Thumb_Format8_LDRSH_RegisterOffset) {
  cpu.SetThumbMode(true);
  cpu.SetRegister(15, 0x08000000u);
  cpu.SetRegister(2, 0x02000000u);
  cpu.SetRegister(3, 0x00000008u);
  memory.Write16(0x02000008u, 0x8000u);
  // LDRSH R0, [R2, R3]: 0x5ED0
  RunThumbInstr(0x5ED0);
  EXPECT_EQ(cpu.GetRegister(0), 0xFFFF8000u);
}
```

**Verify:** `make build && cd build/generated/cmake && ctest -R Thumb_Format8 --output-on-failure`

---

### Step 2: Add LZ77 decompression tests to tests/CPUTests.cpp

**Operation:** INSERT_AFTER the Format 8 tests

```cpp
TEST_F(CPUTest, SWI_LZ77UnCompWram_Literals) {
  const uint32_t src = 0x02000100u;
  const uint32_t dst = 0x02000200u;
  memory.Write32(src, 0x00000810u);
  memory.Write8(src + 4, 0x00u);
  memory.Write8(src + 5, 0x11u);
  memory.Write8(src + 6, 0x22u);
  memory.Write8(src + 7, 0x33u);
  memory.Write8(src + 8, 0x44u);
  memory.Write8(src + 9, 0x55u);
  memory.Write8(src + 10, 0x66u);
  memory.Write8(src + 11, 0x77u);
  memory.Write8(src + 12, 0x88u);
  cpu.SetRegister(0, src);
  cpu.SetRegister(1, dst);
  RunInstr(0xEF000011u);
  EXPECT_EQ(memory.Read8(dst + 0), 0x11u);
  EXPECT_EQ(memory.Read8(dst + 7), 0x88u);
}

TEST_F(CPUTest, SWI_LZ77UnCompVram_Literals) {
  const uint32_t src = 0x02000100u;
  const uint32_t dst = 0x06000000u;
  memory.Write32(src, 0x00000810u);
  memory.Write8(src + 4, 0x00u);
  memory.Write8(src + 5, 0xAAu);
  memory.Write8(src + 6, 0xBBu);
  memory.Write8(src + 7, 0xCCu);
  memory.Write8(src + 8, 0xDDu);
  memory.Write8(src + 9, 0xEEu);
  memory.Write8(src + 10, 0xFFu);
  memory.Write8(src + 11, 0x11u);
  memory.Write8(src + 12, 0x22u);
  cpu.SetRegister(0, src);
  cpu.SetRegister(1, dst);
  RunInstr(0xEF000012u);
  EXPECT_EQ(memory.Read16(dst + 0), 0xBBAAu);
  EXPECT_EQ(memory.Read16(dst + 6), 0x2211u);
}
```

**Verify:** `make build && cd build/generated/cmake && ctest -R SWI_LZ77 --output-on-failure`

---

### Step 3: Run all tests and verify no regressions

```bash
make build && cd build/generated/cmake && ctest --output-on-failure
```

---

## Test Strategy

1. `make build` - compiles without errors
2. `ctest -R Thumb_Format8` - Format 8 tests pass
3. `ctest -R SWI_LZ77` - LZ77 tests pass
4. `ctest --output-on-failure` - all tests pass

---

## Handoff

Run @Implement to execute all steps.
