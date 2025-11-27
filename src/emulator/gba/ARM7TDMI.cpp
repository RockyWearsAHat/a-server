#include "emulator/gba/ARM7TDMI.h"
#include "emulator/gba/GBAMemory.h"
#include <cstring>
#include <iostream>
#include <iomanip>
#include <deque>

namespace AIO::Emulator::GBA {

    static std::deque<std::pair<uint32_t, uint32_t>> branchLog;
    static void LogBranch(uint32_t from, uint32_t to) {
        branchLog.push_back({from, to});
        if (branchLog.size() > 50) branchLog.pop_front();

        // Check for bad jump
        bool valid = false;
        if (to >= 0x08000000 && to < 0x0E000000) valid = true;
        else if (to >= 0x03000000 && to < 0x04000000) valid = true; // IWRAM (Mirrored)
        else if (to >= 0x02000000 && to < 0x03000000) valid = true; // WRAM (Mirrored)
        else if (to < 0x4000) valid = true; // BIOS
        else if ((to & 0xFFFFFF00) == 0xFFFFFF00) valid = true; // Magic Return
        
        if (!valid) {
            std::cout << "BAD BRANCH DETECTED: 0x" << std::hex << from << " -> 0x" << to << std::endl;
            std::cout << "Branch History:" << std::endl;
            for (const auto& pair : branchLog) {
                std::cout << "  0x" << pair.first << " -> 0x" << pair.second << std::endl;
            }
        }
    }

    ARM7TDMI::ARM7TDMI(GBAMemory& mem) : memory(mem) {
        Reset();
    }

    ARM7TDMI::~ARM7TDMI() = default;

    void ARM7TDMI::Reset() {
        std::memset(registers, 0, sizeof(registers));
        cpsr = 0x1F; // System Mode
        spsr = 0;
        thumbMode = false;
        halted = false;
        
        // Initialize Banked Registers
        r13_svc = 0x03007FE0;
        r14_svc = 0;
        spsr_svc = 0;
        r13_irq = 0x03007FA0;
        r14_irq = 0;
        spsr_irq = 0;
        r13_usr = 0x03007F00;
        r14_usr = 0;

        // Skip BIOS for now, start at ROM entry
        registers[15] = 0x08000000; 
        registers[13] = r13_usr; // Initialize SP (User/System)
    }

    void ARM7TDMI::SwitchMode(uint32_t newMode) {
        uint32_t oldMode = cpsr & 0x1F;
        if (oldMode == newMode) return;

        // Save current registers to bank
        switch (oldMode) {
            case 0x10: case 0x1F: // User/System
                r13_usr = registers[13];
                r14_usr = registers[14];
                break;
            case 0x12: // IRQ
                r13_irq = registers[13];
                r14_irq = registers[14];
                spsr_irq = spsr;
                break;
            case 0x13: // Supervisor
                r13_svc = registers[13];
                r14_svc = registers[14];
                spsr_svc = spsr;
                break;
        }

        // Load new registers from bank
        switch (newMode) {
            case 0x10: case 0x1F: // User/System
                registers[13] = r13_usr;
                registers[14] = r14_usr;
                break;
            case 0x12: // IRQ
                registers[13] = r13_irq;
                registers[14] = r14_irq;
                spsr = spsr_irq;
                break;
            case 0x13: // Supervisor
                registers[13] = r13_svc;
                registers[14] = r14_svc;
                spsr = spsr_svc;
                break;
        }

        cpsr = (cpsr & ~0x1F) | newMode;
    }

    void ARM7TDMI::CheckInterrupts() {
        // Check IME (Interrupt Master Enable)
        uint16_t ime = memory.Read16(0x04000208);
        
        // Even if IME is 0, we might need to wake up from Halt if an interrupt is pending in IF & IE?
        // GBA Docs: "If the CPU is in Halted state (SWI 2), it is woken up by ANY interrupt enabled in IE, regardless of IME."
        // But to TAKE the interrupt (jump to vector), IME must be 1.
        
        uint16_t ie = memory.Read16(0x04000200);
        uint16_t if_reg = memory.Read16(0x04000202);

        if (halted) {
            if (ie & if_reg) {
                halted = false;
                // std::cout << "CPU Woken from Halt by Interrupt! IE=" << std::hex << ie << " IF=" << if_reg << " CPSR=" << cpsr << std::endl;
            }
        }

        if (!(ime & 1)) return;

        // Check CPSR I bit (IRQ Disable)
        if (cpsr & 0x80) return;

        if (ie & if_reg) {
            // HLE BIOS IRQ Handling (Since we don't have a real BIOS)
            uint16_t active = ie & if_reg;
            
            // std::cout << "IRQ Triggered! Active=0x" << std::hex << active << " PC=0x" << registers[15] << std::endl;

            // 1. Acknowledge Interrupts in IF
            memory.Write16(0x04000202, active);
            
            // 2. Update BIOS Interrupt Flags (0x03007FF8)
            // The game's interrupt dispatcher (at 0x08000100) reads 0x03007FF8 as 32-bit
            // and checks (val & (val >> 16)). So we must set the flag in both halves.
            uint32_t flags = memory.Read32(0x03007FF8);
            flags |= (active | (active << 16));
            memory.Write32(0x03007FF8, flags);
            
            // 3. Call User Interrupt Handler (0x03007FFC)
            uint32_t handler = memory.Read32(0x03007FFC);
            std::cout << "Jumping to IRQ Handler: 0x" << std::hex << handler << " Active=0x" << active << std::endl;

            if (handler != 0 && handler != 0xFFFFFFFF) {
                // Save Context (Simulate BIOS IRQ Wrapper)
                // We must use the REAL Stack (R13_irq) because some games (SMA2)
                // pop registers directly from the stack in their handler.
                
                // 1. Save CPSR to SPSR_irq
                uint32_t oldCpsr = cpsr;
                if (thumbMode) oldCpsr |= 0x20; else oldCpsr &= ~0x20;
                
                // 2. Switch to IRQ Mode
                SwitchMode(0x12);
                spsr = oldCpsr; // SwitchMode loads SPSR from bank, so we overwrite it with current CPSR
                
                // 3. Disable Thumb (IRQ is always ARM)
                thumbMode = false;
                cpsr &= ~0x20;
                cpsr |= 0x80; // Disable IRQ (I bit)
                
                // 4. Calculate Return Address (PC + 4)
                // Note: registers[15] points to current instruction.
                // Hardware sets LR to PC of next instruction + 4.
                // Since we interrupt BEFORE execution, next instruction is current PC.
                uint32_t returnAddr = registers[15] + 4;
                
                // 5. Push R0-R3, R12, LR to R13_irq (Stack)
                // Stack grows down.
                // Push order: LR, R12, R3, R2, R1, R0 (R0 at lowest address)
                registers[13] -= 4 * 6;
                uint32_t sp = registers[13];
                
                memory.Write32(sp + 0, registers[0]);
                memory.Write32(sp + 4, registers[1]);
                memory.Write32(sp + 8, registers[2]);
                memory.Write32(sp + 12, registers[3]);
                memory.Write32(sp + 16, registers[12]);
                memory.Write32(sp + 20, returnAddr);

                // Set LR to Magic Return
                // Note: User Handler expects to return to BIOS, which restores registers.
                // We use a magic address to trap the return.
                registers[14] = MAGIC_IRQ_RETURN;
                
                // Jump to Handler
                if (handler & 1) {
                    thumbMode = true;
                    registers[15] = handler & ~1;
                    cpsr |= 0x20;
                } else {
                    thumbMode = false;
                    registers[15] = handler & ~3;
                    cpsr &= ~0x20;
                }
                // Note: We stay in the current mode (System/User), which is what User Handlers expect
            }
            
            // If no handler is set, we just continue execution. 
            // The flag update above is enough to unblock wait loops.
        }
    }

