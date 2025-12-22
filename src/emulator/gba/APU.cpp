#include <emulator/gba/APU.h>
#include <emulator/gba/GBAMemory.h>
#include <emulator/gba/IORegs.h>
#include <emulator/common/Logger.h>
#include <cstdlib>
#include <iostream>
#include <cstring>

namespace AIO::Emulator::GBA {

    namespace {
        bool TraceGbaSpam() {
            static const bool enabled = (std::getenv("AIO_TRACE_GBA_SPAM") != nullptr);
            return enabled;
        }

        bool TraceAudioTimerRate() {
            static const bool enabled = (std::getenv("AIO_TRACE_AUDIO_TIMER_RATE") != nullptr);
            return enabled;
        }

        bool FifoTreatSamplesUnsigned() {
            static const bool enabled = (std::getenv("AIO_AUDIO_FIFO_UNSIGNED") != nullptr);
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

        sampleAccumulator = 0.0f;
        currentUpsampleRatio = 2.45f;
        hasPrevInputSample = false;
        prevInputLeft = 0;
        prevInputRight = 0;
        resamplePhase = 0.0f;
    }

    void APU::PushSample(int16_t left, int16_t right) {
        // Convert from game's timer-based sample rate to our fixed OUTPUT_SAMPLE_RATE.
        // Use a constant-phase linear resampler: output samples are emitted at a constant
        // output rate, by advancing a phase accumulator within each input interval.

        auto pushOne = [&](int16_t outL, int16_t outR) -> bool {
            const int wp = writePos.load(std::memory_order_relaxed);
            const int rp = readPos.load(std::memory_order_acquire);
            const int nextWp = (wp + 2) % (RING_BUFFER_SIZE * 2);
            if (nextWp == rp) {
                stats.ringOverrunDrops.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            ringBuffer[wp] = outL;
            ringBuffer[wp + 1] = outR;
            writePos.store(nextWp, std::memory_order_release);
            return true;
        };

        if (!hasPrevInputSample) {
            hasPrevInputSample = true;
            prevInputLeft = left;
            prevInputRight = right;
            resamplePhase = 0.0f;
            return;
        }

        // Phase step per *output* sample, expressed in units of input-sample intervals.
        // currentUpsampleRatio = OUTPUT / INPUT, so phaseStep = INPUT / OUTPUT = 1 / upsample.
        const float phaseStep = (currentUpsampleRatio > 0.000001f) ? (1.0f / currentUpsampleRatio) : 0.0f;
        if (phaseStep <= 0.0f) {
            prevInputLeft = left;
            prevInputRight = right;
            return;
        }

        const float in0L = (float)prevInputLeft;
        const float in0R = (float)prevInputRight;
        const float in1L = (float)left;
        const float in1R = (float)right;

        // Emit as many output samples as fit in this input interval.
        // Keep resamplePhase carried across intervals so output timing stays uniform.
        while (resamplePhase < 1.0f) {
            const float t = resamplePhase;
            const float lf = in0L + (in1L - in0L) * t;
            const float rf = in0R + (in1R - in0R) * t;
            const int16_t outL = (int16_t)lrintf(lf);
            const int16_t outR = (int16_t)lrintf(rf);
            if (!pushOne(outL, outR)) {
                // If we're overrun, drop the remainder for this interval.
                break;
            }
            resamplePhase += phaseStep;
        }

        // Carry remainder into next interval.
        if (resamplePhase >= 1.0f) {
            resamplePhase -= 1.0f;
        }

        prevInputLeft = left;
        prevInputRight = right;
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

        // Only generate output samples on the timer(s) that actually clock FIFO A/B.
        // Timer0/1 are commonly used for non-audio purposes; pushing samples on those
        // overflows creates audible crackle/jitter.
        if (timer != fifoATimer && timer != fifoBTimer) {
            return;
        }
        
        // Timer registers:
        // - TMxCNT_L reads should return the *current counter* (GBAMemory::Read16 emulates this).
        // - For computing the timer period we need the *reload value last written* (raw IO backing store).
        const int audioTimer = timer;
        const uint32_t tmBaseOff = IORegs::TM0CNT_L + (uint32_t)audioTimer * IORegs::TIMER_CHANNEL_SIZE;
        const uint16_t tmReload = memory.ReadIORegisterRaw16(tmBaseOff);
        const uint16_t tmControl = memory.ReadIORegisterRaw16(tmBaseOff + 2);
        
        // Calculate the actual sample rate from timer configuration
        int prescaler = 1;
        switch (tmControl & 3) {
            case 0: prescaler = 1; break;
            case 1: prescaler = 64; break;
            case 2: prescaler = 256; break;
            case 3: prescaler = 1024; break;
        }
        const int reloadDelta = 0x10000 - (int)tmReload;
        const int cyclesPerSample = reloadDelta * prescaler;
        if (cyclesPerSample <= 0) {
            // Defensive: avoid divide-by-zero / nonsense if timer isn't configured yet.
            return;
        }
        const float inputSampleRate = GBA_CPU_FREQ / (float)cyclesPerSample;
        currentUpsampleRatio = OUTPUT_SAMPLE_RATE / inputSampleRate;

        if (TraceAudioTimerRate()) {
            static int logs = 0;
            static int lastRate = -1;
            const int rateInt = (int)(inputSampleRate + 0.5f);
            // Log first few times + whenever the computed rate changes (helps catch bad timer config).
            if (logs < 20 || rateInt != lastRate) {
                logs++;
                lastRate = rateInt;
                const uint16_t scntX = memory.Read16(IORegs::REG_SOUNDCNT_X);
                std::cout << "[AUDIO_TIMER] timer=" << audioTimer
                          << " reload=0x" << std::hex << (unsigned)tmReload << std::dec
                          << " presc=" << prescaler
                          << " rate=" << (double)inputSampleRate << "Hz"
                          << " upsample=" << (double)currentUpsampleRatio
                          << " scntH=0x" << std::hex << (unsigned)scntH << std::dec
                          << " scntX=0x" << std::hex << (unsigned)scntX << std::dec
                          << " fifoA=" << fifoA_Count
                          << " fifoB=" << fifoB_Count
                          << std::endl;
            }
        }
        
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
        } else if (timer == fifoATimer) {
            // FIFO underflow: output silence.
            currentSampleA = 0;
            stats.fifoAUnderflows.fetch_add(1, std::memory_order_relaxed);
        }
        
        if (timer == fifoBTimer && fifoB_Count > 0) {
            currentSampleB = fifoB[fifoB_ReadPos];
            fifoB_ReadPos = (fifoB_ReadPos + 1) % 32;
            fifoB_Count--;
            consumedB = true;
        } else if (timer == fifoBTimer) {
            // FIFO underflow: output silence.
            currentSampleB = 0;
            stats.fifoBUnderflows.fetch_add(1, std::memory_order_relaxed);
        }
        
        // Check if master sound is enabled
        uint16_t scntX = memory.Read16(IORegs::REG_SOUNDCNT_X);
        if (!(scntX & 0x80)) {
            PushSample(0, 0);
            return;
        }
        
        int32_t left = 0;
        int32_t right = 0;
        
        // FIFO A volume (bit 2: 0=50%, 1=100%)
        int volA = (scntH & 0x04) ? 2 : 1;
        // FIFO B volume (bit 3: 0=50%, 1=100%)  
        int volB = (scntH & 0x08) ? 2 : 1;
        
        // FIFO A enable left/right (bits 9, 8)
        if (scntH & 0x200) left += (int32_t)currentSampleA * volA * 64;
        if (scntH & 0x100) right += (int32_t)currentSampleA * volA * 64;
        
        // FIFO B enable left/right (bits 13, 12)
        if (scntH & 0x2000) left += (int32_t)currentSampleB * volB * 64;
        if (scntH & 0x1000) right += (int32_t)currentSampleB * volB * 64;

        // Clamp to signed 16-bit to avoid wraparound distortion.
        if (left < -32768) left = -32768;
        if (left > 32767) left = 32767;
        if (right < -32768) right = -32768;
        if (right > 32767) right = 32767;

        PushSample((int16_t)left, (int16_t)right);
    }

