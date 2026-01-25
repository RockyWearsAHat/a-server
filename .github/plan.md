# Plan: Revert Broken IRQ Trampoline and Fix HLE IRQ Flow

**Status:** 🔴 NOT STARTED  
**Goal:** Fix SMA2/Metroid/OG-DK regressions by reverting to the working trampoline

---

## Context

### Root Cause Analysis

The commit 909dc34 rewrote the HLE BIOS IRQ trampoline with these changes:

1. Switch from IRQ mode to System mode before calling user handler
2. Acknowledge IF early and write to BIOS_IF (0x03007FF8)
3. Added System/User LR preservation

This **broke** SMA2, Metroid Zero Mission, **OG-DK (Classic NES Series: Donkey Kong)**, and other games that were previously working.

**The bug:** The new trampoline reads IE & IF at entry and writes to BIOS_IF. But the HLE CPU CheckInterrupts() already:

1. Calculates triggered = IE & IF
2. Writes triggered bits to BIOS_IF (0x03007FF8)
3. **Clears IF** to prevent re-entry

When the trampoline then reads IF, it gets **0** (already cleared), ANDs with IE to get **0**, and writes **0** to BIOS_IF destroying the value the HLE CPU wrote!

Games using IntrWait/VBlankIntrWait poll BIOS_IF waiting for their IRQ bit. With BIOS_IF = 0, they spin forever causing black screen.

**Evidence:**

- SMA2 stuck at PC=0x0800052E in a spin loop (LDRH r0,[r2]; AND r0,r1; CMP r0,#0; BEQ)
- This is an IntrWait-style loop waiting for (BIOS_IF & mask) != 0
- After SWI 0x01 (RegisterRamReset), the game never progresses
- **OG-DK** crashes with Invalid PC (0xD1012800 or 0x0404EE5C) - the new trampoline's System mode switch corrupts the game's IRQ handler expectations. Verified: OG-DK works in "lastgood" worktree (pre-909dc34).

### The Working Design

The **old trampoline** (before 909dc34) worked correctly because:

1. HLE CPU CheckInterrupts() writes to BIOS_IF and clears IF
2. HLE CPU writes triggered bits to 0x03007FF4 as backup
3. Trampoline does NOT touch BIOS_IF at entry
4. Trampoline calls user handler
5. After handler returns, trampoline reads from 0x03007FF4 and writes to IF (acknowledging)

This flow ensures BIOS_IF is set by the HLE CPU and never overwritten.

---

## Steps

### Step 1: Revert IRQ trampoline to working version

**File:** src/emulator/gba/GBAMemory.cpp
**Operation:** REPLACE
**Anchor:** The trampoline array in InitializeHLEBIOS() (lines 336-410)

Replace the entire trampoline[] array with the old version from commit 39404c6.

**Verify:** make build

---

### Step 2: Revert IRQ trampoline range check

**File:** src/emulator/gba/ARM7TDMI.cpp
**Operation:** REPLACE  
**Anchor:** The inIrqTrampoline check (around line 1157)

Change range from 0x3F64 back to 0x3F58.

**Verify:** make build

---

### Step 3: Revert BIOSTests to match old trampoline

**File:** tests/BIOSTests.cpp
**Operation:** REPLACE
**Anchor:** The IRQTrampolineInstructionsPresent test

See tests/BIOSTests.cpp in commit 39404c6 for the expected instruction values.

**Verify:** make build && ./build/bin/BIOSTests

---

### Step 4: Remove undefined exception handler at vector 0x04

**File:** src/emulator/gba/GBAMemory.cpp
**Operation:** DELETE
**Anchor:** After the trampoline installation loop (around line 414)

Delete the block that installs infinite loop at vector 0x04.

**Verify:** make build

---

## Test Strategy

1. make build - compiles without errors
2. ./build/bin/BIOSTests - all BIOS tests pass
3. Test SMA2: Non-black frame expected
4. Test Metroid Zero Mission: Non-black frame expected
5. Test OG-DK (Classic NES Series: Donkey Kong): No crash, game should render graphics

---

## Handoff

Run @Implement to execute all steps.
