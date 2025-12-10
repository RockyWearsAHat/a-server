#include <emulator/gba/M4AEngine.h>
#include <emulator/gba/GBAMemory.h>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace AIO::Emulator::GBA {

// ADPCM step table (standard IMA ADPCM)
static const int16_t adpcmStepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static const int8_t adpcmIndexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

M4AEngine::M4AEngine(GBAMemory* mem)
    : memory(mem), initialized(false) {
    std::memset(&workArea, 0, sizeof(workArea));
}

M4AEngine::~M4AEngine() {
}

void M4AEngine::Initialize(uint32_t workAreaAddr) {
    // Initialize work area structure
    std::memset(&workArea, 0, sizeof(workArea));
    
    workArea.magic = 0x68736D53; // "Smsh"
    workArea.maxChannels = 8;
    workArea.masterVolume = 15;
    workArea.tempo = 150;
    workArea.sequenceAddr = 0;
    workArea.bankAddr = 0;
    workArea.frameCounter = 0;
    
    // Initialize all channels
    for (int i = 0; i < 16; i++) {
        workArea.channels[i].active = false;
        workArea.channels[i].volume = 127;
        workArea.channels[i].pan = 0;
        workArea.channels[i].envPhase = 0;
    }
    
    initialized = true;
    
    // Write work area back to memory (if valid address)
    if (workAreaAddr >= 0x02000000 && workAreaAddr < 0x04000000) {
        memory->Write32(workAreaAddr + 0x00, workArea.magic);
        memory->Write8(workAreaAddr + 0x04, workArea.maxChannels);
        memory->Write8(workAreaAddr + 0x05, workArea.masterVolume);
        memory->Write8(workAreaAddr + 0x06, workArea.tempo);
    }
}

void M4AEngine::ProcessFrame() {
    if (!initialized) return;
    
    // Update all active channels
    for (int i = 0; i < workArea.maxChannels; i++) {
        if (workArea.channels[i].active) {
            UpdateChannel(workArea.channels[i]);
        }
    }
    
    // Process sequence commands (music data)
    // This would parse sequence data and trigger note on/off events
    // For now, we just keep existing notes playing
}

void M4AEngine::VSync() {
    if (!initialized) return;
    workArea.frameCounter++;
}

void M4AEngine::UpdateChannel(M4AChannel& ch) {
    if (!ch.active) return;
    
    // Update envelope
    UpdateEnvelope(ch);
    
    // Advance sample position
    ch.samplePos += ch.frequency;
    
    // Handle looping
    if (ch.samplePos >= ch.sampleEnd) {
        if (ch.loop && ch.loopStart < ch.sampleEnd) {
            ch.samplePos = ch.loopStart + (ch.samplePos - ch.sampleEnd);
        } else {
            ch.active = false;
        }
    }
}

void M4AEngine::UpdateEnvelope(M4AChannel& ch) {
    // Simplified ADSR envelope
    // Phase 0=Attack, 1=Decay, 2=Sustain, 3=Release
    
    switch (ch.envPhase) {
        case 0: // Attack
            ch.envCounter += ch.envAttack;
            if (ch.envCounter >= 0xFF00) {
                ch.envCounter = 0xFF00;
                ch.envPhase = 1;
            }
            break;
            
        case 1: // Decay
            if (ch.envCounter > (ch.envSustain << 8)) {
                ch.envCounter -= ch.envDecay;
            } else {
                ch.envPhase = 2;
            }
            break;
            
        case 2: // Sustain
            // Hold at sustain level
            break;
            
        case 3: // Release
            if (ch.envCounter > ch.envRelease) {
                ch.envCounter -= ch.envRelease;
            } else {
                ch.envCounter = 0;
                ch.active = false;
            }
            break;
    }
}

int16_t M4AEngine::GetSamplePCM16(M4AChannel& ch) {
    if (!ch.active || ch.sampleAddr == 0) {
        return 0;
    }
    
    uint32_t bytePos = ch.samplePos >> 16; // Convert fixed-point to integer
    
    int16_t sample = 0;
    
    switch (ch.sampleFormat) {
        case 0: // PCM8
        {
            if (ch.sampleAddr + bytePos < 0x0A000000) {
                int8_t pcm8 = (int8_t)memory->Read8(ch.sampleAddr + bytePos);
                sample = pcm8 << 8; // Convert 8-bit to 16-bit
            }
            break;
        }
        
        case 1: // PCM16
        {
            if (ch.sampleAddr + (bytePos * 2) + 1 < 0x0A000000) {
                sample = (int16_t)memory->Read16(ch.sampleAddr + (bytePos * 2));
            }
            break;
        }
        
        case 2: // ADPCM
        {
            // Decode ADPCM nibble
            if (ch.sampleAddr + (bytePos / 2) < 0x0A000000) {
                uint8_t byte = memory->Read8(ch.sampleAddr + (bytePos / 2));
                uint8_t nibble = (bytePos & 1) ? (byte >> 4) : (byte & 0x0F);
                
                // Decode nibble using step table
                int32_t step = adpcmStepTable[ch.adpcmIndex];
                int32_t diff = step >> 3;
                
                if (nibble & 1) diff += step >> 2;
                if (nibble & 2) diff += step >> 1;
                if (nibble & 4) diff += step;
                
                if (nibble & 8) {
                    ch.adpcmPcm -= diff;
                } else {
                    ch.adpcmPcm += diff;
                }
                
                // Clamp
                if (ch.adpcmPcm > 32767) ch.adpcmPcm = 32767;
                if (ch.adpcmPcm < -32768) ch.adpcmPcm = -32768;
                
                // Update index
                ch.adpcmIndex += adpcmIndexTable[nibble];
                if (ch.adpcmIndex < 0) ch.adpcmIndex = 0;
                if (ch.adpcmIndex > 88) ch.adpcmIndex = 88;
                
                sample = ch.adpcmPcm;
            }
            break;
        }
    }
    
    // Apply envelope
    sample = (sample * (ch.envCounter >> 8)) >> 8;
    
    // Apply channel volume
    sample = (sample * ch.volume) >> 7;
    
    return sample;
}

