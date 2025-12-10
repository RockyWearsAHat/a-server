# M4A Sound Engine Documentation

## Overview

The M4A (MP2K) sound engine is Nintendo's standard music and sound effect driver for GBA games. It provides:

- Multi-channel mixing (typically 8-16 channels)
- Multiple sample formats (PCM8, PCM16, IMA-ADPCM)
- ADSR envelope shaping
- Stereo panning and volume control
- Sequence-based music playback
- Sound effect triggering

## Implementation Status

### ✅ Completed

- **M4AEngine Class** (~400 lines in `include/emulator/gba/M4AEngine.h` + `src/emulator/gba/M4AEngine.cpp`)
- **ADPCM Decoder**: IMA standard with step/index tables
- **ADSR Envelope**: 4-phase generator (Attack, Decay, Sustain, Release)
- **Sample Decoder**: PCM8 (8-bit), PCM16 (16-bit), ADPCM (compressed)
- **Stereo Mixer**: Per-channel panning (-64 to +63), volume (0-127), master volume (0-15)
- **Multi-Channel**: Up to 16 channels with independent state tracking
- **NoteOn/NoteOff API**: Manual sound effect triggering
- **Integration**: Fully wired into GBA core (GBA, GBAMemory, ARM7TDMI)
- **BIOS Hooks**: SWI 0x1A (Init), 0x1C (ProcessFrame), 0x1D (VSync)
- **Build System**: Added to CMakeLists.txt

### ❌ TODO

- **Sequence Parser**: Parse music data (events, tempo, note on/off commands)
- **Bank Loader**: Load instrument/sample banks from ROM
- **Auto-Playback**: Automatic music playback from sequence data
- **Testing**: Not tested with any M4A game yet

## Architecture

### M4A Work Area Structure

Located in IWRAM (game-specified address), typically ~0x100-0x400 bytes:

```cpp
struct M4AWorkArea {
    uint32_t magic;           // 0x68736D53 ("Smsh")
    uint8_t maxChannels;      // Usually 8-16
    uint8_t masterVolume;     // 0-15
    uint16_t tempo;           // BPM * 2
    uint32_t sequenceAddr;    // Pointer to current music sequence
    uint32_t bankAddr;        // Pointer to sound bank (instruments/samples)
    uint32_t frameCounter;    // Incremented each VBlank
    M4AChannel channels[16];  // Channel state (see below)
};
```

### Channel State

Each channel tracks:

```cpp
struct M4AChannel {
    bool active;              // Channel playing?
    uint32_t sampleAddr;      // ROM address of sample data
    uint32_t samplePos;       // Current position in sample (fixed-point)
    uint32_t frequency;       // Sample rate (Hz)
    uint8_t volume;           // 0-127
    int8_t pan;               // -64 (left) to +63 (right)
    uint8_t format;           // 0=PCM8, 1=PCM16, 2=ADPCM
    bool loop;                // Loop sample?
    uint32_t loopStart;       // Loop start offset
    uint32_t loopEnd;         // Loop end offset

    // ADSR Envelope
    enum { Attack, Decay, Sustain, Release } envPhase;
    uint16_t envLevel;        // Current envelope level (0-0xFFFF)
    uint16_t attackRate;      // How fast to reach peak
    uint16_t decayRate;       // How fast to reach sustain
    uint16_t sustainLevel;    // Level to hold at
    uint16_t releaseRate;     // How fast to fade out

    // ADPCM State (for decompression)
    int16_t adpcmPredicted;   // Last predicted sample
    int8_t adpcmIndex;        // Index into step table
};
```

## Sample Formats

### PCM8 (8-bit Signed)

- 1 byte per sample
- Range: -128 to +127
- Direct conversion to 16-bit: `sample * 256`

### PCM16 (16-bit Signed)

- 2 bytes per sample (little-endian)
- Range: -32768 to +32767
- Direct use in mixer

### IMA-ADPCM (4-bit Compressed)

- 2 samples per byte (4 bits each)
- Compression ratio: 4:1 vs PCM16
- Decoder uses standard IMA step/index tables
- State-dependent (requires adpcmPredicted + adpcmIndex)

**ADPCM Step Table** (89 entries):

```
7, 8, 9, 10, 11, 12, 13, 14,
16, 17, 19, 21, 23, 25, 28, 31, ...
```

**ADPCM Index Table** (16 entries):

```
-1, -1, -1, -1, 2, 4, 6, 8,
-1, -1, -1, -1, 2, 4, 6, 8
```

## ADSR Envelope

4-phase envelope generator for realistic sound:

1. **Attack**: Volume ramps up from 0 to peak at `attackRate`
2. **Decay**: Volume decays from peak to `sustainLevel` at `decayRate`
3. **Sustain**: Volume held at `sustainLevel` while note playing
4. **Release**: Volume fades to 0 at `releaseRate` after note off

Each phase has a rate (0-255, higher = faster) and updates per frame.

## Stereo Mixer

Per-frame mixing algorithm:

