#pragma once
#include <vector>
#include <cstdint>

namespace AIO::Emulator::Switch {

    class MemoryManager;

    class GpuCore {
    public:
        GpuCore(MemoryManager& mem);
        ~GpuCore();

        void Reset();
        void ProcessCommands(uint32_t gpFifoAddress, uint32_t count);
        
        // Basic Framebuffer info
        const std::vector<uint32_t>& GetFramebuffer() const { return framebuffer; }
        int GetWidth() const { return 1280; }
        int GetHeight() const { return 720; }

    private:
        MemoryManager& memory;
        std::vector<uint32_t> framebuffer; // RGBA8888
        
        void ExecuteCommand(uint32_t method, uint32_t argument);
    };

}
