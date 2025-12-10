# DKC Audio Driver Investigation - KEY DISCOVERY

## The 0x3001500 Mistake

**CRITICAL REALIZATION**: The region at `0x3001500` is **NOT** part of the standard GBA BIOS memory layout!

### Evidence from GBATEK:

```
Default WRAM Usage:
  3007FFC  Pointer to user IRQ handler (32bit ARM code)
  3007FF8  Interrupt check flag (for IntrWait/VBlankIntrWait functions)
  3007FF4  Allocated Area
  3007FF0  Pointer to Sound Buffer
  3007FE0  Default area for SP_svc Supervisor Stack (16 bytes)
  3007FA0  Default area for SP_irq Interrupt Stack (160 bytes)
  3007F00  SP_usr User Stack starts here
```

**NO mention of 0x3001500-0x3001540 region!**

### What Actually Happens:

1. **Our HLE BIOS** (GBAMemory::Reset()):

   - Initializes `0x3007FFC` = IRQ handler pointer
   - Initializes `0x3007FF8` = Interrupt check flag
   - **INCORRECTLY** initialized `0x3001500` with jump table stubs

2. **DKC Boot Sequence**:

   - PC=`0x8000996`: Game performs **normal IWRAM clear** (including `0x3001500`)
   - This is **CORRECT BEHAVIOR** - games clear uninitialized IWRAM before using it!

3. **The Problem**:
   - We were protecting `0x3001500` thinking it was BIOS-initialized
   - But it's **just normal IWRAM** that games clear themselves
   - DKC uploads audio driver to `0x3002b40+`
   - Driver expects **something specific** at runtime (not at boot!)

## What DKC Actually Needs:

### Current Findings:

From HeadlessTest logs (with protection removed):

```
[WRITE8] addr=0x3001500 val=0x0 PC=0x8000996  <- Game clears IWRAM (NORMAL!)
...
Frame 120:
  [DEBUG] Audio driver @ 0x3002b40 = 0x4000
  [DEBUG] Jump table @ 0x3001500 = 0x0          <- CLEARED by game!
  [DEBUG] Driver patch MISSING! Expected 0x4770, got 0x4000
  [DEBUG] Jump table NOT PATCHED! Expected 0xE12FFF1E, got 0x0
```

### The Real Issue:

The driver at `0x3002b40` contains value `0x4000` (not `0x4770` = "BX LR" in Thumb).

The driver is **waiting for initialization** that never comes because:

- Our HLE BIOS doesn't match real BIOS boot behavior
- Real BIOS does something during boot that we're missing
- This isn't about `0x3001500` at all!

## Next Steps:

1. ✅ Remove ALL protection hacks (DONE)
2. ✅ Remove jump table initialization from Reset() (need to do)
3. ❓ Study real GBA BIOS boot sequence:
   - What registers are initialized?
   - What memory regions are set up?
   - What hardware state is configured?
4. ❓ Understand DKC audio driver initialization:
   - What does it poll/wait for?
   - What triggers initialization?
   - What memory/registers does it expect?

## Conclusion:

**Our approach was fundamentally wrong.** We were:

- Initializing non-existent "BIOS regions"
- Blocking game's normal IWRAM clearing
- Papering over the real problem

**The real solution**: Match real BIOS boot behavior exactly, then let games run naturally.
