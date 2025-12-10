#pragma once

#include <cstdint>
#include <vector>
#include <memory>

namespace AIO::Emulator::GBA {

// Forward declaration
class GBAMemory;

/**
 * M4A/MP2K Sound Engine Implementation
 * 
 * The M4A (MusicPlayer2000/MP2K) sound engine is Nintendo's proprietary
 * music driver used in many GBA games. This is a from-scratch HLE implementation
 * that replicates the behavior without executing the original ARM code.
 * 
 * Architecture:
 * - Work Area: Memory structure containing engine state (channels, tempo, etc.)
 * - Sound Banks: Collections of instrument samples and parameters
 * - Sequences: Music/SFX data (note data, tempo changes, etc.)
 * - Channels: Up to 8 simultaneous sound channels (notes playing)
 * - Mixer: Combines channel outputs into stereo FIFO buffers
 */

struct M4AChannel {
    bool active;
    uint32_t sampleAddr;      // ROM/RAM address of sample data
    uint32_t samplePos;       // Current position in sample (fixed-point)
    uint32_t sampleEnd;       // End position
    uint32_t loopStart;       // Loop start position
    uint32_t frequency;       // Playback frequency (fixed-point)
    uint8_t volume;           // 0-127
    int8_t pan;               // -64 to +63 (L to R)
    uint8_t envPhase;         // ADSR envelope phase
    uint16_t envCounter;      // Envelope counter
    uint8_t envAttack;        // Attack rate
    uint8_t envDecay;         // Decay rate
    uint8_t envSustain;       // Sustain level
    uint8_t envRelease;       // Release rate
    bool loop;                // Loop sample?
    uint8_t sampleFormat;     // 0=PCM8, 1=PCM16, 2=ADPCM
    
    // ADPCM state (for compressed samples)
    int16_t adpcmPcm;         // Current PCM value
    int16_t adpcmIndex;       // Index into step table
};

struct M4AWorkArea {
    uint32_t magic;           // "Smsh" = 0x68736D53
    uint8_t maxChannels;      // Usually 8
    uint8_t masterVolume;     // 0-15
    uint8_t tempo;            // Tempo (BPM-related)
    uint8_t reserved;
    uint32_t sequenceAddr;    // Current sequence being played
    uint32_t bankAddr;        // Current sound bank
    uint32_t frameCounter;    // Incremented each VSync
    M4AChannel channels[16];  // Channel state (usually only 8 used)
};

class M4AEngine {
public:
    M4AEngine(GBAMemory* memory);
    ~M4AEngine();
    
    // Called by SWI 0x1A - Initialize sound driver
    void Initialize(uint32_t workAreaAddr);
    
    // Called by SWI 0x1C - Process sound per frame
    void ProcessFrame();
    
    // Called by SWI 0x1D - VSync handler
    void VSync();
    
    // Called by game - Start playing a sequence
    void PlaySequence(uint32_t sequenceAddr, uint32_t bankAddr);
    
    // Called by game - Stop all sound
    void StopAll();
    
    // Mixer - generate samples for FIFO
    void MixSamples(int16_t* buffer, size_t numSamples);
    
private:
    GBAMemory* memory;
    M4AWorkArea workArea;
    bool initialized;
    
    // Sample playback
    int16_t GetSamplePCM16(M4AChannel& ch);
    void UpdateChannel(M4AChannel& ch);
    void UpdateEnvelope(M4AChannel& ch);
    
    // Sequence processing (music/SFX commands)
    void ProcessSequenceCommands();
    
    // Note control
    void NoteOn(uint8_t channelIdx, uint8_t note, uint8_t velocity, uint32_t instrumentAddr);
    void NoteOff(uint8_t channelIdx);
    
    // Helper functions
    uint32_t ReadPointer(uint32_t addr);
    uint8_t ReadByte(uint32_t addr);
    uint16_t ReadHalfword(uint32_t addr);
};

} // namespace AIO::Emulator::GBA