    void ARM7TDMI::Step() {
        CheckInterrupts();

        // Check for Magic Return from IRQ Handler
        if (registers[15] == MAGIC_IRQ_RETURN || (registers[15] & 0xFFFFFFFE) == (MAGIC_IRQ_RETURN & 0xFFFFFFFE)) {
             // Pop Context from Stack (R13_irq)
             // We are in IRQ Mode (or should be, if we returned to BIOS)
             // But if the game switched mode, we might be in System mode?
             // If we are in System mode, R13 is User Stack.
             // But BIOS runs in IRQ mode.
             // So we should assume we are in IRQ mode.
             // If not, we should switch to IRQ mode to access the stack?
             // No, if the game returned to MAGIC_IRQ_RETURN, it did so via BX LR.
             // LR was set in IRQ mode.
             // So we should be in IRQ mode.
             
             uint32_t sp = registers[13];
             
             registers[0] = memory.Read32(sp + 0);
             registers[1] = memory.Read32(sp + 4);
             registers[2] = memory.Read32(sp + 8);
             registers[3] = memory.Read32(sp + 12);
             registers[12] = memory.Read32(sp + 16);
             uint32_t returnAddr = memory.Read32(sp + 20);
             
             registers[13] += 4 * 6; // Pop
             
             // Restore Mode
             uint32_t oldSpsr = spsr;
             SwitchMode(oldSpsr & 0x1F);
             cpsr = oldSpsr;
             thumbMode = cpsr & 0x20;
             
             // Restore PC
             registers[15] = returnAddr - 4;
             
             std::cout << "Returned from IRQ Handler. Restored PC=0x" << std::hex << registers[15] << std::dec << std::endl;
        }

        if (halted) {
            return;
        }

        static int stepCount = 0;
        stepCount++;
        if (stepCount % 100000 == 0) {
            std::cout << "PC Sample: 0x" << std::hex << registers[15] << " Mode=" << (thumbMode?"T":"A") << std::dec << std::endl;
        }
        
        // Dump state every 60 frames (approx 280896 cycles per frame)
        if (stepCount % 280000 == 0) {
             std::cout << "--- State Dump ---" << std::endl;
             std::cout << "PC=0x" << std::hex << registers[15] << " Mode=" << (thumbMode?"T":"A") << std::dec << std::endl;
             std::cout << "R0=" << std::hex << registers[0] << " R1=" << registers[1] << " R2=" << registers[2] << " R3=" << registers[3] << std::dec << std::endl;
             std::cout << "R12=" << std::hex << registers[12] << " SP=" << registers[13] << " LR=" << registers[14] << std::dec << std::endl;
             std::cout << "Branch History:" << std::endl;
             for (const auto& pair : branchLog) {
                 std::cout << "  0x" << std::hex << pair.first << " -> 0x" << pair.second << std::dec << std::endl;
             }
             std::cout << "------------------" << std::endl;
        }

        // Canonicalize PC if in Mirror (Fixes falling off the end of IWRAM mirrors)
        if (registers[15] >= 0x03008000 && registers[15] < 0x04000000) {
            registers[15] = 0x03000000 | (registers[15] & 0x7FFF);
        }
        if (registers[15] >= 0x02040000 && registers[15] < 0x03000000) {
            registers[15] = 0x02000000 | (registers[15] & 0x3FFFF);
        }

        // Fetch
        uint32_t pc = registers[15];
        memory.debugPC = pc;

        // Debug Tracing for IRQ Handler
        static bool tracing = false;
        static int traceCount = 0;
        
        // Trace around the crash site (SWI 0xC at 0x809dfbc)
        if (pc >= 0x0809dfb0 && pc <= 0x0809dfd0) {
            tracing = true;
            traceCount = 0; // Reset count to keep tracing in this region
        }
        
        // Also trace if we jump to 0x4000 (catch the jump)
        if (pc == 0x4000) {
             std::cout << "PC reached 0x4000! Dumping registers:" << std::endl;
             std::cout << "R0=" << std::hex << registers[0] << " R1=" << registers[1] << " R2=" << registers[2] << " R3=" << registers[3] << std::endl;
             std::cout << "R12=" << registers[12] << " SP=" << registers[13] << " LR=" << registers[14] << " PC=" << registers[15] << std::dec << std::endl;
             std::cout << "Branch History:" << std::endl;
             for (const auto& pair : branchLog) {
                 std::cout << "  0x" << std::hex << pair.first << " -> 0x" << pair.second << std::dec << std::endl;
             }
        }

        if (tracing) {
            std::cout << "Trace: PC=0x" << std::hex << pc << " Mode=" << (thumbMode?"T":"A") << " R0=" << registers[0] << " R1=" << registers[1] << " R2=" << registers[2] << " R3=" << registers[3] << " SP=" << registers[13] << " LR=" << registers[14] << std::dec << std::endl;
            traceCount++;
            if (traceCount > 50) { // Short trace
                tracing = false;
            }
        }

        // Debug: Print PC if it looks suspicious (not in ROM/RAM/BIOS)
        bool valid = false;
        if (pc >= 0x08000000 && pc < 0x0E000000) valid = true; // ROM
        else if (pc >= 0x03000000 && pc < 0x04000000) valid = true; // IWRAM (Mirrored)
        else if (pc >= 0x02000000 && pc < 0x03000000) valid = true; // WRAM (Mirrored)
        else if (pc < 0x4000) valid = true; // BIOS
        
        if (!valid) {
             static int invalidCount = 0;
             if (invalidCount < 10) {
                 std::cerr << "INVALID PC DETECTED: 0x" << std::hex << pc << std::dec << std::endl;
                 std::cerr << "Mode: " << (thumbMode ? "Thumb" : "ARM") << std::endl;
                 std::cerr << "Registers:" << std::endl;
                 for(int i=0; i<16; ++i) std::cerr << "R" << i << "=0x" << std::hex << registers[i] << std::dec << std::endl;
                 std::cerr << "Branch History:" << std::endl;
                 for (const auto& pair : branchLog) {
                     std::cerr << "  0x" << std::hex << pair.first << " -> 0x" << pair.second << std::dec << std::endl;
                 }
                 invalidCount++;
             }
             // Force crash
             halted = true;
             return;
        }

        if (pc == 0x0801dd3e) {
             // Debugging removed
        }

        // Hack: Force Enable Interrupts if stuck in Wait Loop at 0x800052e
        /*
        if (pc == 0x0800052e) {
             static bool forced = false;
             if (!forced) {
                 std::cout << "Detected Wait Loop at 0x800052e. Forcing IME=1, IE=VBlank, CPSR IRQ Enable." << std::endl;
                 memory.Write16(0x04000208, 1); // IME
                 uint16_t ie = memory.Read16(0x04000200);
                 memory.Write16(0x04000200, ie | 1); // IE (VBlank)
                 cpsr &= ~0x80; // Enable IRQ in CPSR
                 forced = true;
             }
        }
        */

        // Debug: Check for Wait Loop at 0x08000F78
        if (pc == 0x08000F78 || pc == 0x08000F78 + 2) {
             // std::cout << "PC=0x08000F78 (Wait Loop?) R0=" << std::hex << registers[0] << " R1=" << registers[1] << " R3=" << registers[3] << std::dec << std::endl;
        }
        
        if (pc == 0x08035778 || pc == 0x08035778 + 2) {
             // std::cout << "PC=0x08035778 R0=" << std::hex << registers[0] << " R1=" << registers[1] << " R2=" << registers[2] << " R5=" << registers[5] << " R7=" << registers[7] << std::dec << std::endl;
        }

        if (pc == 0x080015d8) {
             // std::cout << "PC reached Patch Start: 0x080015d8" << std::endl;
        }
        if (pc == 0x08000000) {
             // std::cout << "PC at ROM Entry: 0x08000000" << std::endl;
        }

        if (thumbMode) {
            uint16_t instruction = memory.Read16(pc);
            
            static int iwram_trace_count = 0;
            if (pc >= 0x03000000 && pc < 0x03008000 && iwram_trace_count < 500) {
                std::cout << "IWRAM PC: 0x" << std::hex << pc << " Op: 0x" << instruction << " Mode: T" << std::dec << std::endl;
                iwram_trace_count++;
            }

            registers[15] += 2;
            DecodeThumb(instruction);
        } else {
            uint32_t instruction = memory.Read32(pc);

            static int iwram_trace_count = 0;
            if (pc >= 0x03000000 && pc < 0x03008000 && iwram_trace_count < 500) {
                std::cout << "IWRAM PC: 0x" << std::hex << pc << " Op: 0x" << instruction << " Mode: A" << std::dec << std::endl;
                iwram_trace_count++;
            }

            registers[15] += 4;
            Decode(instruction);
        }
    }

    void ARM7TDMI::Fetch() {
        // Pipeline stage implementation
    }

