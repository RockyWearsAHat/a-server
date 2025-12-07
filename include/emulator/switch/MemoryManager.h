#pragma once
#include <cstdint>
#include <vector>
#include <map>
#include <string>

namespace AIO::Emulator::Switch {

    class MemoryManager {
    public:
        MemoryManager();
        ~MemoryManager();

        // Basic Memory Access
        uint8_t Read8(uint64_t address);
        uint16_t Read16(uint64_t address);
        uint32_t Read32(uint64_t address);
        uint64_t Read64(uint64_t address);

        void Write8(uint64_t address, uint8_t value);
        void Write16(uint64_t address, uint16_t value);
        void Write32(uint64_t address, uint32_t value);
        void Write64(uint64_t address, uint64_t value);

        // Memory Mapping
        // Maps a region of memory. Returns true if successful.
        bool MapMemory(uint64_t address, size_t size, uint32_t permissions);
        
        // Load data into memory
        bool LoadData(uint64_t address, const std::vector<uint8_t>& data);
        bool LoadData(uint64_t address, const uint8_t* data, size_t size);

        // Debug
        void DumpMemory(uint64_t address, size_t size);

    private:
        struct MemoryRegion {
            std::vector<uint8_t> data;
            uint32_t permissions; // 1=R, 2=W, 4=X
            uint64_t baseAddress;
            size_t size;
        };

        // Map of base address to MemoryRegion
        // We use a map to easily find the region containing an address
        std::map<uint64_t, MemoryRegion> regions;

        // Helper to find the region for an address
        MemoryRegion* GetRegion(uint64_t address);
    };

}
