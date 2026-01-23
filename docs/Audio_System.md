# GBA Audio System Documentation

## Overview

The GBA has two audio subsystems:

1. **Direct Sound (DMA Audio)** - 8-bit PCM samples via FIFO buffers
2. **PSG (Programmable Sound Generator)** - 4 legacy Game Boy sound channels

## Current Implementation Status

### ✅ Implemented (Working)

- Direct Sound FIFO A/B buffers
- Timer-based sample rate control
- DMA sound triggering on timer overflow
- Sample mixing and output to SDL2
- Volume control (50%/100%)
- Stereo panning (L/R enable)
- Master sound enable (SOUNDCNT_X)

### ❌ Not Implemented (Missing)

- **M4A/MP2K Sound Engine** - Nintendo's music/sound effect driver used by many games
- **PSG Channels** - Square wave, wave RAM, noise generators
- **Sound BIOS Functions** - Currently HLE stubs, don't provide full M4A support
- **Envelope/Sweep** - PSG hardware features
- **Sound DMA optimizations** - FIFO refill timing

## Game Compatibility

### Games Using Direct Sound (✅ Should Work)

- Most homebrew
- Games with custom audio drivers
- Simple sound effect games

### Games Using M4A/MP2K (❌ Limited/No Audio)

- **Donkey Kong Country** (DKC) - Uses M4A extensively
- Pokemon games - M4A for music
- Many Nintendo first-party titles
- Metroid Fusion/Zero Mission

## M4A/MP2K Sound Engine

### What It Is

M4A (MusicPlayer2000/MP2K) is Nintendo's official sound driver library. Games link it from ROM and expect the BIOS to initialize it via SWI 0x1A-0x1E.

### Why It's Complex

- **~2000 lines of ARM assembly** in the driver alone
- Handles music sequencing, sample mixing, envelopes, pitch bends
- Uses custom data formats (sound bank, sequence data)
- Requires precise timing with VBlank/Timer interrupts
- BIOS provides helper functions the driver calls

### Implementation Options

#### Option 1: HLE M4A (Partial)

- Detect M4A driver in ROM
- Implement basic sequencer
- Mix samples ourselves
- **Pro:** No external files needed
- **Con:** Massive development effort (weeks/months)
- **Con:** Will never be 100% accurate

#### Option 2: LLE BIOS (Complete)

- Load real GBA BIOS file
- Run actual BIOS code in emulator
- M4A works perfectly
- **Pro:** Perfect compatibility
- **Con:** Requires BIOS file (legal issues)
- **Con:** Slower than HLE

#### Option 3: Hybrid (Recommended)

- Keep HLE for most functions
- Implement LLE for SWI 0x1A-0x1E specifically
- Extract M4A driver from ROM, run it in IWRAM
- **Pro:** Good compatibility
- **Pro:** Reasonably fast
- **Con:** Still complex

## Current BIOS Sound Function Behavior

### SWI 0x19: SoundBias

**Status:** ✅ Working  
**Purpose:** Set SOUNDBIAS register (anti-pop on init)  
**Implementation:** Writes to 0x04000088

### SWI 0x1A: SoundDriverInit

**Status:** ⚠️ Partial (hardware only)  
**Purpose:** Initialize M4A sound driver  
**What We Do:**

- Initialize SOUNDCNT registers
- Enable master sound
- Clear FIFOs
- Setup work area structure

**What We DON'T Do:**

- Copy M4A driver code to IWRAM (it's already there from game)
- Setup M4A function pointers
- Initialize M4A sequencer state

### SWI 0x1B: SoundDriverMode

**Status:** ❌ Stub (returns success)  
**Purpose:** Set reverb/volume modes  
**Note:** M4A-specific, no effect without M4A emulation

### SWI 0x1C: SoundDriverMain

**Status:** ❌ Stub (returns success)  
**Purpose:** Process sound commands, mix audio  
**Note:** This is the heart of M4A - called every frame

### SWI 0x1D: SoundDriverVSync

**Status:** ❌ Stub (returns success)  
**Purpose:** Sync audio with VBlank  
**Note:** M4A timing synchronization

### SWI 0x1E: SoundChannelClear

**Status:** ❌ Stub (returns success)  
**Purpose:** Stop/clear sound channels  
**Note:** M4A channel management

## Recommendations

### For DKC and M4A Games

Without M4A emulation, these games will:

- ✅ Boot and run correctly
- ✅ Display graphics properly
- ✅ Respond to input
- ❌ Have no music
- ❌ Have no/limited sound effects

**To Fix:** Implement Option 3 (Hybrid LLE for sound) or Option 2 (Full LLE BIOS)

### For Other Games

Most games using simple direct sound will work fine with current implementation.

## Hardware Register Reference

| Register   | Address    | Purpose             |
| ---------- | ---------- | ------------------- |
| SOUNDCNT_L | 0x04000080 | PSG volume/enable   |
| SOUNDCNT_H | 0x04000082 | DMA sound control   |
| SOUNDCNT_X | 0x04000084 | Master sound enable |
| SOUNDBIAS  | 0x04000088 | PWM bias level      |
| FIFO_A     | 0x040000A0 | DMA Sound A buffer  |
| FIFO_B     | 0x040000A4 | DMA Sound B buffer  |

## Testing

### Test Direct Sound

```bash
./build/bin/HeadlessTest <game_using_direct_sound.gba>
# Check for FIFO writes in log
```

### Test PSG (Square/Wave/Noise channels)

- Unit tests: `./build/bin/APUTests` — covers FIFO behavior, FIFO resets, and master enable. Run locally and ensure all `APUTests` pass.
- Quick manual test: run a game that exercises PSG (choose a simple homebrew or test ROM) in headless mode and watch for PCM output or FIFO activity.

### Test M4A Games

```bash
./build/bin/HeadlessTest DKC.gba
# Will boot but no audio (expected without M4A)
```

## Future Work

1. Implement PSG channels (square/wave/noise)
2. Add M4A detection and driver extraction
3. Implement basic M4A sequencer (Option 3)
4. Or add LLE BIOS support (Option 2)
5. Optimize FIFO refill timing
6. Add sound recording/WAV export