    void ARM7TDMI::Decode(uint32_t instruction) {
        // Check Condition Code (Bits 31-28)
        uint32_t cond = (instruction >> 28) & 0xF;
        
        if (cond != 0xE) { // Optimization: AL (Always) is most common
            if (!CheckCondition(cond)) {
                return; // Condition failed, instruction acts as NOP
            }
        }

        // Identify Instruction Type
        // Branch and Exchange (BX): xxxx 0001 0010 xxxx xxxx xxxx 0001 xxxx
        if ((instruction & 0x0FFFFFF0) == 0x012FFF10) {
            ExecuteBX(instruction);
        }
        // Branch: xxxx 101x xxxx xxxx xxxx xxxx xxxx xxxx
        else if ((instruction & 0x0E000000) == 0x0A000000) {
            ExecuteBranch(instruction);
        } 
        // Multiply: xxxx 0000 00xx xxxx xxxx 1001 xxxx
        else if ((instruction & 0x0FC000F0) == 0x00000090) {
            ExecuteMultiply(instruction);
        }
        // Multiply Long: xxxx 0000 1xxx xxxx xxxx 1001 xxxx
        else if ((instruction & 0x0F8000F0) == 0x00800090) {
            ExecuteMultiplyLong(instruction);
        }
        // Halfword Data Transfer: xxxx 000x xxxx xxxx xxxx 1011 xxxx
        // Note: Bit 22 (I/Immediate) distinguishes from Multiply? No.
        // Mul: 0000 00AS ... 1001 ...
        // Halfword: 000P U1WI ... 1SH1 ...
        // Halfword Register: 000P U0WI ... 1SH1 ...
        // Common: 000x ... 1xx1 ...
        // Mul has bits 7-4 as 1001. Halfword has 1011 or 1101 or 1111.
        // So checking (instruction & 0x0E000090) == 0x00000090 covers both?
        // No, 0x90 is 1001 0000.
        // Mul: 1001 (9). Halfword: 1011 (B), 1101 (D), 1111 (F).
        // My Halfword check was: (instruction & 0x0E000090) == 0x00000090.
        // This matches 9, B, D, F.
        // So it matches Multiply too!
        // But Multiply is checked before.
        // So if we move this check here, it will catch Halfword (B, D, F) and ignore Multiply (9) because Multiply is already handled?
        // Wait, if I move it here, I must ensure it doesn't match Data Processing that happens to have bits 7-4 as 1xx1.
        // Data Processing: 00xx ...
        // If Opcode makes it look like Halfword?
        // SWP is also here.
        // Let's use a stricter check for Halfword.
        // Bits 7 and 4 must be 1.
        // Bit 6 (S) and 5 (H) cannot be both 0 (SWP).
        // So (instruction & 0x60) != 0.
        else if ((instruction & 0x0E000090) == 0x00000090 && (instruction & 0x60) != 0) {
            ExecuteHalfwordDataTransfer(instruction);
        }
        // MRS: xxxx 0001 0x00 1111 xxxx 0000 0000 0000
        else if ((instruction & 0x0FBF0FFF) == 0x010F0000) {
            ExecuteMRS(instruction);
        }
        // MSR (Register): xxxx 0001 0x10 xxxx 1111 0000 0000 xxxx
        else if ((instruction & 0x0FB0FFF0) == 0x0120F000) {
            ExecuteMSR(instruction);
        }
        // MSR (Immediate): xxxx 0011 0x10 xxxx 1111 xxxx xxxx
        else if ((instruction & 0x0FB0F000) == 0x0320F000) {
            ExecuteMSR(instruction);
        }
        // Data Processing: xxxx 00xx xxxx xxxx xxxx xxxx xxxx xxxx
        // Exclude Multiply (xxxx 0000 00xx xxxx xxxx 1001 xxxx) and Halfword Transfer (xxxx 000x xxxx xxxx xxxx 1011 xxxx)
        else if ((instruction & 0x0C000000) == 0x00000000) {
             // Simple check for now, assuming valid Data Proc if not Mul/Halfword
             // TODO: Add stricter checks for Multiply and Halfword Transfer
             ExecuteDataProcessing(instruction);
        }
        // Single Data Transfer: xxxx 01xx xxxx xxxx xxxx xxxx xxxx xxxx
        else if ((instruction & 0x0C000000) == 0x04000000) {
            ExecuteSingleDataTransfer(instruction);
        }
        // Block Data Transfer: xxxx 100x xxxx xxxx xxxx xxxx xxxx xxxx
        else if ((instruction & 0x0E000000) == 0x08000000) {
            ExecuteBlockDataTransfer(instruction);
        }
        // Software Interrupt: xxxx 1111 xxxx xxxx xxxx xxxx xxxx xxxx
        else if ((instruction & 0x0F000000) == 0x0F000000) {
            ExecuteSWI((instruction >> 16) & 0xFF);
        }
        else {
            std::cout << "Unknown Instruction: 0x" << std::hex << instruction << " at PC=" << (registers[15]-4) << " Mode=" << (thumbMode ? "Thumb" : "ARM") << std::endl;
        }
    }

    void ARM7TDMI::Execute() {
        // Pipeline stage implementation
    }

    void ARM7TDMI::ExecuteBranch(uint32_t instruction) {
        // Format: Cond | 101 | L | Offset
        bool link = (instruction >> 24) & 1;
        int32_t offset = instruction & 0xFFFFFF;

        // Sign extend 24-bit offset to 32-bit
        if (offset & 0x800000) {
            offset |= 0xFF000000;
        }

        // Shift left by 2 (word aligned)
        offset <<= 2;

        // PC is already +4 from fetch, but pipeline behavior means PC is effectively +8 during execution
        // So target = (PC + 4) + offset
        // Wait, in Step() we did registers[15] += 4.
        // The PC value used for calculation should be the address of the current instruction + 8.
        // Current PC in registers[15] is (InstructionAddr + 4).
        // So we need to add another 4 to simulate the pipeline effect for the calculation.
        
        uint32_t currentPC = registers[15]; 
        
        if (link) {
            registers[14] = currentPC; // LR = PC - 4 (instruction following branch)
        }

        uint32_t target = currentPC + 4 + offset;
        LogBranch(currentPC - 4, target); // Log from actual instruction address
        registers[15] = target;
        
        // Flush pipeline (if we were simulating it)
        // std::cout << "Branch Taken! New PC: 0x" << std::hex << registers[15] << std::endl;
    }

