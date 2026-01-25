# Plan: Fix HLE BIOS IRQ Trampoline Re-entry Bug

**Status:** 🔴 NOT STARTED
**Goal:** Fix OG-DK crash caused by unbounded IRQ re-entry stack overflow

---

## Context

### Root Cause Analysis

OG-DK (Classic NES Series: Donkey Kong) crashes ~1.6 seconds after boot with:

- Undefined instruction exceptions at IWRAM (0x03008xxx)
- Stack overflow: SP decreases from 0x03007200 to 0x03005718 without recovering
- Final crash: corrupt PC (0xd1012800)

**Root cause:** The HLE BIOS IRQ trampoline enables nested IRQs (clears CPSR.I) BEFORE acknowledging IF. This causes immediate re-entry if IF is still pending, creating an infinite IRQ loop that overflows the stack.

### Current trampoline flow (broken):

```
0x3F00: Save regs to SP_irq
0x3F0C: BIC R3, R3, #0x9F  ← Clears mode bits AND I flag
0x3F14: MSR CPSR_c, R3     ← IRQs now ENABLED, IF still pending → immediate re-entry!
...
0x3F40: LDRH R1, [0x03007FF4]  ← Read BIOS_IF (but too late)
0x3F48: STRH R1, [0x04000202]  ← Acknowledge IF (but too late)
```

### Fix: Acknowledge IF BEFORE enabling nested IRQs

Per GBATEK, the real BIOS:

1. Captures triggered IRQs (IE & IF)
2. Acknowledges those bits in IF (writes them back)
3. THEN switches to System mode with IRQs enabled

---

## Steps

### Step 1: Rewrite HLE BIOS IRQ trampoline — `src/emulator/gba/GBAMemory.cpp`

**Operation:** `REPLACE`
**Anchor:** The `const uint32_t trampoline[]` array definition

Replace the trampoline with a corrected version that acknowledges IF BEFORE switching to System mode:

```cpp
  uint32_t base = kIrqTrampolineBase;
  const uint32_t trampoline[] = {
      // === PHASE 1: IRQ entry (IRQ mode, I=1) ===
      // 0x3F00: STMDB SP!, {R0-R3, R12, LR}  ; save volatile regs
      0xE92D500F,

      // === PHASE 2: Read and acknowledge IF BEFORE enabling nested IRQs ===
      // 0x3F04: MOV   R2, #0x04000000       ; IO base
      0xE3A02404,
      // 0x3F08: ADD   R0, R2, #0x200        ; R0 = 0x04000200 (REG_IE/IF base)
      0xE2820F80,
      // 0x3F0C: LDRH  R1, [R0, #0]          ; R1 = IE
      0xE1D010B0,
      // 0x3F10: LDRH  R3, [R0, #2]          ; R3 = IF
      0xE1D030B2,
      // 0x3F14: AND   R1, R1, R3            ; R1 = triggered = IE & IF
      0xE0011003,
      // 0x3F18: STRH  R1, [R0, #2]          ; ACK IF (write triggered bits back)
      0xE1C010B2,
      // 0x3F1C: LDR   R0, [PC, #64]         ; R0 = literal 0x03007FF8 (BIOS_IF addr)
      0xE59F0040,
      // 0x3F20: STRH  R1, [R0]              ; Store to BIOS_IF for user handler
      0xE1C010B0,

      // === PHASE 3: Switch to System mode with IRQs enabled ===
      // 0x3F24: MRS   R3, CPSR
      0xE10F3000,
      // 0x3F28: BIC   R3, R3, #0x9F         ; clear mode bits AND I flag
      0xE3C3309F,
      // 0x3F2C: ORR   R3, R3, #0x1F         ; SYS mode
      0xE383301F,
      // 0x3F30: MSR   CPSR_c, R3            ; Now in SYS mode, IRQs enabled
      0xE129F003,

      // === PHASE 4: Call user handler ===
      // 0x3F34: STMDB SP!, {LR}             ; preserve interrupted SYS/USR LR
      0xE92D4000,
      // 0x3F38: LDR   R0, [PC, #36]         ; R0 = literal 0x03FFFFFC (handler ptr mirror)
      0xE59F0024,
      // 0x3F3C: LDR   R0, [R0]              ; R0 = user handler address
      0xE5900000,
      // 0x3F40: ADD   LR, PC, #0            ; LR = return address (0x3F48)
      0xE28FE000,
      // 0x3F44: BX    R0                    ; call user handler (ARM or Thumb)
      0xE12FFF10,
      // 0x3F48: LDMIA SP!, {LR}             ; restore interrupted SYS/USR LR
      0xE8BD4000,

      // === PHASE 5: Return to IRQ mode ===
      // 0x3F4C: MRS   R3, CPSR
      0xE10F3000,
      // 0x3F50: BIC   R3, R3, #0x9F         ; clear mode and I flag
      0xE3C3309F,
      // 0x3F54: ORR   R3, R3, #0x92         ; IRQ mode + I=1
      0xE3833092,
      // 0x3F58: MSR   CPSR_c, R3            ; Back in IRQ mode, IRQs masked
      0xE129F003,

      // === PHASE 6: Return from exception ===
      // 0x3F5C: LDMIA SP!, {R0-R3, R12, LR} ; restore regs
      0xE8BD500F,
      // 0x3F60: SUBS  PC, LR, #4            ; return (restores CPSR from SPSR)
      0xE25EF004,

      // === Literals ===
      // 0x3F64: 0x03007FF8 (BIOS_IF)
      0x03007FF8u,
      // 0x3F68: 0x03FFFFFC (handler pointer mirror)
      0x03FFFFFCu,
  };
```

