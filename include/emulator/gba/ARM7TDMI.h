#pragma once
#include <cstdint>
#include <vector>

namespace AIO::Emulator::GBA {

    class GBAMemory;

    class ARM7TDMI {
    public:
        explicit ARM7TDMI(GBAMemory& memory);
        ~ARM7TDMI();

        void Reset();
        void Step();

        // Test Helpers
        uint32_t GetRegister(int index) const { return registers[index]; }
        void SetRegister(int index, uint32_t value) { registers[index] = value; }
        uint32_t GetCPSR() const { return cpsr; }
        void SetThumbMode(bool thumb) { thumbMode = thumb; }

    private:
        GBAMemory& memory;

        // Registers
        // R0-R12: General Purpose
        // R13: SP (Stack Pointer)
        // R14: LR (Link Register)
        // R15: PC (Program Counter)
        uint32_t registers[16];
        
        // CPSR: Current Program Status Register
        // CPSR: Saved Program Status Register (banked)
        uint32_t cpsr;
        uint32_t spsr;
        bool thumbMode = false;
        bool halted = false;

        // Pipeline
        // ARM7TDMI has a 3-stage pipeline: Fetch, Decode, Execute
        // We might simulate this or just do direct execution for simplicity initially

        void Fetch();
        void Decode(uint32_t instruction);
        void DecodeThumb(uint16_t instruction);
        void Execute();

        // Instruction Implementations
        void ExecuteBranch(uint32_t instruction);
        void ExecuteBX(uint32_t instruction);
        void ExecuteDataProcessing(uint32_t instruction);
        void ExecuteMultiply(uint32_t instruction);
        void ExecuteMultiplyLong(uint32_t instruction);
        void ExecuteSingleDataTransfer(uint32_t instruction);
        void ExecuteHalfwordDataTransfer(uint32_t instruction);
        void ExecuteBlockDataTransfer(uint32_t instruction);
        void ExecuteSWI(uint32_t comment);
        void ExecuteMRS(uint32_t instruction);
        void ExecuteMSR(uint32_t instruction);

        int logInstructions = 0;

        // Helpers
        void SetZN(uint32_t result);
        bool CheckCondition(uint32_t cond);
        void CheckInterrupts();
        void SwitchMode(uint32_t newMode);

        long long instructionCount = 0;

        // Banked Registers storage
        uint32_t r13_svc, r14_svc, spsr_svc;
        uint32_t r13_irq, r14_irq, spsr_irq;
        uint32_t r13_usr, r14_usr; // User/System share these
        // We can ignore FIQ/Abort/Undef for now as they are rarely used in standard GBA games

        struct IrqContext {
            uint32_t r0, r1, r2, r3, r12, lr, pc, cpsr;
            bool thumbMode;
        };
        std::vector<IrqContext> irqStack;
        static const uint32_t MAGIC_IRQ_RETURN = 0xFFFFFF00;

    };

}
