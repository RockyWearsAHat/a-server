#include "emulator/switch/MemoryManager.h"
#include <iostream>
#include <iomanip>
#include <cstring>

namespace AIO::Emulator::Switch {

    MemoryManager::MemoryManager() {
    }

    MemoryManager::~MemoryManager() {
    }

    MemoryManager::MemoryRegion* MemoryManager::GetRegion(uint64_t address) {
        // Find the first element with a key > address
        auto it = regions.upper_bound(address);
        
        // If it's the begin iterator, then no region starts <= address
        if (it == regions.begin()) {
            return nullptr;
        }

        // Go back one to find the region that starts <= address
        --it;
        
        MemoryRegion& region = it->second;
        if (address >= region.baseAddress && address < region.baseAddress + region.size) {
            return &region;
        }

        return nullptr;
    }

    bool MemoryManager::MapMemory(uint64_t address, size_t size, uint32_t permissions) {
        // Check for overlaps (simplified)
        if (GetRegion(address) || GetRegion(address + size - 1)) {
            std::cerr << "[Memory] Failed to map memory at 0x" << std::hex << address << ": Overlap detected" << std::dec << std::endl;
            return false;
        }

        MemoryRegion region;
        region.baseAddress = address;
        region.size = size;
        region.permissions = permissions;
        region.data.resize(size, 0);

        regions[address] = std::move(region);
        // std::cout << "[Memory] Mapped " << size << " bytes at 0x" << std::hex << address << std::dec << std::endl;
        return true;
    }

    bool MemoryManager::LoadData(uint64_t address, const std::vector<uint8_t>& data) {
        return LoadData(address, data.data(), data.size());
    }

    bool MemoryManager::LoadData(uint64_t address, const uint8_t* data, size_t size) {
        MemoryRegion* region = GetRegion(address);
        if (!region) {
            std::cerr << "[Memory] LoadData failed: No region at 0x" << std::hex << address << std::dec << std::endl;
            return false;
        }

        uint64_t offset = address - region->baseAddress;
        if (offset + size > region->size) {
            std::cerr << "[Memory] LoadData failed: Region overflow" << std::endl;
            return false;
        }

        std::memcpy(region->data.data() + offset, data, size);
        return true;
    }

    uint8_t MemoryManager::Read8(uint64_t address) {
        MemoryRegion* region = GetRegion(address);
        if (region) {
            return region->data[address - region->baseAddress];
        }
        // std::cerr << "[Memory] Read8 Fault at 0x" << std::hex << address << std::dec << std::endl;
        return 0;
    }

    uint16_t MemoryManager::Read16(uint64_t address) {
        MemoryRegion* region = GetRegion(address);
        if (region) {
            uint64_t offset = address - region->baseAddress;
            if (offset + 1 < region->size) {
                return region->data[offset] | (region->data[offset + 1] << 8);
            }
        }
        return 0;
    }

    uint32_t MemoryManager::Read32(uint64_t address) {
        MemoryRegion* region = GetRegion(address);
        if (region) {
            uint64_t offset = address - region->baseAddress;
            if (offset + 3 < region->size) {
                return region->data[offset] | (region->data[offset + 1] << 8) |
                       (region->data[offset + 2] << 16) | (region->data[offset + 3] << 24);
            }
        }
        return 0;
    }

    uint64_t MemoryManager::Read64(uint64_t address) {
        uint32_t low = Read32(address);
        uint32_t high = Read32(address + 4);
        return (uint64_t)low | ((uint64_t)high << 32);
    }

    void MemoryManager::Write8(uint64_t address, uint8_t value) {
        MemoryRegion* region = GetRegion(address);
        if (region) {
            region->data[address - region->baseAddress] = value;
        } else {
            // std::cerr << "[Memory] Write8 Fault at 0x" << std::hex << address << std::dec << std::endl;
        }
    }

    void MemoryManager::Write16(uint64_t address, uint16_t value) {
        Write8(address, value & 0xFF);
        Write8(address + 1, (value >> 8) & 0xFF);
    }

    void MemoryManager::Write32(uint64_t address, uint32_t value) {
        Write8(address, value & 0xFF);
        Write8(address + 1, (value >> 8) & 0xFF);
        Write8(address + 2, (value >> 16) & 0xFF);
        Write8(address + 3, (value >> 24) & 0xFF);
    }

    void MemoryManager::Write64(uint64_t address, uint64_t value) {
        Write32(address, value & 0xFFFFFFFF);
        Write32(address + 4, (value >> 32) & 0xFFFFFFFF);
    }

    void MemoryManager::DumpMemory(uint64_t address, size_t size) {
        std::cout << "Memory Dump at 0x" << std::hex << address << ":" << std::endl;
        for (size_t i = 0; i < size; ++i) {
            if (i % 16 == 0) std::cout << std::setw(4) << i << ": ";
            std::cout << std::setw(2) << std::setfill('0') << (int)Read8(address + i) << " ";
            if (i % 16 == 15) std::cout << std::endl;
        }
        std::cout << std::dec << std::endl;
    }

}