**Verify:** `make build && ./build/bin/BIOSTests --gtest_filter='BIOSTest.*'`

---

### Step 2: Add HLE BIOS undefined exception handler — `src/emulator/gba/GBAMemory.cpp`

**Operation:** `INSERT_AFTER`
**Anchor:** After the trampoline installation loop (after `for (size_t i = 0; ...`)

Add an infinite loop at vector 0x04 for undefined instruction exceptions:

```cpp
  // Undefined Instruction Exception handler at vector 0x04
  // Install an infinite loop so the emulator halts cleanly on undefined
  // instructions rather than sliding through NOPs and corrupting state.
  // Real BIOS has: B 0x04 (infinite loop)
  // Opcode: 0xEAFFFFFE = B #-8 (branches to itself)
  bios[0x04] = 0xFE;
  bios[0x05] = 0xFF;
  bios[0x06] = 0xFF;
  bios[0x07] = 0xEA;
```

**Verify:** Undefined instruction at IWRAM should halt CPU at 0x04 instead of crashing

---

## Test Strategy

1. `make build` — compiles without errors
2. `./build/bin/BIOSTests` — BIOS tests pass (esp. IRQReturnRestoresThumbState)
3. Run OG-DK headless for 5 seconds:
   ```bash
   timeout 10 ./build/bin/AIOServer --headless --rom "OG-DK.gba" --headless-max-ms 5000 --headless-dump-ppm /tmp/ogdk.ppm --headless-dump-ms 4500
   ```

   - Should NOT crash with "Invalid PC"
   - Should NOT show stack overflow (SP should stay near 0x03007xxx)
   - PPM should show game graphics (not 98% black)

---

## Documentation Updates

### Append to `.github/instructions/memory.md` under "### IRQ Entry Semantics":

```markdown
### HLE BIOS IRQ Trampoline

The HLE BIOS IRQ trampoline at 0x3F00 follows real BIOS semantics:

1. **Acknowledge IF early**: Read IE & IF, write triggered bits back to IF, store to BIOS_IF (0x03007FF8)
2. **Then enable nested IRQs**: Switch to System mode with CPSR.I=0
3. **Call user handler**: Via pointer at 0x03FFFFFC (mirror of 0x03007FFC)
4. **Return to IRQ mode**: Re-mask IRQs (CPSR.I=1)
5. **Exception return**: SUBS PC, LR, #4 restores CPSR from SPSR

This ordering prevents IRQ re-entry storms when IF has pending bits.
```

---

## Handoff

Run `@Implement` to execute all steps.