    void APU::WriteFIFO_A(uint32_t value) {
        // Write 4 bytes (samples) to FIFO A
        for (int i = 0; i < 4; i++) {
            if (fifoA_Count < 32) {
                const uint8_t raw = (uint8_t)((value >> (i * 8)) & 0xFF);
                const int8_t sample = FifoTreatSamplesUnsigned() ? (int8_t)((int)raw - 128) : (int8_t)raw;
                fifoA[fifoA_WritePos] = sample;
                fifoA_WritePos = (fifoA_WritePos + 1) % 32;
                fifoA_Count++;
            }
        }
    }

    void APU::WriteFIFO_A16(uint16_t value) {
        // Write 2 bytes (samples) to FIFO A
        for (int i = 0; i < 2; i++) {
            if (fifoA_Count < 32) {
                const uint8_t raw = (uint8_t)((value >> (i * 8)) & 0xFF);
                const int8_t sample = FifoTreatSamplesUnsigned() ? (int8_t)((int)raw - 128) : (int8_t)raw;
                fifoA[fifoA_WritePos] = sample;
                fifoA_WritePos = (fifoA_WritePos + 1) % 32;
                fifoA_Count++;
            }
        }
    }

    void APU::WriteFIFO_A8(uint8_t value) {
        // Write 1 byte (sample) to FIFO A
        if (fifoA_Count < 32) {
            const uint8_t raw = value;
            const int8_t sample = FifoTreatSamplesUnsigned() ? (int8_t)((int)raw - 128) : (int8_t)raw;
            fifoA[fifoA_WritePos] = sample;
            fifoA_WritePos = (fifoA_WritePos + 1) % 32;
            fifoA_Count++;
        }
    }

