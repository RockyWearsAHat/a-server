#pragma once
#include <cstdint>
#include <vector>
#include <array>
#include <atomic>
#include <mutex>

namespace AIO::Emulator::GBA {

    class GBAMemory;

    class APU {
    public:
        APU(GBAMemory& memory);
        ~APU();

        void Reset();
        void Update(int cycles);
        
        // Called when timer overflows (for DMA sound)
        void OnTimerOverflow(int timer);
        
        // FIFO operations
        void WriteFIFO_A(uint32_t value);
        void WriteFIFO_B(uint32_t value);
        void ResetFIFO_A();
        void ResetFIFO_B();
        
        // Get samples for audio output - fills buffer with samples
        // Returns number of samples written
        int GetSamples(int16_t* buffer, int numSamples);
        
        // Check if sound is enabled
        bool IsSoundEnabled() const;

    private:
        GBAMemory& memory;
        
        // DMA Sound FIFOs (32 bytes each)
        std::array<int8_t, 32> fifoA;
        std::array<int8_t, 32> fifoB;
        int fifoA_ReadPos = 0;
        int fifoA_WritePos = 0;
        int fifoA_Count = 0;
        int fifoB_ReadPos = 0;
        int fifoB_WritePos = 0;
        int fifoB_Count = 0;
        
        // Current FIFO samples being output
        int8_t currentSampleA = 0;
        int8_t currentSampleB = 0;
        
        // Output ring buffer for audio thread (lock-free)
        static constexpr int RING_BUFFER_SIZE = 8192;
        std::array<int16_t, RING_BUFFER_SIZE * 2> ringBuffer; // Stereo pairs
        std::atomic<int> writePos{0};
        std::atomic<int> readPos{0};
        
        // Sound control registers
        uint16_t soundcntH = 0;
        uint16_t soundcntX = 0;
        
        // Add a sample to the ring buffer
        void PushSample(int16_t left, int16_t right);
    };

}