    void ARM7TDMI::ExecuteDataProcessing(uint32_t instruction) {
        bool I = (instruction >> 25) & 1;
        uint32_t opcode = (instruction >> 21) & 0xF;
        bool S = (instruction >> 20) & 1;
        uint32_t rn = (instruction >> 16) & 0xF;
        uint32_t rd = (instruction >> 12) & 0xF;
        uint32_t op2 = 0;

        // Calculate Operand 2
        if (I) {
            // Immediate
            uint32_t rotate = (instruction >> 8) & 0xF;
            uint32_t imm = instruction & 0xFF;
            // Rotate right by 2 * rotate
            uint32_t shift = rotate * 2;
            op2 = (imm >> shift) | (imm << (32 - shift));
        } else {
            // Register
            // TODO: Implement Barrel Shifter for Register operands
            uint32_t rm = instruction & 0xF;
            op2 = registers[rm];
        }

        uint32_t result = 0;
        uint32_t rnVal = registers[rn];
        bool carry = (cpsr >> 29) & 1;
        bool overflow = (cpsr >> 28) & 1;
        bool cOut = carry; // Default to current carry (for logical ops)

        // For logical ops with shift, cOut should be the shifter carry out.
        // TODO: Implement shifter carry out.

        switch (opcode) {
            case 0x0: // AND
                result = rnVal & op2;
                break;
            case 0x1: // EOR
                result = rnVal ^ op2;
                break;
            case 0x2: // SUB
            case 0xA: // CMP
                result = rnVal - op2;
                cOut = (rnVal >= op2); // No Borrow
                {
                    bool sign1 = (rnVal >> 31) & 1;
                    bool sign2 = (op2 >> 31) & 1;
                    bool signR = (result >> 31) & 1;
                    overflow = (sign1 != sign2 && sign1 != signR);
                }
                break;
            case 0x3: // RSB
                result = op2 - rnVal;
                cOut = (op2 >= rnVal); // No Borrow
                {
                    bool sign1 = (op2 >> 31) & 1;
                    bool sign2 = (rnVal >> 31) & 1;
                    bool signR = (result >> 31) & 1;
                    overflow = (sign1 != sign2 && sign1 != signR);
                }
                break;
            case 0x4: // ADD
            case 0xB: // CMN
                result = rnVal + op2;
                cOut = (result < rnVal); // Carry
                {
                    bool sign1 = (rnVal >> 31) & 1;
                    bool sign2 = (op2 >> 31) & 1;
                    bool signR = (result >> 31) & 1;
                    overflow = (sign1 == sign2 && sign1 != signR);
                }
                break;
            case 0x5: // ADC
                result = rnVal + op2 + carry;
                cOut = (rnVal + op2 < rnVal) || (rnVal + op2 + carry < rnVal + op2); // Carry if either addition overflows
                {
                    bool sign1 = (rnVal >> 31) & 1;
                    bool sign2 = (op2 >> 31) & 1;
                    bool signR = (result >> 31) & 1;
                    overflow = (sign1 == sign2 && sign1 != signR);
                }
                break;
            case 0x6: // SBC
                result = rnVal - op2 - !carry;
                cOut = (rnVal >= (uint64_t)op2 + !carry); // No Borrow
                {
                    bool sign1 = (rnVal >> 31) & 1;
                    bool sign2 = (op2 >> 31) & 1;
                    bool signR = (result >> 31) & 1;
                    overflow = (sign1 != sign2 && sign1 != signR);
                }
                break;
            case 0x7: // RSC
                result = op2 - rnVal - !carry;
                cOut = (op2 >= (uint64_t)rnVal + !carry); // No Borrow
                {
                    bool sign1 = (op2 >> 31) & 1;
                    bool sign2 = (rnVal >> 31) & 1;
                    bool signR = (result >> 31) & 1;
                    overflow = (sign1 != sign2 && sign1 != signR);
                }
                break;
            case 0x8: // TST
                result = rnVal & op2;
                break;
            case 0x9: // TEQ
                result = rnVal ^ op2;
                break;
            case 0xC: // ORR
                result = rnVal | op2;
                break;
            case 0xD: // MOV
                result = op2;
                break;
            case 0xE: // BIC
                result = rnVal & (~op2);
                break;
            case 0xF: // MVN
                result = ~op2;
                break;
        }

        // Write back result if not a test instruction
        if (opcode != 0x8 && opcode != 0x9 && opcode != 0xA && opcode != 0xB) {
            if (rd == 15) {
                LogBranch(registers[15] - 4, result);
            }
            registers[rd] = result;
        }

        // Update Flags (CPSR) if S is set
        if (S) {
            if (rd == 15) {
                // If Rd is PC, restore CPSR from SPSR
                cpsr = spsr;
            } else {
                SetZN(result);
                // Update C and V
                if (cOut) cpsr |= 0x20000000; else cpsr &= ~0x20000000;
                if (overflow) cpsr |= 0x10000000; else cpsr &= ~0x10000000;
            }
        }
    }

    void ARM7TDMI::ExecuteSingleDataTransfer(uint32_t instruction) {
        bool I = (instruction >> 25) & 1;
        bool P = (instruction >> 24) & 1;
        bool U = (instruction >> 23) & 1;
        bool B = (instruction >> 22) & 1;
        bool W = (instruction >> 21) & 1;
        bool L = (instruction >> 20) & 1;
        uint32_t rn = (instruction >> 16) & 0xF;
        uint32_t rd = (instruction >> 12) & 0xF;
        uint32_t offset = 0;

        if (!I) {
            // Immediate Offset
            offset = instruction & 0xFFF;
        } else {
            // Register Offset
            // TODO: Implement Shifted Register Offset
            uint32_t rm = instruction & 0xF;
            offset = registers[rm];
        }

        uint32_t baseAddr = registers[rn];
        if (rn == 15) {
            // PC is 8 bytes ahead
            baseAddr += 4; 
        }

        uint32_t targetAddr = baseAddr;
        if (P) {
            // Pre-indexing
            if (U) targetAddr += offset;
            else   targetAddr -= offset;
        }

        if (L) {
            // Load
            if (B) {
                uint8_t val = memory.Read8(targetAddr);
                registers[rd] = val;
                // std::cout << "LDRB: Rd=R" << rd << " Addr=0x" << std::hex << targetAddr << " Val=0x" << (uint32_t)val << std::endl;
            } else {
                uint32_t val = memory.Read32(targetAddr);
                // Rotate if unaligned (TODO)
                if (rd == 15) {
                    LogBranch(registers[15] - 4, val);
                }
                registers[rd] = val;
                // std::cout << "LDR: Rd=R" << rd << " Addr=0x" << std::hex << targetAddr << " Val=0x" << val << std::endl;
            }
        } else {
            // Store
            uint32_t val = registers[rd];
            if (rd == 15) val += 4; // Storing PC stores PC+12 (Instr+8+4?? Check docs) - usually PC+12

            if (B) {
                memory.Write8(targetAddr, val & 0xFF);
                // std::cout << "STRB: Rd=R" << rd << " Addr=0x" << std::hex << targetAddr << " Val=0x" << (val & 0xFF) << std::endl;
            } else {
                memory.Write32(targetAddr, val);
                // std::cout << "STR: Rd=R" << rd << " Addr=0x" << std::hex << targetAddr << " Val=0x" << val << std::endl;
            }
        }

        if (!P || W) {
            // Write-back
            // Post-indexing always writes back
            // Pre-indexing writes back if W bit is set
            uint32_t newBase = baseAddr;
            if (U) newBase += offset;
            else   newBase -= offset;
            registers[rn] = newBase;
        }
    }

    void ARM7TDMI::ExecuteBlockDataTransfer(uint32_t instruction) {
        bool P = (instruction >> 24) & 1;
        bool U = (instruction >> 23) & 1;
        bool S = (instruction >> 22) & 1;
        bool W = (instruction >> 21) & 1;
        bool L = (instruction >> 20) & 1;
        uint32_t rn = (instruction >> 16) & 0xF;
        uint16_t regList = instruction & 0xFFFF;

        uint32_t address = registers[rn];
        uint32_t startAddress = address;
        
        // Count set bits
        int numRegs = 0;
        for(int i=0; i<16; ++i) {
            if((regList >> i) & 1) numRegs++;
        }

        // Calculate start address based on addressing mode
        // P=0, U=1: Increment After (IA) - Start at Rn
        // P=1, U=1: Increment Before (IB) - Start at Rn + 4
        // P=0, U=0: Decrement After (DA) - Start at Rn - (n * 4) + 4
        // P=1, U=0: Decrement Before (DB) - Start at Rn - (n * 4)

        if (!U) {
            // Decrement
            address -= (numRegs * 4);
            if (!P) address += 4; // DA
        } else {
            // Increment
            if (P) address += 4; // IB
        }

        uint32_t currentAddr = address;
        bool writeBack = W;

        for (int i = 0; i < 16; ++i) {
            if ((regList >> i) & 1) {
                if (L) {
                    // Load (LDM)
                    uint32_t val = memory.Read32(currentAddr);
                    if (i == 15) {
                        LogBranch(registers[15] - 4, val);
                    }
                    registers[i] = val;
                    // std::cout << "LDM: R" << i << " <- [0x" << std::hex << currentAddr << "] (0x" << registers[i] << ")" << std::endl;
                } else {
                    // Store (STM)
                    uint32_t val = registers[i];
                    if (i == 15) val += 4; // PC store quirk? Usually PC+12
                    memory.Write32(currentAddr, val);
                    // std::cout << "STM: [0x" << std::hex << currentAddr << "] <- R" << i << " (0x" << val << ")" << std::endl;
                }
                currentAddr += 4;
            }
        }

        // Writeback
        if (writeBack) {
            if (U) registers[rn] = startAddress + (numRegs * 4);
            else   registers[rn] = startAddress - (numRegs * 4);
        }
        
        // TODO: Handle S bit (User mode registers or CPSR restore)
    }