    void APU::WriteFIFO_B(uint32_t value) {
        // Write 4 bytes (samples) to FIFO B
        for (int i = 0; i < 4; i++) {
            if (fifoB_Count < 32) {
                const uint8_t raw = (uint8_t)((value >> (i * 8)) & 0xFF);
                const int8_t sample = FifoTreatSamplesUnsigned() ? (int8_t)((int)raw - 128) : (int8_t)raw;
                fifoB[fifoB_WritePos] = sample;
                fifoB_WritePos = (fifoB_WritePos + 1) % 32;
                fifoB_Count++;
            }
        }
    }

    void APU::WriteFIFO_B16(uint16_t value) {
        // Write 2 bytes (samples) to FIFO B
        for (int i = 0; i < 2; i++) {
            if (fifoB_Count < 32) {
                const uint8_t raw = (uint8_t)((value >> (i * 8)) & 0xFF);
                const int8_t sample = FifoTreatSamplesUnsigned() ? (int8_t)((int)raw - 128) : (int8_t)raw;
                fifoB[fifoB_WritePos] = sample;
                fifoB_WritePos = (fifoB_WritePos + 1) % 32;
                fifoB_Count++;
            }
        }
    }

    void APU::WriteFIFO_B8(uint8_t value) {
        // Write 1 byte (sample) to FIFO B
        if (fifoB_Count < 32) {
            const uint8_t raw = value;
            const int8_t sample = FifoTreatSamplesUnsigned() ? (int8_t)((int)raw - 128) : (int8_t)raw;
            fifoB[fifoB_WritePos] = sample;
            fifoB_WritePos = (fifoB_WritePos + 1) % 32;
            fifoB_Count++;
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

        // Optional: periodic diagnostics from the audio callback thread.
        const bool traceAudioStats = (std::getenv("AIO_TRACE_AUDIO_STATS") != nullptr);
        static uint32_t samplesSinceLastLog = 0;
        uint32_t localUnderruns = 0;
        
        for (int i = 0; i < numSamples; i++) {
            int rp = readPos.load(std::memory_order_relaxed);
            int wp = writePos.load(std::memory_order_acquire);
            
            if (rp == wp) {
                // Buffer empty - fill rest with silence
                buffer[i * 2] = 0;
                buffer[i * 2 + 1] = 0;
                localUnderruns++;
            } else {
                buffer[i * 2] = ringBuffer[rp];
                buffer[i * 2 + 1] = ringBuffer[rp + 1];
                
                int nextRp = (rp + 2) % (RING_BUFFER_SIZE * 2);
                readPos.store(nextRp, std::memory_order_release);
                samplesWritten++;
            }
        }

        if (localUnderruns != 0) {
            stats.ringUnderrunSamples.fetch_add(localUnderruns, std::memory_order_relaxed);
        }

        if (traceAudioStats) {
            samplesSinceLastLog += (uint32_t)numSamples;
            if (samplesSinceLastLog >= (uint32_t)OUTPUT_SAMPLE_RATE) {
                samplesSinceLastLog = 0;
                const uint64_t underruns = stats.ringUnderrunSamples.exchange(0, std::memory_order_relaxed);
                const uint64_t drops = stats.ringOverrunDrops.exchange(0, std::memory_order_relaxed);
                const uint64_t ufa = stats.fifoAUnderflows.exchange(0, std::memory_order_relaxed);
                const uint64_t ufb = stats.fifoBUnderflows.exchange(0, std::memory_order_relaxed);

                const int rp = readPos.load(std::memory_order_relaxed);
                const int wp = writePos.load(std::memory_order_relaxed);
                int fillSamples = wp - rp;
                if (fillSamples < 0) fillSamples += (RING_BUFFER_SIZE * 2);
                fillSamples /= 2;

                std::cout << "[AUDIO] underrunSamples/s=" << underruns
                          << " ringDrops/s=" << drops
                          << " fifoAUnderflow/s=" << ufa
                          << " fifoBUnderflow/s=" << ufb
                          << " ringFill=" << fillSamples << "/" << RING_BUFFER_SIZE
                          << " upsample=" << currentUpsampleRatio
                          << " fifoA=" << fifoA_Count
                          << " fifoB=" << fifoB_Count
                          << std::endl;
            }
        }
        
        return samplesWritten;
    }

    bool APU::IsSoundEnabled() const {
        return (soundcntX & 0x80) != 0;
    }

}
