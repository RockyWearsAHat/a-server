#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace AIO::Emulator::Switch {

    class MemoryManager;
    class CpuCore;
    class ServiceManager;
    class GpuCore;

    class SwitchEmulator {
    public:
        SwitchEmulator();
        ~SwitchEmulator();

        bool LoadROM(const std::string& path);
        void RunFrame();
        void Reset();
        
        // Debugging
        std::string GetDebugInfo();
        
        // Accessors
        GpuCore* GetGPU() const;

    private:
        std::unique_ptr<MemoryManager> memory;
        std::unique_ptr<CpuCore> cpu;
        std::unique_ptr<ServiceManager> serviceManager;
        std::unique_ptr<GpuCore> gpu;
        bool isRunning;
        
        bool LoadNSO(const std::vector<uint8_t>& data);
    };

}
