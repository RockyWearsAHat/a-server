# Plan: Fix Infinite Recursion Crash in DISPSTAT/VCOUNT Flush

**Date:** 2026-01-23  
**Status:** ✅ COMPLETED  
**Goal:** Fix infinite recursion introduced by FlushPendingPeripheralCycles() in Read16()

---

## Root Cause Analysis

The recent changes to improve timing accuracy introduced a **critical infinite recursion bug**:

1. `GBAMemory::Read16()` was modified to call `FlushPendingPeripheralCycles()` when reading DISPSTAT/VCOUNT
2. `FlushPendingPeripheralCycles()` calls `memory->AdvanceCycles()`
3. `AdvanceCycles()` calls `PPU::Update()`
4. `PPU::Update()` calls `memory.Read16(0x04000004)` to read DISPSTAT
5. This triggers the flush again → **infinite recursion → stack overflow crash**

### Call Stack (from lldb):

```
PPU::Update() → memory.Read16(DISPSTAT) → FlushPendingPeripheralCycles() → AdvanceCycles() → PPU::Update() → ...
```

---

## Fix Strategy

**Solution:** Add a `ReadIORegister16Internal()` method that reads IO registers directly without triggering the flush. The PPU should use this internal method when reading its own registers.

---

## Step 1: Add ReadIORegister16Internal to GBAMemory

**File:** [include/emulator/gba/GBAMemory.h](include/emulator/gba/GBAMemory.h)

### 1.1 Add method declaration after WriteIORegisterInternal

```cpp
// After line 81 (WriteIORegisterInternal declaration), add:
  uint16_t ReadIORegister16Internal(uint32_t offset) const;
```

---

## Step 2: Implement ReadIORegister16Internal

**File:** [src/emulator/gba/GBAMemory.cpp](src/emulator/gba/GBAMemory.cpp)

### 2.1 Add implementation near WriteIORegisterInternal

```cpp
uint16_t GBAMemory::ReadIORegister16Internal(uint32_t offset) const {
  // Direct read from io_regs without triggering flush (for internal PPU use)
  if (offset + 1 < io_regs.size()) {
    return io_regs[offset] | (io_regs[offset + 1] << 8);
  }
  return 0;
}
```

---

## Step 3: Update PPU to use ReadIORegister16Internal

**File:** [src/emulator/gba/PPU.cpp](src/emulator/gba/PPU.cpp)

### 3.1 Replace memory.Read16(0x04000004) calls with ReadIORegister16Internal(0x04)

All reads of DISPSTAT (0x04000004), VCOUNT (0x04000006), and IF (0x04000202) inside PPU::Update() must use the internal method:

- `memory.Read16(0x04000004)` → `memory.ReadIORegister16Internal(0x04)`
- `memory.Read16(0x04000006)` → `memory.ReadIORegister16Internal(0x06)`
- `memory.Read16(0x04000202)` → `memory.ReadIORegister16Internal(0x202)`
- `memory.Read16(0x04000200)` → `memory.ReadIORegister16Internal(0x200)` (IE)
- `memory.Read16(0x04000208)` → `memory.ReadIORegister16Internal(0x208)` (IME)
- `memory.Read16(0x03007FF8)` → Keep as-is (IWRAM, not IO)

---

## Step 4: Verify Fix

### 4.1 Build

```bash
make build
```

### 4.2 Run tests

```bash
cd build && ctest --output-on-failure
```

### 4.3 Test SMA2

```bash
./build/bin/SMA2Harness
```

### 4.4 Test games in GUI

```bash
./build/bin/AIOServer SMA2.gba
./build/bin/AIOServer DKC.gba
./build/bin/AIOServer "Metroid - Zero Mission (USA).gba"
```

---

## Files Affected

| File                                                                 | Change                                                                                 |
| -------------------------------------------------------------------- | -------------------------------------------------------------------------------------- |
| [include/emulator/gba/GBAMemory.h](include/emulator/gba/GBAMemory.h) | Add `ReadIORegister16Internal()` declaration                                           |
| [src/emulator/gba/GBAMemory.cpp](src/emulator/gba/GBAMemory.cpp)     | Add `ReadIORegister16Internal()` implementation                                        |
| [src/emulator/gba/PPU.cpp](src/emulator/gba/PPU.cpp)                 | Replace `Read16()` with `ReadIORegister16Internal()` for IO register reads in Update() |

---

## Verification Criteria

- [ ] Build succeeds
- [ ] All tests pass (134+ tests)
- [ ] SMA2Harness runs without crashing
- [ ] SMA2, DKC, Metroid boot and run in GUI

---

## Previous Issues (Still To Verify After Fix)

| Issue                     | Game  | Symptom          | Status                         |
| ------------------------- | ----- | ---------------- | ------------------------------ |
| **Massive Lag**           | SMA2  | Game runs slow   | 🔍 Verify after recursion fix  |
| **Logo Fade Not Working** | DKC   | Logos don't fade | 🔍 Investigate after crash fix |
| **Corrupted Mess**        | OG-DK | Display broken   | 🔍 Investigate after crash fix |

---

**Ready for implementation!** Hand off to `@Implement` agent.
