---
description: "Translate gamer bug reports into technical emulation specs for immediate TDD action."
---

# Emulator Bug Intake

Translate casual bug descriptions into actionable technical specifications.

## Usage

```
/emu-bug [description in any language - gamer, casual, technical]
```

## Examples

**Input:** "colors are all wrong in DKC"
**Output:**

```
SUBSYSTEM: PPU (Color Effects)
SYMPTOM: Incorrect color output
GBATEK CHECK: LCD I/O - Color Special Effects, BLDCNT/BLDALPHA/BLDY
LIKELY CAUSE: Alpha blending or brightness fade miscalculation
TEST FILE: tests/PPUTests.cpp
NEXT: @tdd "PPU color blending produces incorrect RGB values"
```

**Input:** "game is super laggy"
**Output:**

```
SUBSYSTEM: Timing (CPU/PPU sync)
SYMPTOM: Frame rate drops or stuttering
GBATEK CHECK: LCD I/O - VCOUNT/DISPSTAT timing, DMA timing
LIKELY CAUSE: Stale register reads, incorrect cycle counting, or batch size
TEST FILE: tests/DMATests.cpp or tests/PPUTests.cpp
NEXT: @tdd "VCOUNT/DISPSTAT reads return stale values"
```

**Input:** "screen is glitchy/garbage"  
**Output:**

```
SUBSYSTEM: PPU (VRAM/Tilemap)
SYMPTOM: Visual corruption or garbage tiles
GBATEK CHECK: LCD VRAM - BG Map/Tile Data, OBJ Tile Mapping
LIKELY CAUSE: Wrong VRAM base address, incorrect tile indexing, or DMA destination
TEST FILE: tests/PPUTests.cpp or tests/DMATests.cpp
NEXT: @tdd "BG tilemap reads from incorrect VRAM offset"
```

---

## Translation Table

| Gamer Says                         | Technical Meaning              | GBATEK Section        |
| ---------------------------------- | ------------------------------ | --------------------- |
| "laggy" / "slow"                   | Timing desync, stale reads     | LCD I/O, Timers       |
| "colors wrong" / "too bright/dark" | Color effects (blend/fade)     | Color Special Effects |
| "screen garbage" / "glitchy"       | VRAM/tilemap corruption        | LCD VRAM, BG Map Data |
| "sprites missing" / "flickering"   | OBJ rendering issues           | LCD OBJ, OAM          |
| "no sound" / "sound glitchy"       | APU/DMA audio issues           | Sound Controller      |
| "game crashes" / "freezes"         | CPU exception or infinite loop | ARM CPU, Interrupts   |
| "saves don't work"                 | EEPROM/Flash/SRAM protocol     | Cartridge Backup      |
| "controls don't respond"           | Keypad input issues            | Keypad Input          |
| "wrong speed" / "too fast"         | Cycle timing incorrect         | System Clock, Timers  |
| "black screen"                     | Boot failure or PPU disabled   | DISPCNT, BIOS         |
| "intro skips" / "cutscene broken"  | Timer/IRQ timing issues        | Timers, Interrupts    |

---

## Output Format

When you describe ANY bug, I will respond with:

```
┌─────────────────────────────────────────────────────┐
│ SUBSYSTEM:    [PPU|CPU|DMA|APU|Memory|Timer|Input] │
│ SYMPTOM:      [One-line description]               │
│ GBATEK CHECK: [Specific section to research]       │
│ LIKELY CAUSE: [Technical hypothesis]               │
│ TEST FILE:    [Which test file to check/add to]    │
│ NEXT:         @tdd "[Ready-to-paste command]"      │
└─────────────────────────────────────────────────────┘
```

Then you can copy the `@tdd` command directly and the TDD agent takes over.
