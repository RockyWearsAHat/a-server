#include "emulator/switch/SwitchEmulator.h"
#include "emulator/switch/MemoryManager.h"
#include "emulator/switch/CpuCore.h"
#include "emulator/switch/ServiceManager.h"
#include "emulator/switch/GpuCore.h"
#include <iostream>
#include <fstream>

namespace AIO::Emulator::Switch {

    SwitchEmulator::SwitchEmulator() : isRunning(false) {
        memory = std::make_unique<MemoryManager>();
        cpu = std::make_unique<CpuCore>(*memory);
        serviceManager = std::make_unique<ServiceManager>();
        gpu = std::make_unique<GpuCore>(*memory);
        std::cout << "[Switch] Emulator Initialized" << std::endl;
    }

    SwitchEmulator::~SwitchEmulator() = default;

    bool SwitchEmulator::LoadROM(const std::string& path) {
        std::cout << "[Switch] Loading ROM: " << path << std::endl;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "[Switch] Failed to open ROM file" << std::endl;
            return false;
        }
        
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        std::vector<uint8_t> buffer(size);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
            std::cerr << "[Switch] Failed to read ROM file" << std::endl;
            return false;
        }
        
        // Check for NSO0 Magic
        if (size > 4 && buffer[0] == 'N' && buffer[1] == 'S' && buffer[2] == 'O' && buffer[3] == '0') {
            return LoadNSO(buffer);
        }
        
        std::cerr << "[Switch] Unknown file format (Not NSO0)" << std::endl;
        return false;
    }

    bool SwitchEmulator::LoadNSO(const std::vector<uint8_t>& data) {
        std::cout << "[Switch] Loading NSO..." << std::endl;
        
        // NSO Header Structure (Simplified)
        // 0x00: Magic (NSO0)
        // 0x04: Version
        // 0x08: Reserved
        // 0x0C: Flags
        // 0x10: Text Segment Header (FileOffset, MemoryOffset, Size)
        // 0x20: Module Name Offset
        // 0x24: RO Segment Header
        // 0x34: Module Name Size
        // 0x38: Data Segment Header
        // ...
        
        // For now, let's just map the whole file to a base address (e.g., 0x8000000)
        // In reality, we need to decompress segments (LZ4) and map them to correct offsets.
        
        uint64_t baseAddress = 0x8000000; // Typical load address
        
        if (!memory->MapMemory(baseAddress, data.size(), 7)) { // RWX
            return false;
        }
        
        if (!memory->LoadData(baseAddress, data)) {
            return false;
        }
        
        cpu->Reset();
        cpu->SetPC(baseAddress);
        
        std::cout << "[Switch] NSO Loaded at 0x" << std::hex << baseAddress << std::dec << std::endl;
        isRunning = true;
        return true;
    }

    void SwitchEmulator::RunFrame() {
        if (!isRunning) return;
        
        // Run ~10000 cycles per frame for now
        cpu->Run(10000);
        
        // Process GPU commands (Dummy)
        // In reality, CPU writes to GPFIFO and GPU processes it in parallel
        gpu->ProcessCommands(0, 0);
    }

    void SwitchEmulator::Reset() {
        isRunning = false;
        cpu->Reset();
        gpu->Reset();
        // TODO: Clear memory
        std::cout << "[Switch] Reset" << std::endl;
    }

    std::string SwitchEmulator::GetDebugInfo() {
        return "Switch Emulator: Running=" + std::to_string(isRunning) + "\n" + cpu->GetStateString();
    }
    
    GpuCore* SwitchEmulator::GetGPU() const {
        return gpu.get();
    }

}
