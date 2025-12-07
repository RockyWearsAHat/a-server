#pragma once
#include <cstdint>
#include <array>
#include <string>

namespace AIO::Emulator::Switch {

    class MemoryManager;

    class CpuCore {
    public:
        CpuCore(MemoryManager& mem);
        ~CpuCore();

        void Reset();
        void Run(int cycles);
        
        // Registers
        uint64_t GetX(int index) const;
        void SetX(int index, uint64_t value);
        uint64_t GetPC() const { return pc; }
        void SetPC(uint64_t value) { pc = value; }
        uint64_t GetSP() const { return sp; }
        void SetSP(uint64_t value) { sp = value; }

        std::string GetStateString();

    private:
        MemoryManager& memory;

        // ARMv8 Registers
        std::array<uint64_t, 31> x; // X0-X30
        uint64_t sp; // Stack Pointer
        uint64_t pc; // Program Counter
        uint32_t pstate; // Processor State

        void ExecuteInstruction(uint32_t instr);
    };

}