    void ARM7TDMI::ExecuteHalfwordDataTransfer(uint32_t instruction) {
        bool P = (instruction >> 24) & 1;
        bool U = (instruction >> 23) & 1;
        bool I = (instruction >> 22) & 1;
        bool W = (instruction >> 21) & 1;
        bool L = (instruction >> 20) & 1;
        uint32_t rn = (instruction >> 16) & 0xF;
        uint32_t rd = (instruction >> 12) & 0xF;
        uint32_t offset = 0;
        uint32_t opcode = (instruction >> 5) & 0x3; // H, SB, SH

        if (I) {
            // Immediate Offset
            offset = ((instruction >> 4) & 0xF0) | (instruction & 0xF);
        } else {
            // Register Offset
            uint32_t rm = instruction & 0xF;
            offset = registers[rm];
        }

        uint32_t baseAddr = registers[rn];
        if (rn == 15) baseAddr += 8; // PC+8

        uint32_t targetAddr = baseAddr;
        if (P) {
            if (U) targetAddr += offset;
            else   targetAddr -= offset;
        }

        if (L) {
            // Load
            if (opcode == 1) { // LDRH (Unsigned Halfword)
                registers[rd] = memory.Read16(targetAddr);
                // std::cout << "LDRH: Rd=R" << rd << " Addr=0x" << std::hex << targetAddr << " Val=0x" << registers[rd] << std::endl;
            } else if (opcode == 2) { // LDRSB (Signed Byte)
                int8_t val = (int8_t)memory.Read8(targetAddr);
                registers[rd] = (int32_t)val;
                // std::cout << "LDRSB: Rd=R" << rd << " Addr=0x" << std::hex << targetAddr << " Val=0x" << registers[rd] << std::endl;
            } else if (opcode == 3) { // LDRSH (Signed Halfword)
                int16_t val = (int16_t)memory.Read16(targetAddr);
                registers[rd] = (int32_t)val;
                // std::cout << "LDRSH: Rd=R" << rd << " Addr=0x" << std::hex << targetAddr << " Val=0x" << registers[rd] << std::endl;
            }
        } else {
            // Store
            if (opcode == 1) { // STRH
                memory.Write16(targetAddr, registers[rd] & 0xFFFF);
                // std::cout << "STRH: Rd=R" << rd << " Addr=0x" << std::hex << targetAddr << " Val=0x" << (registers[rd] & 0xFFFF) << std::endl;
            }
        }

        if (!P || W) {
            if (U) registers[rn] = baseAddr + offset;
            else   registers[rn] = baseAddr - offset;
        }
    }

    void ARM7TDMI::ExecuteBX(uint32_t instruction) {
        uint32_t rm = instruction & 0xF;
        uint32_t target = registers[rm];
        
        if (target >= 0x03000000 && target < 0x04000000) {
             std::cout << "BX to IWRAM detected! Rm=R" << rm << " Val=0x" << std::hex << target << std::endl;
             /*
             if ((target & 1) == 0) {
                 std::cout << "Hack: Forcing Thumb mode for even IWRAM target" << std::endl;
                 target |= 1;
             }
             */
        }

        LogBranch(registers[15] - 4, target);

        if (target & 1) {
            thumbMode = true;
            registers[15] = target & 0xFFFFFFFE;
            // std::cout << "BX to Thumb: 0x" << std::hex << registers[15] << std::endl;
        } else {
            thumbMode = false;
            registers[15] = target & 0xFFFFFFFC; // Align to 4 bytes
            // std::cout << "BX to ARM: 0x" << std::hex << registers[15] << std::endl;
        }
    }

    void ARM7TDMI::ExecuteSWI(uint32_t comment) {
        std::cout << "SWI 0x" << std::hex << comment << " at PC=" << (thumbMode ? registers[15]-2 : registers[15]-4) << std::endl;
        
        if (comment == 0x00) {
             std::cout << "SoftReset (SWI 0) called!" << std::endl;
        }

        switch (comment) {
            case 0x00: // SoftReset
                // TODO: Implement SoftReset (Clear registers, jump to 0x8000000, etc.)
                // For now, just jump to ROM start
                registers[15] = 0x08000000;
                registers[13] = 0x03007F00; // Reset SP
                break;
            case 0x01: // RegisterRamReset
                std::cout << "RegisterRamReset: Flags=0x" << std::hex << registers[0] << std::endl;
                // TODO: Clear RAM regions based on flags
                break;
            case 0x02: // Halt
                halted = true;
                break;
            case 0x04: // IntrWait
                std::cout << "SWI 0x04 (IntrWait) R0=" << registers[0] << " R1=" << registers[1] << std::endl;
                // Enable IME
                memory.Write16(0x04000208, 1);
                // Enable requested interrupts in IE
                {
                    uint16_t ie = memory.Read16(0x04000200);
                    memory.Write16(0x04000200, ie | (registers[1] & 0xFFFF));
                }
                // Force Enable IRQs in CPSR (Hack for HLE)
                cpsr &= ~0x80;
                halted = true;
                break;
            case 0x05: // VBlankIntrWait
                std::cout << "SWI 0x05 (VBlankIntrWait) R0=" << registers[0] << " R1=" << registers[1] << std::endl;
                // Enable IME
                memory.Write16(0x04000208, 1);
                // Enable VBlank IRQ in IE (Bit 0)
                {
                    uint16_t ie = memory.Read16(0x04000200);
                    memory.Write16(0x04000200, ie | 1);
                }
                // Enable VBlank IRQ in DISPSTAT (Bit 3)
                {
                    uint16_t dispstat = memory.Read16(0x04000004);
                    memory.Write16(0x04000004, dispstat | 0x0008);
                }
                // Force Enable IRQs in CPSR (Hack for HLE)
                cpsr &= ~0x80;
                halted = true;
                break;
            case 0x0B: // CpuSet (R0=Src, R1=Dst, R2=Cnt/Ctrl)
            {
                uint32_t src = registers[0];
                uint32_t dst = registers[1];
                uint32_t len = registers[2] & 0x1FFFFF;
                bool fixedSrc = (registers[2] >> 24) & 1;
                bool is32Bit = (registers[2] >> 26) & 1;
                
                std::cout << "CpuSet: Src=" << std::hex << src << " Dst=" << dst << " Len=" << len << " 32Bit=" << is32Bit << " Fixed=" << fixedSrc << std::endl;

                if (is32Bit) {
                    for (uint32_t i = 0; i < len; ++i) {
                        uint32_t val = memory.Read32(src);
                        memory.Write32(dst, val);
                        dst += 4;
                        if (!fixedSrc) src += 4;
                    }
                } else {
                    for (uint32_t i = 0; i < len; ++i) {
                        uint16_t val = memory.Read16(src);
                        memory.Write16(dst, val);
                        dst += 2;
                        if (!fixedSrc) src += 2;
                    }
                }
                break;
            }
            case 0x0C: // CpuFastSet (R0=Src, R1=Dst, R2=Cnt/Ctrl)
            {
                uint32_t src = registers[0];
                uint32_t dst = registers[1];
                uint32_t len = registers[2] & 0x1FFFFF;
                
                std::cout << "CpuFastSet: Src=" << std::hex << src << " Dst=" << dst << " Len=" << len << " Fixed=" << ((registers[2] >> 24) & 1) << std::endl;

                // len is the word count (must be multiple of 8)
                bool fixedSrc = (registers[2] >> 24) & 1;
                
                // Always 32-bit
                for (uint32_t i = 0; i < len; ++i) {
                    uint32_t val = memory.Read32(src);
                    memory.Write32(dst, val);
                    dst += 4;
                    if (!fixedSrc) src += 4;
                }
                break;
            }
            default:
                std::cout << "Unimplemented SWI 0x" << std::hex << comment << " at PC=" << registers[15] << std::endl;
                break;
        }
    }

