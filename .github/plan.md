# Plan: Fix IRQ Re-entry Loop Caused by CPSR.I Clearing

**Status:** 🔴 NOT STARTED  
**Goal:** Prevent IRQ re-entry loop while still allowing nested interrupts in user handlers

---

## Context

### Background

- **Original issue**: OG-DK needed CPSR.I=0 in System mode for nested timer interrupts
- **Applied fix** (plan.md rev 1): Changed `BIC R3, R3, #0x1F` to `BIC R3, R3, #0x9F` to clear I flag
- **New issue**: Test `BIOSTest.IRQReturnRestoresThumbState` fails - CPU loops in trampoline

### Root Cause Analysis

The HLE BIOS IRQ trampoline flow:

1. **IRQ Entry** (ARM7TDMI.cpp): Save CPSR to `spsr_irq`, jump to trampoline
2. **0x3F00-0x3F08**: Push regs, prepare for mode switch
3. **0x3F0C-0x3F14**: `BIC R3, R3, #0x9F` + `ORR R3, R3, #0x1F` + `MSR CPSR_c, R3`
   - This clears CPSR.I and switches to System mode
   - **PROBLEM**: IF is still pending, so immediate IRQ re-entry before handler runs
4. Handler never runs, CPU loops forever

### Evidence from Test

```
Expected: PC=0x08000002, Thumb=true
Actual:   PC=16132 (0x3F04), Thumb=false
```

CPU is stuck looping in the trampoline due to immediate re-entry.

### Solution

**Clear IF bits at IRQ entry** (in ARM7TDMI::CheckInterrupts) before jumping to trampoline.

This matches real GBA BIOS behavior where triggered IRQs are atomically captured and cleared on entry, preventing same-source re-entry when CPSR.I is cleared.

The trampoline's existing IF clear at 0x3F48 (using 0x03007FF4 triggered mask) becomes a secondary acknowledgment for any IRQs that fired during the handler.

---

## Steps

### Step 1: Clear IF at IRQ entry - `src/emulator/gba/ARM7TDMI.cpp`

**Operation:** INSERT_AFTER  
**Anchor:** Line 500 (after `memory.Write16(0x03007FF8, ...)`)

Find this code block:

```cpp
    memory.Write16(0x03007FF8, (uint16_t)(biosIF | triggered));

    // std::cout << "[IRQ ENTRY] Triggered=0x" << std::hex << triggered << "
```

Insert BEFORE the commented-out line:

```cpp
    memory.Write16(0x03007FF8, (uint16_t)(biosIF | triggered));

    // Clear triggered IRQ bits from IF to prevent re-entry when CPSR.I clears.
    // Real BIOS atomically captures and clears IF at entry; HLE trampoline
    // enables IRQs before calling user handler, so we must clear IF here.
    memory.Write16(IORegs::REG_IF, triggered);

    // std::cout << "[IRQ ENTRY] Triggered=0x" << std::hex << triggered << "
```

**Verify:** `make build && ./build/bin/BIOSTests --gtest_filter='BIOSTest.IRQReturnRestoresThumbState'`

---

### Step 2: Update test expectation comment - `tests/BIOSTests.cpp`

**Operation:** REPLACE  
**Anchor:** Lines 247-250

Find:

```cpp
  // Install a minimal IRQ handler in IWRAM that acknowledges IF and returns.
  // This matches how real games/libc handlers behave; with System mode IRQs
  // enabled, the pending IF bit must be cleared to avoid immediate re-entry.
  constexpr uint32_t kHandlerAddr = 0x03001000u;
```

Replace with:

```cpp
  // Install a minimal IRQ handler in IWRAM that returns immediately.
  // Note: IF is cleared at IRQ entry by the emulator (matching real BIOS),
  // so the handler doesn't need to acknowledge IF to prevent re-entry.
  constexpr uint32_t kHandlerAddr = 0x03001000u;
```

**Verify:** Comment is accurate and doesn't affect test behavior.

---

## Test Strategy

1. `make build` - compiles without errors
2. `cd build/generated/cmake && ctest --output-on-failure` - all 135 tests pass
3. OG-DK manual test (if available) - verify nested timer IRQs still work

---

## Documentation Updates

### Append to `.github/instructions/memory.md` (after Timing and Performance section):

```markdown
### IRQ Entry Semantics

The emulator clears triggered IF bits at IRQ entry (in `ARM7TDMI::CheckInterrupts`) to prevent immediate re-entry when the HLE BIOS trampoline enables CPSR.I=0 for nested interrupts. This matches real GBA BIOS atomicity semantics:

- Triggered bits (IE & IF) are captured
- IF is cleared for those bits
- Trampoline runs with nested IRQs enabled but same-source blocked
- User handler can re-enable specific IRQs by clearing IF for them
```

---

## Handoff

Run `@Implement` to execute all steps.
