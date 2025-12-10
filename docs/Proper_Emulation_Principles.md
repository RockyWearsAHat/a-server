# Proper Emulation Principles

## Core Philosophy

**Proper emulation means accurately replicating hardware behavior, NOT patching games to work.**

### What Proper Emulation IS:

- Implementing CPU instruction sets correctly
- Accurately timing hardware components (PPU, APU, DMA, Timers)
- Replicating memory maps and I/O register behavior
- Implementing BIOS functions that ALL games expect
- Following hardware specifications (GBATEK, official docs)

### What Proper Emulation IS NOT:

- Game-specific hacks or workarounds
- Protecting memory regions based on game ID
- Injecting values that games "expect" but BIOS doesn't provide
- Patching game code to skip initialization
- Making assumptions about what games "need"

## Case Study: DKC (Donkey Kong Country)

### The Problem

DKC uses a custom audio driver that expects certain memory initialization at `0x3001500`. Our HLE BIOS doesn't provide this initialization.

### The Wrong Approach (What We Were Doing)

```cpp
// WRONG: Game-specific hack
bool isDKC = (gameCode == "BDQE" || gameCode == "A5NE");
if (isDKC && currentDst == 0x3001500 && val == 0) {
    skipWrite = true; // Protect jump table from clearing
}

// WRONG: Inject game-specific value
wram_chip[0x1500] = 0x40; // Point to driver code
```

**Why This Is Wrong:**

1. **Not hardware behavior** - Real GBA doesn't check game codes
2. **Doesn't scale** - Every game would need its own hacks
3. **Maintainability nightmare** - Codebase becomes a patchwork of special cases
4. **Hides real issues** - Masks missing BIOS functionality
5. **Breaks other games** - Protection might interfere with normal behavior

### The Right Approach (What We Do Now)

```cpp
// RIGHT: Implement BIOS according to specification
void GBAMemory::InitializeHLEBIOS() {
    // Initialize ONLY what GBATEK/official docs say BIOS initializes:
    // - Interrupt vectors (0x3007FF8, 0x3007FFC)
    // - Stack pointers (CPU registers, not memory)
    // - SOUNDBIAS register
    // - Master sound enable

    // DO NOT initialize game-specific memory locations
}
```

**Why This Is Right:**

1. **Matches hardware** - Only initializes what real BIOS initializes
2. **Scales perfectly** - Works for all games that follow BIOS spec
3. **Maintainable** - Clear, documented, specification-based
4. **Exposes real issues** - Shows which games need LLE BIOS
5. **No side effects** - Can't break working games

### The Outcome

- **SMA2 and standard games:** ✅ Work perfectly (they follow BIOS spec)
- **DKC:** ❌ Doesn't work (needs LLE BIOS or reverse-engineering)
- **Code quality:** ✅ Clean, maintainable, specification-based

## When Game-Specific Code IS Acceptable

There are RARE cases where game-specific code is justified:

### 1. Known Hardware Bugs

```cpp
// ACCEPTABLE: Working around a documented hardware quirk
// that affects specific games due to timing
if (gameRequiresTimingWorkaround) {
    // Add documented delay to match hardware behavior
}
```

### 2. ROM Patching for Known Issues

```cpp
// ACCEPTABLE: Official patches for known game bugs
// (like day-one patches or bugfix hacks)
if (gameHasKnownBug && userEnabledPatches) {
    ApplyOfficialPatch();
}
```

### 3. Optional Enhancement Features

```cpp
// ACCEPTABLE: User-requested features that don't affect emulation accuracy
if (userEnabledSpeedHack) {
    SkipBIOSIntro();
}
```

**Key Difference:** These are **documented, optional, user-controlled** features, NOT core emulation behavior.

## How to Handle Incompatible Games

When a game doesn't work:

### Step 1: Research

- Check GBATEK documentation
- Compare with other emulators (mGBA, VBA-M, NO$GBA)
- Search for technical discussions (GBADev forums, emulation wikis)

### Step 2: Implement Missing Hardware/BIOS Features

```cpp
// If research shows BIOS should provide a function:
void GBAMemory::InitializeHLEBIOS() {
    // Add the missing BIOS initialization
    // Document the source (GBATEK section, hardware test, etc.)
}
```

### Step 3: Document Limitations

If the game needs features we can't/won't implement:

```markdown
# Compatibility.md

## Incompatible Games

- **DKC:** Requires LLE BIOS (custom audio driver initialization)
  - Workaround: User-provided BIOS dump
  - Future: Add LLE BIOS loading option
```

### Step 4: Never Resort to Game-Specific Hacks

If you find yourself writing `if (gameCode == "...")`, **STOP**. You're going down the wrong path.

## Benefits of Proper Emulation

### Short-term

- ✅ Clean, readable codebase
- ✅ Easy to review and understand
- ✅ Specification-based development
- ✅ No unexpected side effects

### Long-term

- ✅ High compatibility with standards-compliant games
- ✅ Easy to maintain and extend
- ✅ Clear separation of concerns
- ✅ Community trust (accuracy-focused emulation)
- ✅ Educational value (learn real hardware behavior)

### Community

- ✅ Other developers can understand the code
- ✅ Contributions follow same principles
- ✅ Emulator gains reputation for accuracy
- ✅ Users understand limitations clearly

## Current Status (December 2024)

### Working Perfectly (Proper Emulation)

- ✅ SMA2 (Super Mario Advance 2)
- ✅ All standard M4A/MP2K games (once tested)
- ✅ Games using EEPROM, Flash, SRAM saves
- ✅ Games using standard BIOS SWIs
- ✅ Direct Sound audio (FIFO A/B)

### Not Working (Requires LLE or Special Features)

- ❌ DKC (custom audio driver, needs LLE BIOS)
- ❌ Games requiring undocumented BIOS behavior
- ❌ Games with anti-emulation checks

### Next Steps

1. Test M4A engine with Pokemon Ruby/Sapphire
2. Implement LLE BIOS loading (user-provided)
3. Continue specification-based development
4. Document game compatibility accurately

## Conclusion

**Proper emulation is about accuracy, not compatibility at any cost.**

It's better to:

- Have 95% of games work perfectly
- Have 5% documented as needing LLE/special features
- Have clean, maintainable, specification-based code

Than to:

- Have 100% of games "work" with hacks
- Have unmaintainable spaghetti code
- Have unpredictable behavior and side effects
- Have no understanding of real hardware behavior

**When in doubt: Follow the specification, not the game.**
