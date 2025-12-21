#include <emulator/gba/APU.h>
#include <emulator/gba/GBAMemory.h>
#include <emulator/gba/IORegs.h>
#include <cstdlib>
#include <iostream>
#include <cstring>

namespace AIO::Emulator::GBA {

    namespace {
        bool TraceGbaSpam() {
            static const bool enabled = (std::getenv("AIO_TRACE_GBA_SPAM") != nullptr);
            return enabled;
        }
    }

    APU::APU(GBAMemory& mem) : memory(mem) {
        Reset();
    }

    APU::~APU() = default;

    void APU::Reset() {
        fifoA.fill(0);
        fifoB.fill(0);
        fifoA_ReadPos = 0;
        fifoA_WritePos = 0;
        fifoA_Count = 0;
        fifoB_ReadPos = 0;
        fifoB_WritePos = 0;
        fifoB_Count = 0;
        
        currentSampleA = 0;
        currentSampleB = 0;
        
        ringBuffer.fill(0);
        writePos = 0;
        readPos = 0;
        
        soundcntH = 0;
        soundcntX = 0;
    }

    void APU::PushSample(int16_t left, int16_t right) {
        // Push the sample multiple times based on the upsample ratio
        // This converts from game's timer-based sample rate to our output rate
        sampleAccumulator += currentUpsampleRatio;
        
        while (sampleAccumulator >= 1.0f) {
            int wp = writePos.load(std::memory_order_relaxed);
            int rp = readPos.load(std::memory_order_acquire);
            
            // Calculate next write position
            int nextWp = (wp + 2) % (RING_BUFFER_SIZE * 2);
            
            // Check if buffer is full (leave one slot empty)
            if (nextWp == rp) {
                // Buffer full, drop sample
                sampleAccumulator = 0.0f;
                return;
            }
            
            ringBuffer[wp] = left;
            ringBuffer[wp + 1] = right;
            writePos.store(nextWp, std::memory_order_release);
            
            sampleAccumulator -= 1.0f;
        }
    }

    void APU::Update(int cycles) {
        // Read current sound control registers
        soundcntX = memory.Read16(IORegs::REG_SOUNDCNT_X);
        soundcntH = memory.Read16(IORegs::REG_SOUNDCNT_H);
        
        // Nothing else to do here - samples are pushed on timer overflow
    }

