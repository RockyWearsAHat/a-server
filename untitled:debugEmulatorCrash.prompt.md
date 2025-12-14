---
name: debugEmulatorCrash
description: Debug emulator crashes with systematic instruction-level tracing and register analysis
argument-hint: ROM file name, crash symptoms, relevant log files
---

# Debug Emulator Crash - Systematic Analysis

You are debugging a CPU emulator crash. Follow this systematic approach:

## 1. Gather Initial Context

- Read the debug.log file for crash symptoms
- Identify the crash location (PC address where failure occurs)
- Note CPU state: mode (User/IRQ/etc), SP, LR, CPSR flags
- Check if crash is deterministic (same location every time)

## 2. Identify the True Root Cause

- **Symptom vs Cause**: If SP becomes 0, the real issue may be PC corruption
- Check if crash location contains valid code or data
- Use hexdump to read actual ROM bytes at crash address
- Decode instructions to verify they're valid (ARM vs Thumb mode)
- If executing data as code, trace backwards to find where PC got corrupted

## 3. Add Targeted Logging

- Log execution in critical paths (BIOS trampolines, IRQ handlers, mode switches)
- Disable verbose per-step logging to reduce noise
- Focus on: PC, SP, CPU mode, relevant registers (R3 for address loads, etc)
- Log state BEFORE and AFTER suspected problematic instructions

## 4. Trace Execution Flow

- Identify the last known good state
- Find the first instruction where state becomes invalid
- Check if execution path skips expected instructions
- Verify PC increments correctly (ARM +4, Thumb +2)
- Look for unexpected jumps or branches

## 5. Check for Common Emulator Bugs

- **Register banking**: IRQ/User mode SP/LR saved/restored correctly?
- **CPSR restoration**: SUBS PC, LR with S-bit restores CPSR from SPSR?
- **Interrupt re-entry**: Is CheckInterrupts() retriggering during handler?
- **IF register**: Does game clear interrupt flags, or are interrupts looping?
- **Mode confusion**: Is code executing in wrong mode (IRQ vs User)?
- **Thumb bit**: Is T-bit in CPSR matching actual instruction encoding?

## 6. Verify Against Hardware Behavior

- IRQ entry: Save PC+4 (ARM) or PC+2 (Thumb) to LR
- IRQ mode: Sets I-bit in CPSR, disables further IRQs
- BIOS trampoline: Pushes registers, calls user handler, restores context
- IRQ return: SUBS PC, LR, #4 restores CPSR from SPSR and returns
- User handler: Responsible for clearing IF bits

## 7. Key Debugging Commands

```bash
# Check crash location in debug.log
tail -100 debug.log

# Disassemble ROM instruction at crash PC
dd if=ROM.gba bs=1 skip=OFFSET count=16 2>/dev/null | hexdump -C

# Trace specific address ranges
grep "PC=0xCRASH_ADDR" debug.log -B 5 -A 5

# Check BIOS trampoline execution
grep "BIOS TRAMPOLINE" debug.log
```

## 8. Calculation Reference

- ROM address to file offset: `PC - 0x08000000`
- Decimal to hex: Use calculator or `printf "0x%x\n" DECIMAL`
- Check instruction encoding: Verify cond/opcode/registers match expected

## Current Issue Summary

**Problem**: Emulator crashes when loading SMA2.gba, with SP becoming 0 during execution.

**Symptoms**:

- Infinite loop at 0x809e36c-0x809e378 (User mode, SP=0x3007cbc)
- IRQ fires at PC=0x809e376, switches to IRQ mode (SP=0x3007fa0) ✓
- IRQ returns to 0x809e378 ✓
- **Execution jumps from BIOS 0x188 directly to game code 0x809e390**
- Game executes at 0x809e390-0x809e398 in **IRQ mode** (should be User mode)
- At 0x809e398, instruction zeros SP, causing crash

**Key Findings**:

1. BIOS trampoline execution sequence: 0x180 → 0x184 → 0x188 → **JUMPS to 0x809e390**
2. Missing execution: Should go 0x188 → 0x18c → ... → 0x1bc (call user handler) → 0x1c8 → 0x1cc (SUBS PC, LR return)
3. CheckInterrupts() shows: IE=0x20, IF=0x20 (VBlank), IRQDisabled=1 (correct for IRQ mode)
4. Game code at 0x809e378+ contains **Thumb instructions** but executing in **ARM mode**
5. R3 = 0x3007ff4 at PC=0x188 (correct - points to saved triggered bits)

**Root Cause Hypothesis**:
The LDRH instruction at 0x188 (`0xE1D310B0` = LDRH R1, [R3]) somehow causes PC to jump to game code instead of continuing to 0x18c. Possible causes:

- PC increment logic error after LDRH execution
- CheckInterrupts() incorrectly retriggering despite IRQ disabled
- User IRQ handler pointer at 0x03007FFC corrupted by game
- Memory read at 0x3007ff4 triggering side effect that changes PC

**Next Steps**:

1. Add logging after LDRH execution to see exact PC value
2. Verify PC increment logic in halfword load/store instruction handler
3. Check if CheckInterrupts() has bug allowing re-entry during IRQ mode
4. Verify BIOS trampoline instructions are correctly written to memory at 0x180-0x1d0
5. Check what value is stored at 0x03007FFC (user IRQ handler pointer)