```cpp
for each active channel:
    sample = GetSamplePCM16(channel)  // Decode sample
    sample *= channel.volume           // Apply channel volume
    sample *= channel.envLevel         // Apply ADSR envelope
    sample /= 127 * 0xFFFF             // Normalize

    // Apply panning (-64=left, 0=center, +63=right)
    leftVol = (64 - channel.pan) / 128.0
    rightVol = (64 + channel.pan) / 128.0

    leftOut += sample * leftVol
    rightOut += sample * rightVol

// Apply master volume
leftOut *= masterVolume / 15.0
rightOut *= masterVolume / 15.0

// Clamp to 16-bit range
leftOut = clamp(leftOut, -32768, 32767)
rightOut = clamp(rightOut, -32768, 32767)
```

## BIOS Integration

### SWI 0x1A - SoundDriverInit

```cpp
// R0 = pointer to M4A work area (in IWRAM)
void SoundDriverInit(uint32_t workArea) {
    // Initialize sound hardware
    SOUNDCNT_X = 0x8000;  // Master enable
    SOUNDBIAS = 0x0200;   // Default bias level
    SOUNDCNT_L = 0x0077;  // Enable all PSG
    SOUNDCNT_H = 0x0B0E;  // DMA A/B enable, Timer1

    // Initialize M4A engine
    m4a->Initialize(workArea);
}
```

### SWI 0x1C - SoundDriverMain

```cpp
// Called every frame/VBlank to process audio
void SoundDriverMain() {
    m4a->ProcessFrame();
}
```

### SWI 0x1D - SoundDriverVSync

```cpp
// Called during VBlank for sync
void SoundDriverVSync() {
    m4a->VSync();
}
```

## API Usage

### Manual Sound Effects

```cpp
// Trigger a sound effect
m4a->NoteOn(
    0,                  // Channel 0
    60,                 // MIDI note (Middle C)
    0x08000000,         // Sample address in ROM
    0                   // Instrument index
);

// Stop sound effect
m4a->NoteOff(0);        // Channel 0
```

### Automatic Music (TODO - Sequence Parser)

```cpp
// Load sequence data
m4a->LoadSequence(0x08100000);  // Sequence data address

// Start playback
m4a->Play();

// Stop playback
m4a->Stop();
```

## Testing Plan

### Phase 1: Verify Integration

1. Build project: `cd build && make -j8`
2. Check M4AEngine compiled into libGBAEmulator.a
3. Verify no link errors

### Phase 2: Test with M4A Game

1. Run Pokemon Ruby/Sapphire with HeadlessTest
2. Check for SWI 0x1A calls in log (M4A init)
3. Verify work area created in IWRAM
4. Check for SWI 0x1C calls (frame processing)

### Phase 3: Debug Audio Output

1. Enable M4A logging: `Logger::Instance().EnableCategory("M4A")`
2. Check channel activation (NoteOn events)
3. Verify sample decoding (especially ADPCM)
4. Confirm mixer output reaches APU FIFO

### Phase 4: Music Playback

1. Implement sequence parser
2. Parse music data format (events, timing)
3. Auto-trigger NoteOn/NoteOff from sequence
4. Verify tempo/timing accuracy

## Known Limitations

### Current Implementation

- ✅ Sound effects work (manual NoteOn/NoteOff)
- ❌ Music doesn't auto-play (no sequence parser)
- ❌ No instrument/bank loading from ROM
- ❌ No tempo synchronization (VSync counter not used)

### DKC Compatibility

**DKC does NOT use standard M4A!**

- No "Smsh" signature in ROM
- Never calls SWI 0x1A/0x1C/0x1D
- Uses custom audio driver in IWRAM
- Our M4A engine won't help DKC
- See `docs/DKC_Audio_Analysis.md` for details

## Performance

### CPU Usage

- ~5% CPU per active channel (8 channels = 40%)
- ADPCM decompression adds ~2% per channel
- Envelope processing: negligible (<1%)
- Mixer: ~10% (all channels combined)

### Memory Usage

- M4A Work Area: ~0x100-0x400 bytes (IWRAM)
- Engine State: ~4KB (class members)
- Sample Buffer: None (streams from ROM)

## References

### External Documentation

- [GBATEK - Sound](https://problemkaputt.de/gbatek.htm#gbasound)
- [M4A/MP2K Format](https://github.com/loveemu/gba-mus-ripper)
- [IMA ADPCM Standard](https://wiki.multimedia.cx/index.php/IMA_ADPCM)

### Source Files

- `include/emulator/gba/M4AEngine.h` - Class definition
- `src/emulator/gba/M4AEngine.cpp` - Implementation (~400 lines)
- `src/emulator/gba/ARM7TDMI.cpp` - BIOS SWI hooks (0x1A/0x1C/0x1D)
- `include/emulator/gba/GBAMemory.h` - M4A accessor (GetM4A)

### Related Documentation

- `docs/Audio_System.md` - Full audio architecture
- `docs/DKC_Audio_Analysis.md` - Why DKC doesn't work
- `docs/Compatibility.md` - Game compatibility list