    void APU::OnTimerOverflow(int timer) {
        static int overflowCount = 0;
        overflowCount++;
        
        // Read SOUNDCNT_H to check which timer each FIFO uses
        uint16_t scntH = memory.Read16(IORegs::REG_SOUNDCNT_H);
        
        // FIFO A uses timer specified in bit 10 (0=Timer0, 1=Timer1)
        int fifoATimer = (scntH >> 10) & 1;
        // FIFO B uses timer specified in bit 14 (0=Timer0, 1=Timer1)
        int fifoBTimer = (scntH >> 14) & 1;
        
        // Read Timer 0 registers (or Timer 1 depending on which is used for audio)
        int audioTimer = fifoATimer; // Use FIFO A's timer for rate calculation
        uint16_t tmReload = memory.Read16(IORegs::BASE + IORegs::TM0CNT_L + audioTimer * IORegs::TIMER_CHANNEL_SIZE);
        uint16_t tmControl = memory.Read16(IORegs::BASE + IORegs::TM0CNT_H + audioTimer * IORegs::TIMER_CHANNEL_SIZE);
        
        // Calculate the actual sample rate from timer configuration
        int prescaler = 1;
        switch (tmControl & 3) {
            case 0: prescaler = 1; break;
            case 1: prescaler = 64; break;
            case 2: prescaler = 256; break;
            case 3: prescaler = 1024; break;
        }
        int cyclesPerSample = (0x10000 - tmReload) * prescaler;
        float inputSampleRate = GBA_CPU_FREQ / cyclesPerSample;
        currentUpsampleRatio = OUTPUT_SAMPLE_RATE / inputSampleRate;
        
        if (TraceGbaSpam() && (overflowCount <= 3 || overflowCount % 100000 == 0)) {
            std::cout << "[APU] Timer " << timer << " overflow #" << overflowCount 
                      << " FIFO_A count=" << fifoA_Count << " FIFO_B count=" << fifoB_Count 
                      << " inputRate=" << (int)inputSampleRate 
                      << " upsample=" << currentUpsampleRatio << std::endl;
        }
        
        bool consumedA = false;
        bool consumedB = false;
        
        // When the associated timer overflows, consume a sample from FIFO
        if (timer == fifoATimer && fifoA_Count > 0) {
            currentSampleA = fifoA[fifoA_ReadPos];
            fifoA_ReadPos = (fifoA_ReadPos + 1) % 32;
            fifoA_Count--;
            consumedA = true;
        }
        
        if (timer == fifoBTimer && fifoB_Count > 0) {
            currentSampleB = fifoB[fifoB_ReadPos];
            fifoB_ReadPos = (fifoB_ReadPos + 1) % 32;
            fifoB_Count--;
            consumedB = true;
        }
        
        // Check if master sound is enabled
        uint16_t scntX = memory.Read16(IORegs::REG_SOUNDCNT_X);
        if (!(scntX & 0x80)) {
            PushSample(0, 0);
            return;
        }
        
        int16_t left = 0;
        int16_t right = 0;
        
        // FIFO A volume (bit 2: 0=50%, 1=100%)
        int volA = (scntH & 0x04) ? 2 : 1;
        // FIFO B volume (bit 3: 0=50%, 1=100%)  
        int volB = (scntH & 0x08) ? 2 : 1;
        
        // FIFO A enable left/right (bits 9, 8)
        if (scntH & 0x200) left += currentSampleA * volA * 64;
        if (scntH & 0x100) right += currentSampleA * volA * 64;
        
        // FIFO B enable left/right (bits 13, 12)
        if (scntH & 0x2000) left += currentSampleB * volB * 64;
        if (scntH & 0x1000) right += currentSampleB * volB * 64;
        
        PushSample(left, right);
    }

    void APU::WriteFIFO_A(uint32_t value) {
        // Write 4 bytes (samples) to FIFO A
        for (int i = 0; i < 4; i++) {
            if (fifoA_Count < 32) {
                fifoA[fifoA_WritePos] = static_cast<int8_t>((value >> (i * 8)) & 0xFF);
                fifoA_WritePos = (fifoA_WritePos + 1) % 32;
                fifoA_Count++;
            }
        }
    }

    void APU::WriteFIFO_B(uint32_t value) {
        // Write 4 bytes (samples) to FIFO B
        for (int i = 0; i < 4; i++) {
            if (fifoB_Count < 32) {
                fifoB[fifoB_WritePos] = static_cast<int8_t>((value >> (i * 8)) & 0xFF);
                fifoB_WritePos = (fifoB_WritePos + 1) % 32;
                fifoB_Count++;
            }
        }
    }

    void APU::ResetFIFO_A() {
        fifoA.fill(0);
        fifoA_ReadPos = 0;
        fifoA_WritePos = 0;
        fifoA_Count = 0;
        currentSampleA = 0;
    }

    void APU::ResetFIFO_B() {
        fifoB.fill(0);
        fifoB_ReadPos = 0;
        fifoB_WritePos = 0;
        fifoB_Count = 0;
        currentSampleB = 0;
    }

    int APU::GetSamples(int16_t* buffer, int numSamples) {
        int samplesWritten = 0;
        
        for (int i = 0; i < numSamples; i++) {
            int rp = readPos.load(std::memory_order_relaxed);
            int wp = writePos.load(std::memory_order_acquire);
            
            if (rp == wp) {
                // Buffer empty - fill rest with silence
                buffer[i * 2] = 0;
                buffer[i * 2 + 1] = 0;
            } else {
                buffer[i * 2] = ringBuffer[rp];
                buffer[i * 2 + 1] = ringBuffer[rp + 1];
                
                int nextRp = (rp + 2) % (RING_BUFFER_SIZE * 2);
                readPos.store(nextRp, std::memory_order_release);
                samplesWritten++;
            }
        }
        
        return samplesWritten;
    }

    bool APU::IsSoundEnabled() const {
        return (soundcntX & 0x80) != 0;
    }

}