    void ARM7TDMI::DecodeThumb(uint16_t instruction) {
        // Thumb Instruction Decoding
        
        /*
        if (registers[15] >= 0x08005520 && registers[15] <= 0x08005530) {
             std::cout << "Thumb Decode at PC=" << std::hex << (registers[15]-2) << " Instr=" << instruction << std::endl;
        }
        */

        // Format 2: Add/Subtract
        // 0001 1xxx xxxx xxxx
        if ((instruction & 0xF800) == 0x1800) {
            bool I = (instruction >> 10) & 1;
            bool sub = (instruction >> 9) & 1;
            uint32_t rn = (instruction >> 6) & 0x7; // If I=0, Rn. If I=1, Imm3
            uint32_t rs = (instruction >> 3) & 0x7;
            uint32_t rd = instruction & 0x7;
            
            uint32_t op2 = I ? rn : registers[rn];
            uint32_t val = registers[rs];
            uint32_t res = 0;
            
            if (sub) {
                res = val - op2;
                SetZN(res);
                // Set C if NOT borrow (val >= op2)
                if (val >= op2) cpsr |= 0x20000000; else cpsr &= ~0x20000000;
                // Set V if overflow
                bool sign1 = (val >> 31) & 1;
                bool sign2 = (op2 >> 31) & 1;
                bool signR = (res >> 31) & 1;
                if (sign1 != sign2 && sign1 != signR) cpsr |= 0x10000000; else cpsr &= ~0x10000000;
            } else {
                res = val + op2;
                SetZN(res);
                // Set C if carry (overflow for unsigned)
                if (res < val) cpsr |= 0x20000000; else cpsr &= ~0x20000000;
                // Set V if overflow
                bool sign1 = (val >> 31) & 1;
                bool sign2 = (op2 >> 31) & 1;
                bool signR = (res >> 31) & 1;
                if (sign1 == sign2 && sign1 != signR) cpsr |= 0x10000000; else cpsr &= ~0x10000000;
            }
            
            registers[rd] = res;

            if (registers[15] >= 0x080015d8 && registers[15] <= 0x08001610) {
                 // std::cout << "ADD/SUB: Rd=R" << rd << " Val=0x" << std::hex << res << std::endl;
            }
        }
        // Format 1: Move Shifted Register
        // 000x xxxx xxxx xxxx
        else if ((instruction & 0xE000) == 0x0000) {
            uint32_t opcode = (instruction >> 11) & 0x3;
            uint32_t offset = (instruction >> 6) & 0x1F;
            uint32_t rs = (instruction >> 3) & 0x7;
            uint32_t rd = instruction & 0x7;
            
            uint32_t val = registers[rs];
            uint32_t res = 0;
            bool carry = (cpsr >> 29) & 1;

            if (opcode == 0) { // LSL
                if (offset != 0) {
                    carry = (val >> (32 - offset)) & 1;
                    res = val << offset;
                } else {
                    res = val; // LSL #0 is MOV
                }
            } else if (opcode == 1) { // LSR
                if (offset == 0) offset = 32;
                if (offset == 32) {
                    carry = (val >> 31) & 1;
                    res = 0;
                } else {
                    carry = (val >> (offset - 1)) & 1;
                    res = val >> offset;
                }
            } else if (opcode == 2) { // ASR
                if (offset == 0) offset = 32;
                if (offset >= 32) {
                    carry = (val >> 31) & 1;
                    res = ((int32_t)val) >> 31; // Fill with sign bit
                } else {
                    carry = (val >> (offset - 1)) & 1;
                    res = (int32_t)val >> offset;
                }
            }
            
            SetZN(res);
            if (carry) cpsr |= 0x20000000; else cpsr &= ~0x20000000;
            registers[rd] = res;

            if (registers[15] >= 0x080015d8 && registers[15] <= 0x08001610) {
                 // std::cout << "Shift: Rd=R" << rd << " Val=0x" << std::hex << res << std::endl;
            }
        }
        // Format 3: Move/Compare/Add/Subtract Immediate
        // 001x xxxx xxxx xxxx
        else if ((instruction & 0xE000) == 0x2000) {
            uint32_t opcode = (instruction >> 11) & 0x3;
            uint32_t rd = (instruction >> 8) & 0x7;
            uint32_t imm = instruction & 0xFF;
            
            if (opcode == 0) { // MOV Rd, #Offset8
                registers[rd] = imm;
                SetZN(registers[rd]);
            } else if (opcode == 1) { // CMP Rd, #Offset8
                uint32_t result = registers[rd] - imm;
                SetZN(result);
                // TODO: Set C and V
                if (registers[rd] >= imm) cpsr |= 0x20000000; else cpsr &= ~0x20000000; // Simple C
            } else if (opcode == 2) { // ADD Rd, #Offset8
                registers[rd] += imm;
                SetZN(registers[rd]);
                // TODO: Set C and V
            } else if (opcode == 3) { // SUB Rd, #Offset8
                registers[rd] -= imm;
                SetZN(registers[rd]);
                // TODO: Set C and V
            }
        }
        // Format 4: ALU Operations
        // 0100 00xx xxxx xxxx
        else if ((instruction & 0xFC00) == 0x4000) {
            uint32_t opcode = (instruction >> 6) & 0xF;
            uint32_t rs = (instruction >> 3) & 0x7;
            uint32_t rd = instruction & 0x7;
            
            uint32_t val = registers[rd];
            uint32_t op2 = registers[rs];
            uint32_t res = 0;
            
            switch (opcode) {
                case 0x0: // AND
                    res = val & op2;
                    SetZN(res);
                    registers[rd] = res;
                    break;
                case 0x1: // EOR
                    res = val ^ op2;
                    SetZN(res);
                    registers[rd] = res;
                    break;
                case 0x2: // LSL
                    op2 &= 0xFF;
                    if (op2 >= 32) res = 0; // TODO: Carry
                    else res = val << op2;
                    SetZN(res);
                    registers[rd] = res;
                    break;
                case 0x3: // LSR
                    op2 &= 0xFF;
                    if (op2 >= 32) res = 0; // TODO: Carry
                    else res = val >> op2;
                    SetZN(res);
                    registers[rd] = res;
                    break;
                case 0x4: // ASR
                    op2 &= 0xFF;
                    if (op2 >= 32) res = ((int32_t)val) >> 31; // TODO: Carry
                    else res = (int32_t)val >> op2;
                    SetZN(res);
                    registers[rd] = res;
                    break;
                case 0x5: // ADC
                    // TODO: Carry
                    res = val + op2; 
                    SetZN(res);
                    registers[rd] = res;
                    break;
                case 0x6: // SBC
                    // TODO: Carry
                    res = val - op2; 
                    SetZN(res);
                    registers[rd] = res;
                    break;
                case 0x7: // ROR
                    op2 &= 0xFF;
                    op2 &= 0x1F;
                    if (op2 == 0) res = val;
                    else res = (val >> op2) | (val << (32 - op2));
                    SetZN(res);
                    registers[rd] = res;
                    break;
                case 0x8: // TST
                    res = val & op2;
                    SetZN(res);
                    break;
                case 0x9: // NEG (RSB Rd, Rs, #0)
                    res = 0 - op2;
                    SetZN(res);
                    registers[rd] = res;
                    break;
                case 0xA: // CMP
                    res = val - op2;
                    SetZN(res);
                    if (val >= op2) cpsr |= 0x20000000; else cpsr &= ~0x20000000;
                    {
                        bool sign1 = (val >> 31) & 1;
                        bool sign2 = (op2 >> 31) & 1;
                        bool signR = (res >> 31) & 1;
                        if (sign1 != sign2 && sign1 != signR) cpsr |= 0x10000000; else cpsr &= ~0x10000000;
                    }
                    break;
                case 0xB: // CMN
                    res = val + op2;
                    SetZN(res);
                    break;
                case 0xC: // ORR
                    res = val | op2;
                    SetZN(res);
                    registers[rd] = res;
                    break;
                case 0xD: // MUL
                    res = val * op2;
                    SetZN(res);
                    registers[rd] = res;
                    break;
                case 0xE: // BIC
                    res = val & (~op2);
                    SetZN(res);
                    registers[rd] = res;
                    break;
                case 0xF: // MVN
                    res = ~op2;
                    SetZN(res);
                    registers[rd] = res;
                    break;
            }
        }
        // Format 5: HiReg Operations / BX
        // 0100 01xx xxxx xxxx
        else if ((instruction & 0xFC00) == 0x4400) {
            uint32_t opcode = (instruction >> 8) & 0x3;
            bool h1 = (instruction >> 7) & 1;
            bool h2 = (instruction >> 6) & 1;
            uint32_t rm = (instruction >> 3) & 0x7;
            uint32_t rd = instruction & 0x7;
            
            uint32_t regD = rd | (h1 << 3);
            uint32_t regM = rm | (h2 << 3);
            
            if (opcode == 0) { // ADD Rd, Rm
                if (regD == 15) {
                     LogBranch(registers[15] - 2, registers[15] + registers[regM]);
                }
                registers[regD] += registers[regM];
                // Format 5 ADD does NOT set flags (except if Rd=PC? No)
            } else if (opcode == 1) { // CMP Rd, Rm
                uint32_t val = registers[regD];
                uint32_t op2 = registers[regM];
                uint32_t res = val - op2;
                SetZN(res);
                // Set C if NOT borrow (val >= op2)
                if (val >= op2) cpsr |= 0x20000000; else cpsr &= ~0x20000000;
                // Set V
                bool sign1 = (val >> 31) & 1;
                bool sign2 = (op2 >> 31) & 1;
                bool signR = (res >> 31) & 1;
                if (sign1 != sign2 && sign1 != signR) cpsr |= 0x10000000; else cpsr &= ~0x10000000;
            } else if (opcode == 2) { // MOV Rd, Rm
                if (regD == 15) {
                     LogBranch(registers[15] - 2, registers[regM]);
                }
                registers[regD] = registers[regM];
            } else if (opcode == 3) { // BX Rm
                uint32_t target = registers[regM];
                
                /*
                if (target >= 0x03000000 && target < 0x04000000 && (target & 1) == 0) {
                     // Hack: Force Thumb mode for even IWRAM target (Fixes SMA2 crash)
                     target |= 1;
                }
                */

                LogBranch(registers[15] - 2, target);
                if (target & 1) {
                    thumbMode = true;
                    registers[15] = target & 0xFFFFFFFE;
                } else {
                    thumbMode = false;
                    registers[15] = target & 0xFFFFFFFC;
                }
            }
        }
        // Format 6: PC-relative Load
        // 0100 1xxx xxxx xxxx
        else if ((instruction & 0xF800) == 0x4800) {
            uint32_t rd = (instruction >> 8) & 0x7;
            uint32_t imm = instruction & 0xFF;
            uint32_t addr = ((registers[15] + 2) & 0xFFFFFFFC) + (imm * 4); // PC is +4 in Thumb
            registers[rd] = memory.Read32(addr);
            
            if (registers[15] >= 0x080015d8 && registers[15] <= 0x08001610) {
                 // std::cout << "LDR PC-Rel: Rd=R" << rd << " Addr=0x" << std::hex << addr << " Val=0x" << registers[rd] << std::endl;
            }
        }
        // Format 8: Load/Store Sign-Extended Byte/Halfword
        // 0101 001x xxxx xxxx
        else if ((instruction & 0xF200) == 0x5200) {
            bool H = (instruction >> 11) & 1;
            bool S = (instruction >> 10) & 1; // Sign extended
            uint32_t ro = (instruction >> 6) & 0x7;
            uint32_t rb = (instruction >> 3) & 0x7;
            uint32_t rd = instruction & 0x7;
            
            uint32_t addr = registers[rb] + registers[ro];
            
            if (S) {
                if (H) { // LDRSH
                    int16_t val = (int16_t)memory.Read16(addr);
                    registers[rd] = (int32_t)val;
                } else { // LDRSB
                    int8_t val = (int8_t)memory.Read8(addr);
                    registers[rd] = (int32_t)val;
                }
            } else {
                if (H) { // LDRH
                    registers[rd] = memory.Read16(addr);
                } else { // STRH
                    memory.Write16(addr, registers[rd] & 0xFFFF);
                }
            }
        }
        // Format 9: Load/Store with Immediate Offset
        // 011x xxxx xxxx xxxx (STRB/LDRB)
        else if ((instruction & 0xE000) == 0x6000) {
            bool B = (instruction >> 12) & 1; // 0=STR, 1=LDR (Word) - Wait, this is Format 9?
            // Format 9: 011B L5 Rn Rd
            // B=0: STR/LDR Word (Format 9 is Byte/Word?)
            // GBATEK: Format 9: Load/Store with Immediate Offset
            // 011B L5 Rn Rd
            // B=0: Word, B=1: Byte
            // L=0: Store, L=1: Load
            
            bool L = (instruction >> 11) & 1;
            uint32_t imm = (instruction >> 6) & 0x1F;
            uint32_t rn = (instruction >> 3) & 0x7;
            uint32_t rd = instruction & 0x7;
            
            uint32_t addr = registers[rn] + (imm * (B ? 1 : 4));

            if (registers[15] >= 0x080015d8 && registers[15] <= 0x08001610) {
                 // std::cout << "Format 9: " << (L?"LDR":"STR") << (B?"B":"") << " Rd=R" << rd << " Rn=R" << rn << " Addr=0x" << std::hex << addr << " Val=0x" << registers[rd] << std::endl;
            }

            if (L) { // Load
                if (B) {
                    registers[rd] = memory.Read8(addr);
                    /*
                    if (registers[15] == 0x08005524 + 2) {
                         std::cout << "LDRB Result: R" << rd << " = 0x" << std::hex << registers[rd] << " from [0x" << addr << "]" << std::endl;
                    }
                    */
                }
                else   registers[rd] = memory.Read32(addr);
            } else { // Store
                if (B) memory.Write8(addr, registers[rd] & 0xFF);
                else   memory.Write32(addr, registers[rd]);
            }
        }
        // Format 10: Load/Store Halfword
        // 1000 xxxx xxxx xxxx
        else if ((instruction & 0xF000) == 0x8000) {
            bool L = (instruction >> 11) & 1;
            uint32_t imm = (instruction >> 6) & 0x1F;
            uint32_t rn = (instruction >> 3) & 0x7;
            uint32_t rd = instruction & 0x7;
            
            uint32_t addr = registers[rn] + (imm * 2);
            
            if (L) registers[rd] = memory.Read16(addr);
            else   memory.Write16(addr, registers[rd] & 0xFFFF);
        }
        // Format 11: Load/Store Multiple
        // 1100 xxxx xxxx xxxx
        else if ((instruction & 0xF000) == 0xC000) {
            bool L = (instruction >> 11) & 1;
            uint32_t rb = (instruction >> 8) & 0x7;
            uint8_t rList = instruction & 0xFF;
            
            uint32_t addr = registers[rb];
            
            for (int i=0; i<8; ++i) {
                if ((rList >> i) & 1) {
                    if (L) { // Load
                        registers[i] = memory.Read32(addr);
                    } else { // Store
                        memory.Write32(addr, registers[i]);
                    }
                    addr += 4;
                }
            }
            
            // Write-back
            registers[rb] = addr;
        }
        // Format 13: Add Offset to Stack Pointer
        // 1011 0000 xxxx xxxx
        else if ((instruction & 0xFF00) == 0xB000) {
            bool S = (instruction >> 7) & 1;
            uint32_t imm = instruction & 0x7F;
            imm *= 4;
            
            if (S) registers[13] -= imm; // SUB
            else   registers[13] += imm; // ADD
        }
        // Format 14: Push/Pop Registers
        // 1011 x10x xxxx xxxx
        else if ((instruction & 0xF600) == 0xB400) {
            bool L = (instruction >> 11) & 1; // 0=Push, 1=Pop
            bool R = (instruction >> 8) & 1; // PC/LR
            uint8_t rList = instruction & 0xFF;
            
            if (!L) { // PUSH
                // Decrement SP, Store Registers
                // R=1: Push LR
                // Rlist: Push R0-R7
                
                uint32_t sp = registers[13];
                uint32_t count = 0;
                if (R) count++;
                for (int i=0; i<8; ++i) if ((rList >> i) & 1) count++;
                
                sp -= (count * 4);
                uint32_t currentAddr = sp;
                registers[13] = sp;
                
                for (int i=0; i<8; ++i) {
                    if ((rList >> i) & 1) {
                        memory.Write32(currentAddr, registers[i]);
                        currentAddr += 4;
                    }
                }
                if (R) {
                    memory.Write32(currentAddr, registers[14]); // Push LR
                }
            } else { // POP
                // Load Registers, Increment SP
                // R=1: Pop PC
                // Rlist: Pop R0-R7
                
                uint32_t sp = registers[13];
                uint32_t currentAddr = sp;
                
                for (int i=0; i<8; ++i) {
                    if ((rList >> i) & 1) {
                        registers[i] = memory.Read32(currentAddr);
                        currentAddr += 4;
                    }
                }
                if (R) {
                    uint32_t pc = memory.Read32(currentAddr);
                    LogBranch(registers[15] - 2, pc);
                    registers[15] = pc & 0xFFFFFFFE; // Pop PC (Thumb)
                    // If bit 0 is 0, switch to ARM? Usually POP PC stays in Thumb unless BX is used?
                    // Actually POP {PC} is interworking in ARMv5T, but on ARM7TDMI (ARMv4T) it might just be a load to PC.
                    // In Thumb state, loading PC sets bit 0 to 0?
                    // Docs say: "If the PC is in the register list, the instruction causes a branch to the address popped off the stack."
                    // "Bit 0 of the loaded value is treated as the new Thumb bit."
                    // So if popped value has bit 0 set, stay in Thumb. If 0, switch to ARM?
                    // Wait, ARM7TDMI POP PC in Thumb:
                    // "The value popped into R15 has bit 0 ignored and the processor remains in Thumb state." - Some sources.
                    // "In ARMv4T, POP {PC} in Thumb state does NOT support interworking."
                    // So it stays in Thumb.
                    registers[15] &= ~1; 
                    currentAddr += 4;
                }
                
                registers[13] = currentAddr;
            }
        }
        // Format 16: Conditional Branch
        // 1101 xxxx xxxx xxxx
        else if ((instruction & 0xF000) == 0xD000) {
            uint32_t cond = (instruction >> 8) & 0xF;
            int8_t offset = (int8_t)(instruction & 0xFF); // Signed 8-bit
            
            if (cond != 0xF) { // 0xF is SWI
                if (CheckCondition(cond)) {
                    uint32_t target = registers[15] + 2 + (offset * 2);
                    LogBranch(registers[15] - 2, target);
                    registers[15] = target;
                }
            } else {
                // SWI (Format 17)
                ExecuteSWI(instruction & 0xFF);
            }
        }
        // Format 18: Unconditional Branch
        // 1110 0xxx xxxx xxxx
        else if ((instruction & 0xF800) == 0xE000) {
            int32_t offset = (instruction & 0x7FF);
            if (offset & 0x400) offset |= 0xFFFFF800; // Sign extend
            offset <<= 1;
            uint32_t target = registers[15] + 2 + offset;
            LogBranch(registers[15] - 2, target);
            registers[15] = target; 
        }
        // Format 19: Long Branch with Link
        // 1111 xxxx xxxx xxxx
        else if ((instruction & 0xF000) == 0xF000) {
            bool H = (instruction >> 11) & 1;
            int32_t offset = instruction & 0x7FF;
            
            if (!H) { // First instruction (High)
                offset = (offset << 12);
                if (offset & 0x400000) offset |= 0xFF800000; // Sign extend
                registers[14] = registers[15] + 2 + offset; // Store in LR (PC+4+offset)
            } else { // Second instruction (Low)
                uint32_t nextPC = registers[15] - 2; // Instruction address + 2 (already incremented)
                uint32_t target = registers[14] + (offset << 1);
                LogBranch(nextPC, target);
                registers[14] = (nextPC + 2) | 1; // LR = Return Address + 1 (Thumb) -> Next Instruction
                registers[15] = target;
            }
        }
        else {
            // std::cout << "Unknown Thumb: 0x" << std::hex << instruction << " at PC: 0x" << (registers[15]-2) << std::endl;
        }
    }