void M4AEngine::MixSamples(int16_t* buffer, size_t numSamples) {
    if (!initialized) {
        std::memset(buffer, 0, numSamples * 2 * sizeof(int16_t)); // Stereo
        return;
    }
    
    for (size_t i = 0; i < numSamples; i++) {
        int32_t left = 0;
        int32_t right = 0;
        
        // Mix all active channels
        for (int ch = 0; ch < workArea.maxChannels; ch++) {
            if (!workArea.channels[ch].active) continue;
            
            int16_t sample = GetSamplePCM16(workArea.channels[ch]);
            
            // Apply panning
            int8_t pan = workArea.channels[ch].pan;
            int16_t leftGain = 64 - pan;  // -64..+63 -> 128..1
            int16_t rightGain = 64 + pan; // -64..+63 -> 0..127
            
            left += (sample * leftGain) >> 7;
            right += (sample * rightGain) >> 7;
        }
        
        // Apply master volume
        left = (left * workArea.masterVolume) >> 4;
        right = (right * workArea.masterVolume) >> 4;
        
        // Clamp
        if (left > 32767) left = 32767;
        if (left < -32768) left = -32768;
        if (right > 32767) right = 32767;
        if (right < -32768) right = -32768;
        
        buffer[i * 2 + 0] = (int16_t)left;
        buffer[i * 2 + 1] = (int16_t)right;
    }
}

void M4AEngine::NoteOn(uint8_t channelIdx, uint8_t note, uint8_t velocity, uint32_t instrumentAddr) {
    if (channelIdx >= workArea.maxChannels) return;
    
    M4AChannel& ch = workArea.channels[channelIdx];
    
    // Read instrument data from ROM
    // Format: [type, key, length, loopStart, sample_addr, envelope...]
    uint8_t type = ReadByte(instrumentAddr + 0);
    uint8_t baseKey = ReadByte(instrumentAddr + 1);
    uint32_t sampleLen = ReadPointer(instrumentAddr + 4);
    uint32_t loopStart = ReadPointer(instrumentAddr + 8);
    uint32_t sampleAddr = ReadPointer(instrumentAddr + 12);
    
    ch.active = true;
    ch.sampleAddr = sampleAddr;
    ch.samplePos = 0;
    ch.sampleEnd = sampleLen << 16;
    ch.loopStart = loopStart << 16;
    ch.loop = (loopStart < sampleLen);
    ch.sampleFormat = type & 0x0F;
    ch.volume = velocity;
    ch.pan = 0;
    
    // Calculate frequency from note
    // GBA frequency = (sample_rate * note_freq) / output_rate
    // Standard: A4(440Hz) = MIDI note 69
    double noteFreq = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    double sampleRate = 22050.0; // Typical GBA sample rate
    double outputRate = 32768.0; // GBA output rate
    ch.frequency = (uint32_t)((sampleRate * noteFreq / outputRate) * 65536.0);
    
    // Initialize envelope
    ch.envPhase = 0;
    ch.envCounter = 0;
    ch.envAttack = 0xFF;
    ch.envDecay = 0x10;
    ch.envSustain = 0xC0;
    ch.envRelease = 0x08;
    
    // Initialize ADPCM state
    ch.adpcmPcm = 0;
    ch.adpcmIndex = 0;
}

void M4AEngine::NoteOff(uint8_t channelIdx) {
    if (channelIdx >= workArea.maxChannels) return;
    workArea.channels[channelIdx].envPhase = 3; // Enter release phase
}

void M4AEngine::PlaySequence(uint32_t sequenceAddr, uint32_t bankAddr) {
    workArea.sequenceAddr = sequenceAddr;
    workArea.bankAddr = bankAddr;
    // Would parse sequence data and start playback
}

void M4AEngine::StopAll() {
    for (int i = 0; i < 16; i++) {
        workArea.channels[i].active = false;
    }
}

uint32_t M4AEngine::ReadPointer(uint32_t addr) {
    if (addr >= 0x08000000 && addr + 3 < 0x0A000000) {
        return memory->Read32(addr);
    }
    return 0;
}

uint8_t M4AEngine::ReadByte(uint32_t addr) {
    if (addr >= 0x08000000 && addr < 0x0A000000) {
        return memory->Read8(addr);
    }
    return 0;
}

uint16_t M4AEngine::ReadHalfword(uint32_t addr) {
    if (addr >= 0x08000000 && addr + 1 < 0x0A000000) {
        return memory->Read16(addr);
    }
    return 0;
}

void M4AEngine::ProcessSequenceCommands() {
    // This would parse the sequence data format
    // Typical commands: note_on, note_off, tempo, volume, etc.
    // For now, stub implementation
}

} // namespace AIO::Emulator::GBA