    void ARM7TDMI::SetZN(uint32_t result) {
        if (result == 0) cpsr |= 0x40000000; // Set Z
        else             cpsr &= ~0x40000000; // Clear Z
        
        if (result & 0x80000000) cpsr |= 0x80000000; // Set N
        else                     cpsr &= ~0x80000000; // Clear N
    }

    bool ARM7TDMI::CheckCondition(uint32_t cond) {
        bool N = (cpsr >> 31) & 1;
        bool Z = (cpsr >> 30) & 1;
        bool C = (cpsr >> 29) & 1;
        bool V = (cpsr >> 28) & 1;

        switch (cond) {
            case 0x0: return Z; // EQ
            case 0x1: return !Z; // NE
            case 0x2: return C; // CS/HS
            case 0x3: return !C; // CC/LO
            case 0x4: return N; // MI
            case 0x5: return !N; // PL
            case 0x6: return V; // VS
            case 0x7: return !V; // VC
            case 0x8: return C && !Z; // HI
            case 0x9: return !C || Z; // LS
            case 0xA: return N == V; // GE
            case 0xB: return N != V; // LT
            case 0xC: return !Z && (N == V); // GT
            case 0xD: return Z || (N != V); // LE
            case 0xE: return true; // AL
            default: return true;
        }
    }

    void ARM7TDMI::ExecuteMultiply(uint32_t instruction) {
        bool A = (instruction >> 21) & 1; // Accumulate
        bool S = (instruction >> 20) & 1; // Set Flags
        uint32_t rd = (instruction >> 16) & 0xF;
        uint32_t rn = (instruction >> 12) & 0xF;
        uint32_t rs = (instruction >> 8) & 0xF;
        uint32_t rm = instruction & 0xF;

        uint32_t op1 = registers[rm];
        uint32_t op2 = registers[rs];
        uint32_t result = op1 * op2;

        if (A) {
            result += registers[rn];
        }

        registers[rd] = result;

        if (S) {
            SetZN(result);
        }
    }

    void ARM7TDMI::ExecuteMultiplyLong(uint32_t instruction) {
        bool U = (instruction >> 22) & 1; // Unsigned/Signed
        bool A = (instruction >> 21) & 1; // Accumulate
        bool S = (instruction >> 20) & 1; // Set Flags
        uint32_t rdHi = (instruction >> 16) & 0xF;
        uint32_t rdLo = (instruction >> 12) & 0xF;
        uint32_t rs = (instruction >> 8) & 0xF;
        uint32_t rm = instruction & 0xF;

        uint64_t op1, op2, result;

        if (U) { // Signed
            op1 = (int64_t)(int32_t)registers[rm];
            op2 = (int64_t)(int32_t)registers[rs];
            result = op1 * op2;
        } else { // Unsigned
            op1 = (uint64_t)registers[rm];
            op2 = (uint64_t)registers[rs];
            result = op1 * op2;
        }

        if (A) {
            uint64_t acc = ((uint64_t)registers[rdHi] << 32) | registers[rdLo];
            result += acc;
        }

        registers[rdLo] = (uint32_t)(result & 0xFFFFFFFF);
        registers[rdHi] = (uint32_t)(result >> 32);

        if (S) {
            SetZN(registers[rdHi]); // N and Z are set based on 64-bit result?
            // ARM docs: N bit set to bit 63 of result. Z bit set if 64-bit result is 0.
            if (result == 0) cpsr |= 0x40000000; else cpsr &= ~0x40000000;

            if ((result >> 63) & 1) cpsr |= 0x80000000; else cpsr &= ~0x80000000;
            // V and C are undefined (or V unaffected, C meaningless)
        }
    }

    void ARM7TDMI::ExecuteMRS(uint32_t instruction) {
        bool R = (instruction >> 22) & 1;
        uint32_t rd = (instruction >> 12) & 0xF;
        
        if (R) { // SPSR
            registers[rd] = spsr;
        } else { // CPSR
            registers[rd] = cpsr;
        }
    }

    void ARM7TDMI::ExecuteMSR(uint32_t instruction) {
        bool I = (instruction >> 25) & 1;
        bool R = (instruction >> 22) & 1;
        uint32_t mask = (instruction >> 16) & 0xF;
        uint32_t operand = 0;
        
        if (I) {
            uint32_t rotate = (instruction >> 8) & 0xF;
            uint32_t imm = instruction & 0xFF;
            uint32_t shift = rotate * 2;
            operand = (imm >> shift) | (imm << (32 - shift));
        } else {
            uint32_t rm = instruction & 0xF;
            operand = registers[rm];
        }
        
        uint32_t currentPSR = R ? spsr : cpsr;
        uint32_t newPSR = currentPSR;
        
        if (mask & 1) newPSR = (newPSR & 0xFFFFFF00) | (operand & 0x000000FF); // Control
        if (mask & 2) newPSR = (newPSR & 0xFFFF00FF) | (operand & 0x0000FF00); // Extension
        if (mask & 4) newPSR = (newPSR & 0xFF00FFFF) | (operand & 0x00FF0000); // Status
        if (mask & 8) newPSR = (newPSR & 0x00FFFFFF) | (operand & 0xFF000000); // Flags
        
        if (R) {
            spsr = newPSR;
        } else {
            // If changing CPSR, we might switch mode!
            uint32_t oldMode = cpsr & 0x1F;
            uint32_t newMode = newPSR & 0x1F;
            
            cpsr = newPSR;
            
            if (oldMode != newMode) {
                SwitchMode(newMode);
            }
        }
    }



}
