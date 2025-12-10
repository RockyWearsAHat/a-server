#include <emulator/gba/ARM7TDMI.h>
#include <emulator/gba/GBAMemory.h>
#include <cstring>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <deque>

namespace AIO::Emulator::GBA {

    // Branch logging - used for debugging invalid jumps and crash logs
    static std::deque<std::pair<uint32_t, uint32_t>> branchLog;

    // Crash notification callback (set by GUI)
    void (*CrashPopupCallback)(const char* logPath) = nullptr;
    
    static void LogBranch([[maybe_unused]] uint32_t from, [[maybe_unused]] uint32_t to) {
        branchLog.push_back({from, to});
        if (branchLog.size() > 50) branchLog.pop_front();

        bool valid = (to >= 0x08000000 && to < 0x0E000000) ||
                     (to >= 0x03000000 && to < 0x04000000) ||
                     (to >= 0x02000000 && to < 0x03000000) ||
                     (to < 0x4000) ||
                     ((to & 0xFFFFFF00) == 0xFFFFFF00);
        if (!valid) {
            std::cerr << "BAD BRANCH: 0x" << std::hex << from << " -> 0x" << to << std::dec << std::endl;
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
        uint16_t ime = memory.Read16(0x04000208);
        uint16_t ie = memory.Read16(0x04000200);
        uint16_t if_reg = memory.Read16(0x04000202);

        // Wake from halt if any enabled interrupt is pending
        if (halted && (ie & if_reg)) {
            halted = false;
        }

        if (!(ime & 1)) return;
        if (cpsr & 0x80) return; // IRQ disabled in CPSR

        if (ie & if_reg) {
            // Calculate triggered interrupts NOW before PPU/Timers run
            // On real GBA, BIOS reads IE & IF atomically at IRQ entry
            uint16_t triggered = ie & if_reg;
            
            // Update BIOS_IF immediately (before PPU can change IF)
            uint32_t biosIF = memory.Read32(0x03007FF8);
            memory.Write32(0x03007FF8, biosIF | triggered);
            
            // Also write to a scratch location the BIOS can read
            // We'll use 0x03007FF4 as a temp storage for triggered bits
            memory.Write16(0x03007FF4, triggered);
            
            // Save CPSR to SPSR_irq
            uint32_t oldCpsr = cpsr;
            if (thumbMode) oldCpsr |= 0x20; else oldCpsr &= ~0x20;
            
            // Switch to IRQ Mode
            SwitchMode(0x12);
            spsr = oldCpsr;
            spsr_irq = oldCpsr; // Also save to banked SPSR for System Mode return
            
            // Disable Thumb, enable IRQ mask
            thumbMode = false;
            cpsr &= ~0x20;
            cpsr |= 0x80;
            
            // Calculate return address (LR = PC_next + 4)
            // registers[15] points to the next instruction to be executed
            registers[14] = registers[15] + 4;
            
            // Jump to BIOS IRQ Trampoline at 0x180
            registers[15] = 0x180;
        }
    }

    void ARM7TDMI::Step() {
        static uint32_t lastPC = 0;
        
        // Sanity Check SP
        if (registers[13] == 0) {
             std::cerr << "[FATAL] SP is 0! CPSR=0x" << std::hex << cpsr << " Mode=0x" << (cpsr & 0x1F) << std::dec << std::endl;
             std::cerr << "Last PC: 0x" << std::hex << lastPC << std::dec << std::endl;
             std::cerr << "PC: 0x" << std::hex << registers[15] << std::dec << std::endl;
             
             // Dump Instructions around Last PC
             std::cerr << "Code around Last PC (0x" << lastPC << "):" << std::endl;
             for (int i = -16; i <= 16; i+=2) {
                 uint32_t addr = lastPC + i;
                 if (addr >= 0x08000000) {
                     uint16_t val = memory.Read16(addr);
                     std::cerr << "0x" << addr << ": " << val << (i==0 ? " <--" : "") << std::endl;
                 }
             }
             exit(1);
        }

        CheckInterrupts();

        if (halted) {
            return;
        }
        
        // Universal PC region validation and mirroring
        // Canonicalize IWRAM/EWRAM/VRAM/ROM
        uint32_t pc = registers[15];
        if (pc >= 0x03008000 && pc < 0x04000000)
            pc = 0x03000000 | (pc & 0x7FFF);
        if (pc >= 0x02040000 && pc < 0x03000000)
            pc = 0x02000000 | (pc & 0x3FFFF);
        if (pc >= 0x06018000 && pc < 0x07000000)
            pc = 0x06000000 | (pc & 0x17FFF);
        if (pc >= 0x08000000 && pc < 0x10000000)
            pc = 0x08000000 | (pc & 0x1FFFFFF);
        registers[15] = pc;

        // Validate PC region
        bool valid = false;
        if (pc < 0x00004000) valid = true; // BIOS
        if (pc >= 0x02000000 && pc < 0x02040000) valid = true; // EWRAM
        if (pc >= 0x03000000 && pc < 0x03008000) valid = true; // IWRAM
        if (pc >= 0x06000000 && pc < 0x06018000) valid = true; // VRAM
        if (pc >= 0x08000000 && pc < 0x10000000) valid = true; // ROM
        if (!valid) {
            std::cerr << "[FATAL] Invalid PC: 0x" << std::hex << pc << std::dec << std::endl;
            halted = true;
            // Write crash info to log file asynchronously
            FILE* logFile = fopen("crash_log.txt", "a");
            if (logFile) {
                fprintf(logFile, "==== Emulator Crash ====");
                fprintf(logFile, "\nPC: 0x%08X\n", pc);
                for (int i = 0; i < 16; ++i)
                    fprintf(logFile, "R%d: 0x%08X\n", i, registers[i]);
                fprintf(logFile, "CPSR: 0x%08X\n", cpsr);
                fprintf(logFile, "ThumbMode: %d\n", thumbMode);
                fprintf(logFile, "BranchLog (last 50):\n");
                for (const auto& br : branchLog)
                    fprintf(logFile, "  0x%08X -> 0x%08X\n", br.first, br.second);
                // Optionally dump a small memory region around SP
                uint32_t sp = registers[13];
                fprintf(logFile, "Stack Dump:\n");
                for (int i = 0; i < 64; i += 4)
                    fprintf(logFile, "  0x%08X: 0x%08X\n", sp + i, memory.Read32(sp + i));
                fclose(logFile);
            }
            // Signal GUI to show crash popup and allow log viewing if callback is set
            if (CrashPopupCallback) CrashPopupCallback("crash_log.txt");
            return;
        }

        pc = registers[15];

        // ...existing code...

        // Trace Interrupt Handler
        /*
        if ((pc >= 0x180 && pc <= 0x220) || (pc >= 0x03002364 && pc <= 0x03002600) || (pc >= 0x08000578 && pc <= 0x08000600) || (pc >= 0x08000400 && pc <= 0x08000550)) {
             uint32_t instr = (thumbMode) ? memory.ReadInstruction16(pc) : memory.ReadInstruction32(pc);
             std::cout << "[Trace] PC=0x" << std::hex << pc << " Instr=0x" << instr << " Mode=" << (thumbMode?"Thumb":"ARM") 
                       << " R0=" << registers[0] << " R1=" << registers[1] << " R2=" << registers[2] << " R3=" << registers[3] << " R12=" << registers[12] << " LR=" << registers[14] << std::dec << std::endl;
        }
        */
        
        // DEBUG: Trace near crash point 0x300330c
        static bool crashTraceEnabled = false;
        static int crashTraceCount = 0;
        
        // Trace caller at ROM before BX R0
        if (pc >= 0x8032400 && pc <= 0x8032420 && !crashTraceEnabled) {
            uint32_t instr = (thumbMode) ? memory.ReadInstruction16(pc) : memory.ReadInstruction32(pc);
            std::cout << "[CallerTrace] PC=0x" << std::hex << pc << " Instr=0x" << instr 
                      << " R0=0x" << registers[0] << " R1=0x" << registers[1]
                      << " R11=0x" << registers[11] << " LR=0x" << registers[14]
                      << " Mode=" << (thumbMode?"T":"A") << std::dec << std::endl;
        }
        
        // Trace early ROM startup (first 100 instructions from 0x08000000)
        static int earlyTraceCount = 0;
        if (pc >= 0x08000000 && pc <= 0x08000200 && earlyTraceCount < 100) {
            earlyTraceCount++;
            uint32_t instr = (thumbMode) ? memory.ReadInstruction16(pc) : memory.ReadInstruction32(pc);
            std::cout << "[EarlyTrace] PC=0x" << std::hex << pc << " Instr=0x" << instr 
                      << " R0=0x" << registers[0] << " R1=0x" << registers[1]
                      << " R13=0x" << registers[13] << " LR=0x" << registers[14]
                      << std::dec << std::endl;
        }
        
        // Trace 0x800023d to 0x8000996 (before DMA#1)
        static int preDmaTraceCount = 0;
        if (pc >= 0x0800023c && pc <= 0x080009a0 && preDmaTraceCount < 200) {
            preDmaTraceCount++;
            uint32_t instr = (thumbMode) ? memory.ReadInstruction16(pc) : memory.ReadInstruction32(pc);
            std::cout << "[PreDMA] PC=0x" << std::hex << pc << " Instr=0x" << instr 
                      << " R0=0x" << registers[0] << " R1=0x" << registers[1]
                      << " R2=0x" << registers[2] << " R3=0x" << registers[3]
                      << " SP=0x" << registers[13]
                      << (thumbMode?" T":" A") << std::dec << std::endl;
        }
        
        // Trace the entry point 0x30032b0 to 0x3003400 (expanded to cover branch targets)
        if (pc >= 0x30032b0 && pc <= 0x3003400 && !crashTraceEnabled) {
            uint32_t instr = (thumbMode) ? memory.ReadInstruction16(pc) : memory.ReadInstruction32(pc);
            std::cout << "[EntryTrace] PC=0x" << std::hex << pc << " Instr=0x" << instr 
                      << " R0=0x" << registers[0] << " R1=0x" << registers[1]
                      << " R11=0x" << registers[11] << " LR=0x" << registers[14]
                      << " Mode=" << (thumbMode?"T":"A") << std::dec << std::endl;
        }
        if (pc == 0x3003308) {
            // This is the LDR R0, [R12, R0, LSL #4] instruction
            // R0 should be a small index (0-15 or so)
            if (registers[0] > 20) {
                std::cout << "[CRASH TRACE] LDR at 0x3003308 with BAD R0=0x" << std::hex << registers[0]
                          << " R11=0x" << registers[11]
                          << " R12=0x" << registers[12] << std::dec << std::endl;
            }
        }
        if (pc >= 0x3003300 && pc <= 0x3003320) {
            crashTraceEnabled = true;
        }
        if (crashTraceEnabled && crashTraceCount < 200) {
            crashTraceCount++;
            uint32_t instr = (thumbMode) ? memory.ReadInstruction16(pc) : memory.ReadInstruction32(pc);
            std::cout << "[CrashTrace] PC=0x" << std::hex << pc << " Instr=0x" << instr 
                      << " R0=0x" << registers[0] << " R11=0x" << registers[11]
                      << " R12=0x" << registers[12] 
                      << " LR=0x" << registers[14]
                      << " R1=0x" << registers[1] << " R2=0x" << registers[2]
                      << " SP=0x" << registers[13] << std::dec << std::endl;
        }
        
        // ...existing code...

        lastPC = pc;

        if (thumbMode) {
            uint16_t instruction = memory.ReadInstruction16(pc);
            
            // DEBUG: Trace instruction at PC near 0x80323c6 (disabled)
            // if (pc >= 0x80323c0 && pc <= 0x80323d0) {
            //     std::cout << "[THUMB] PC=0x" << std::hex << pc 
            //               << " instr=0x" << instruction << std::dec << std::endl;
            // }
            
            registers[15] += 2;
            DecodeThumb(instruction);
        } else {
            uint32_t instruction = memory.ReadInstruction32(pc);
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
        
        // DEBUG: Trace branches in DKC audio code
        if (currentPC >= 0x30032b0 && currentPC <= 0x3003400) {
            std::cout << "[BRANCH DEBUG] PC=0x" << std::hex << (currentPC - 4) 
                      << " instr=0x" << instruction << " offset=" << offset 
                      << " target=0x" << target << std::dec << std::endl;
        }
        
        LogBranch(currentPC - 4, target); // Log from actual instruction address
        registers[15] = target;
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
            uint32_t shift = rotate * 2;
            op2 = (imm >> shift) | (imm << (32 - shift));
        } else {
            // Register with optional shift
            uint32_t rm = instruction & 0xF;
            uint32_t rmVal = registers[rm];
            if (rm == 15) rmVal += 4; // PC is Instruction + 8 (registers[15] is Instruction + 4)
            
            bool shiftByReg = (instruction >> 4) & 1;
            uint32_t shiftType = (instruction >> 5) & 3;
            uint32_t shiftAmount;
            
            if (shiftByReg) {
                uint32_t rs = (instruction >> 8) & 0xF;
                shiftAmount = registers[rs] & 0xFF;
            } else {
                shiftAmount = (instruction >> 7) & 0x1F;
            }
            
            // Apply shift
            switch (shiftType) {
                case 0: // LSL
                    if (shiftAmount == 0) {
                        op2 = rmVal;
                    } else if (shiftAmount < 32) {
                        op2 = rmVal << shiftAmount;
                    } else {
                        op2 = 0;
                    }
                    break;
                case 1: // LSR
                    if (shiftAmount == 0) {
                        op2 = 0;
                    } else if (shiftAmount < 32) {
                        op2 = rmVal >> shiftAmount;
                    } else {
                        op2 = 0;
                    }
                    break;
                case 2: // ASR
                    if (shiftAmount == 0) {
                        op2 = (rmVal & 0x80000000) ? 0xFFFFFFFF : 0;
                    } else if (shiftAmount < 32) {
                        op2 = (int32_t)rmVal >> shiftAmount;
                    } else {
                        op2 = (rmVal & 0x80000000) ? 0xFFFFFFFF : 0;
                    }
                    break;
                case 3: // ROR
                    if (shiftAmount == 0) {
                        bool carryIn = (cpsr >> 29) & 1;
                        op2 = (carryIn << 31) | (rmVal >> 1);
                    } else {
                        shiftAmount &= 31;
                        if (shiftAmount == 0) {
                            op2 = rmVal;
                        } else {
                            op2 = (rmVal >> shiftAmount) | (rmVal << (32 - shiftAmount));
                        }
                    }
                    break;
            }
        }

        uint32_t result = 0;
        uint32_t rnVal = registers[rn];
        if (rn == 15) rnVal += 4; // PC is Instruction + 8 (registers[15] is Instruction + 4)
        bool carry = (cpsr >> 29) & 1;
        bool overflow = (cpsr >> 28) & 1;
        bool cOut = carry;

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
                cOut = (rnVal >= op2);
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
                uint32_t oldMode = cpsr & 0x1F;  // Save old mode BEFORE modifying cpsr
                if ((cpsr & 0x1F) == 0x1F) {
                    // System Mode - Hack for BIOS IRQ return
                    // Assume we came from IRQ and use SPSR_irq
                    cpsr = spsr_irq;
                } else {
                    cpsr = spsr;
                }
                uint32_t newMode = cpsr & 0x1F;
                // Force proper mode switch by passing old mode
                if (oldMode != newMode) {
                    // Manually do what SwitchMode does but with correct old mode
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
                }
                thumbMode = (cpsr & 0x20) != 0;
            } else {
                SetZN(result);
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
            offset = instruction & 0xFFF;
        } else {
            uint32_t rm = instruction & 0xF;
            offset = registers[rm];
        }

        uint32_t baseAddr = registers[rn];
        if (rn == 15) {
            baseAddr += 4; 
        }

        uint32_t targetAddr = baseAddr;
        if (P) {
            if (U) targetAddr += offset;
            else   targetAddr -= offset;
        }

        if (L) {
            // Load
            if (B) {
                registers[rd] = memory.Read8(targetAddr);
            } else {
                uint32_t val = memory.Read32(targetAddr);
                if (rd == 15) {
                    LogBranch(registers[15] - 4, val);
                }
                registers[rd] = val;
            }
        } else {
            // Store
            uint32_t val = registers[rd];
            if (rd == 15) val += 8; // PC+12 (registers[15] is PC+4)

            if (B) {
                memory.Write8(targetAddr, val & 0xFF);
            } else {
                memory.Write32(targetAddr, val);
            }
        }

        if (!P || W) {
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
                    
                    // DEBUG: Trace ARM STM to 0x3001500
                    if ((currentAddr >> 24) == 0x03 && (currentAddr & 0x7FFF) >= 0x1500 && (currentAddr & 0x7FFF) < 0x1510) {
                        std::cout << "[ARM STM] PC=0x" << std::hex << (registers[15] - 8)
                                  << " [0x" << currentAddr << "] <- R" << std::dec << i 
                                  << " (0x" << std::hex << val << ")" << std::endl;
                    }
                    
                    memory.Write32(currentAddr, val);
                    // std::cout << "STM: [0x" << std::hex << currentAddr << "] <- R" << i << " (0x" << val << ")" << std::endl;
                }
                currentAddr += 4;
            }
        }

        // Writeback
        // For LDM, if Rn is in the list, writeback is ignored (loaded value persists)
        // For STM, writeback always happens (base is updated)
        if (writeBack) {
            bool rnInList = (regList >> rn) & 1;
            if (!L || !rnInList) {
                if (U) registers[rn] = startAddress + (numRegs * 4);
                else   registers[rn] = startAddress - (numRegs * 4);
            }
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
        if (rn == 15) baseAddr += 4; // PC+8 (registers[15] is PC+4)

        uint32_t targetAddr = baseAddr;
        if (P) {
            if (U) targetAddr += offset;
            else   targetAddr -= offset;
        }

        if (L) {
            // Load
            if (opcode == 1) { // LDRH (Unsigned Halfword)
                registers[rd] = memory.Read16(targetAddr);
            } else if (opcode == 2) { // LDRSB (Signed Byte)
                int8_t val = (int8_t)memory.Read8(targetAddr);
                registers[rd] = (int32_t)val;
            } else if (opcode == 3) { // LDRSH (Signed Halfword)
                int16_t val = (int16_t)memory.Read16(targetAddr);
                registers[rd] = (int32_t)val;
            }
        } else {
            // Store
            if (opcode == 1) { // STRH
                memory.Write16(targetAddr, registers[rd] & 0xFFFF);
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

        LogBranch(registers[15] - 4, target);

        if (target & 1) {
            thumbMode = true;
            cpsr |= 0x20;  // Set T bit in CPSR
            registers[15] = target & 0xFFFFFFFE;
        } else {
            thumbMode = false;
            cpsr &= ~0x20; // Clear T bit in CPSR
            registers[15] = target & 0xFFFFFFFC; // Align to 4 bytes
        }
    }

    void ARM7TDMI::ExecuteSWI(uint32_t comment) {
        switch (comment) {
            case 0x00: // SoftReset
                registers[15] = 0x08000000;
                registers[13] = 0x03007F00; // Reset SP
                break;
            case 0x01: // RegisterRamReset - Clear/Initialize RAM and registers
            {
                uint8_t flags = registers[0] & 0xFF;
                
                // Bit 0: Clear 256K EWRAM (0x02000000-0x0203FFFF)
                if (flags & 0x01) {
                    for (uint32_t addr = 0x02000000; addr < 0x02040000; addr++) {
                        memory.Write8(addr, 0);
                    }
                }
                
                // Bit 1: Clear 32K IWRAM (0x03000000-0x03007FFF, excluding last 0x200 bytes)
                if (flags & 0x02) {
                    for (uint32_t addr = 0x03000000; addr < 0x03007E00; addr++) {
                        memory.Write8(addr, 0);
                    }
                }
                
                // Bit 2: Clear Palette RAM (0x05000000-0x050003FF)
                if (flags & 0x04) {
                    for (uint32_t addr = 0x05000000; addr < 0x05000400; addr++) {
                        memory.Write8(addr, 0);
                    }
                }
                
                // Bit 3: Clear VRAM (0x06000000-0x06017FFF)
                if (flags & 0x08) {
                    for (uint32_t addr = 0x06000000; addr < 0x06018000; addr++) {
                        memory.Write8(addr, 0);
                    }
                }
                
                // Bit 4: Clear OAM (0x07000000-0x070003FF)
                if (flags & 0x10) {
                    for (uint32_t addr = 0x07000000; addr < 0x07000400; addr++) {
                        memory.Write8(addr, 0);
                    }
                }
                
                // Bits 5-6: Reset SIO registers (bit 5=0x04000120-0x04000159, bit 6=0x04000300)
                // TODO: Implement SIO register reset if needed
                
                // Bit 7: Reset other registers
                if (flags & 0x80) {
                    // Reset most IO registers to defaults
                    // DISPCNT, DISPSTAT, BG control, etc.
                    memory.Write16(0x04000000, 0x0080); // DISPCNT: Force blank
                    memory.Write16(0x04000004, 0x0000); // DISPSTAT
                    memory.Write16(0x04000008, 0x0000); // BG0CNT
                    memory.Write16(0x0400000A, 0x0000); // BG1CNT
                    memory.Write16(0x0400000C, 0x0000); // BG2CNT
                    memory.Write16(0x0400000E, 0x0000); // BG3CNT
                    // Reset sound registers
                    for (uint32_t addr = 0x04000060; addr <= 0x040000A6; addr += 2) {
                        memory.Write16(addr, 0);
                    }
                    // Reset DMA registers
                    for (uint32_t addr = 0x040000B0; addr <= 0x040000DE; addr += 2) {
                        memory.Write16(addr, 0);
                    }
                }
                break;
            }
            case 0x02: // Halt
                halted = true;
                break;
            case 0x03: // Stop/Sleep
                halted = true;
                break;
            case 0x04: // IntrWait
            intrwait_entry:
            {
                // R0 = Clear Old Flags (1=Clear), R1 = Wait Flags
                uint32_t clearOld = registers[0];
                uint32_t waitFlags = registers[1];

                if (clearOld) {
                    uint32_t currentFlags = memory.Read32(0x03007FF8);
                    memory.Write32(0x03007FF8, currentFlags & ~waitFlags);
                    registers[0] = 0; // Clear the "Clear Old" flag so we don't do it again on resume
                }

                uint32_t currentFlags = memory.Read32(0x03007FF8);
                if (currentFlags & waitFlags) {
                    // Condition met! Return.
                    // Acknowledge flags in BIOS_IF (Standard BIOS behavior)
                    memory.Write32(0x03007FF8, currentFlags & ~waitFlags);
                    return;
                }

                // Condition not met. Enable interrupts and Halt.
                memory.Write16(0x04000208, 1); // IME=1
                uint16_t ie = memory.Read16(0x04000200);
                memory.Write16(0x04000200, ie | (waitFlags & 0xFFFF)); // Enable required IRQs
                cpsr &= ~0x80; // Enable IRQ in CPSR
                halted = true;

                // Rewind PC to re-execute this SWI instruction
                if (thumbMode) registers[15] -= 2;
                else registers[15] -= 4;
                break;
            }
            case 0x05: // VBlankIntrWait
            {
                // VBlankIntrWait is equivalent to:
                // R0 = 1 (clear old flags)
                // R1 = 1 (wait for VBlank IRQ, bit 0)
                // Then call IntrWait (SWI 0x04)
                
                // Enable VBlank IRQ in DISPSTAT (Bit 3) - Required for VBlank IRQ to fire
                uint16_t dispstat = memory.Read16(0x04000004);
                memory.Write16(0x04000004, dispstat | 0x0008);
                
                // Set up for IntrWait: R0=1, R1=1
                registers[0] = 1; // Clear old flags
                registers[1] = 1; // Wait for VBlank IRQ (bit 0)
                
                // Fall through to IntrWait logic
                goto intrwait_entry;
            }
            case 0x06: // Div - R0 = R0 / R1, R1 = R0 % R1, R3 = abs(R0 / R1)
            {
                int32_t num = (int32_t)registers[0];
                int32_t denom = (int32_t)registers[1];
                if (denom == 0) {
                    // Division by zero
                    // R0 = 0 (if R0=0), +1 (if R0>0), -1 (if R0<0)
                    // R1 = R0
                    // R3 = 0 (if R0=0), +1 (if R0!=0)
                    if (num == 0) {
                        registers[0] = 0;
                        registers[3] = 0;
                    } else {
                        registers[0] = (num < 0) ? -1 : 1;
                        registers[3] = 1;
                    }
                    registers[1] = num;
                } else {
                    int32_t result = num / denom;
                    int32_t remainder = num % denom;
                    registers[0] = (uint32_t)result;
                    registers[1] = (uint32_t)remainder;
                    registers[3] = (uint32_t)(result < 0 ? -result : result);
                }
                break;
            }
            case 0x07: // DivArm - Same as Div but with R0 and R1 swapped
            {
                int32_t num = (int32_t)registers[1];
                int32_t denom = (int32_t)registers[0];
                if (denom == 0) {
                    if (num == 0) {
                        registers[0] = 0;
                        registers[3] = 0;
                    } else {
                        registers[0] = (num < 0) ? -1 : 1;
                        registers[3] = 1;
                    }
                    registers[1] = num;
                } else {
                    int32_t result = num / denom;
                    int32_t remainder = num % denom;
                    registers[0] = (uint32_t)result;
                    registers[1] = (uint32_t)remainder;
                    registers[3] = (uint32_t)(result < 0 ? -result : result);
                }
                break;
            }
            case 0x08: // Sqrt - R0 = sqrt(R0)
            {
                uint32_t val = registers[0];
                uint32_t result = 0;
                uint32_t bit = 1 << 30; // Start with highest bit
                
                while (bit > val) bit >>= 2;
                
                while (bit != 0) {
                    if (val >= result + bit) {
                        val -= result + bit;
                        result = (result >> 1) + bit;
                    } else {
                        result >>= 1;
                    }
                    bit >>= 2;
                }
                registers[0] = result;
                break;
            }
            case 0x09: // ArcTan - R0 = arctan(R0)
            {
                // R0 is a signed 16.16 fixed-point value representing tan(θ)
                // Result is a signed 16.16 fixed-point angle in range -π/4 to π/4
                int32_t x = (int32_t)registers[0];
                // Convert to double, calculate, convert back
                double tanVal = x / 65536.0;
                double angle = atan(tanVal);
                // Convert to 16.16 fixed point (but result is actually in 1.15 format for BIOS)
                // BIOS returns value in range -0x4000 to 0x4000 (-π/4 to π/4 scaled)
                registers[0] = (uint32_t)(int32_t)(angle / (2.0 * 3.14159265358979323846) * 65536.0);
                break;
            }
            case 0x0A: // ArcTan2 - R0 = arctan2(R0, R1)
            {
                // R0 = Y, R1 = X (both signed 16.16 fixed-point)
                // Result is angle from 0 to 2π mapped to 0x0000-0xFFFF
                int32_t y = (int32_t)registers[0];
                int32_t x = (int32_t)registers[1];
                double yVal = y / 65536.0;
                double xVal = x / 65536.0;
                double angle = atan2(yVal, xVal); // Returns -PI to PI
                
                if (angle < 0) {
                    angle += 2.0 * 3.14159265358979323846;
                }
                
                // Map 0-2PI to 0-FFFF
                uint16_t result = (uint16_t)(angle / (2.0 * 3.14159265358979323846) * 65536.0);
                registers[0] = result;
                break;
            }
            case 0x0B: // CpuSet (R0=Src, R1=Dst, R2=Cnt/Ctrl)
            {
                uint32_t src = registers[0];
                uint32_t dst = registers[1];
                uint32_t len = registers[2] & 0x1FFFFF;
                bool fixedSrc = (registers[2] >> 24) & 1;
                bool is32Bit = (registers[2] >> 26) & 1;
                
                // Debug: trace CpuSet to VRAM
                if ((dst & 0xFF000000) == 0x06000000) {
                    std::cout << "[SWI 0x0B] CpuSet to VRAM: src=0x" << std::hex << src
                              << " dst=0x" << dst << " len=" << std::dec << len
                              << " 32bit=" << is32Bit << " fixed=" << fixedSrc << std::endl;
                }

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
                
                // len is the word count (must be multiple of 8)
                bool fixedSrc = (registers[2] >> 24) & 1;
                
                // Debug: trace CpuFastSet to VRAM
                if ((dst & 0xFF000000) == 0x06000000) {
                    std::cout << "[SWI 0x0C] CpuFastSet to VRAM: src=0x" << std::hex << src
                              << " dst=0x" << dst << " len=" << std::dec << len
                              << " fixed=" << fixedSrc << std::endl;
                }
                
                // Always 32-bit
                for (uint32_t i = 0; i < len; ++i) {
                    uint32_t val = memory.Read32(src);
                    memory.Write32(dst, val);
                    dst += 4;
                    if (!fixedSrc) src += 4;
                }
                break;
            }
            case 0x0E: // BgAffineSet
            {
                // R0 = Source Address, R1 = Destination Address, R2 = Number of calculations
                // Source: 4 bytes OrigCenterX, 4 bytes OrigCenterY, 2 bytes DisplayCenterX, 2 bytes DisplayCenterY
                //         2 bytes ScaleX, 2 bytes ScaleY, 2 bytes Angle
                // Destination: 2 bytes PA, 2 bytes PB, 2 bytes PC, 2 bytes PD, 4 bytes StartX, 4 bytes StartY
                uint32_t src = registers[0];
                uint32_t dst = registers[1];
                uint32_t count = registers[2];
                
                for (uint32_t i = 0; i < count; ++i) {
                    // Read source parameters
                    int32_t origCenterX = (int32_t)memory.Read32(src);       // 8.8 fixed point
                    int32_t origCenterY = (int32_t)memory.Read32(src + 4);   // 8.8 fixed point
                    int16_t dispCenterX = (int16_t)memory.Read16(src + 8);
                    int16_t dispCenterY = (int16_t)memory.Read16(src + 10);
                    int16_t scaleX = (int16_t)memory.Read16(src + 12);       // 8.8 fixed point
                    int16_t scaleY = (int16_t)memory.Read16(src + 14);       // 8.8 fixed point
                    uint16_t angle = memory.Read16(src + 16);                // 0-FFFF = 0-360°
                    
                    // Convert angle to radians (0-FFFF maps to 0-2π)
                    double theta = (angle / 65536.0) * 2.0 * 3.14159265358979323846;
                    double cosA = cos(theta);
                    double sinA = sin(theta);
                    
                    // Calculate affine parameters (8.8 fixed point)
                    // PA = cos / scaleX, PB = sin / scaleX, PC = -sin / scaleY, PD = cos / scaleY
                    int16_t pa = (int16_t)((cosA * 256.0) / (scaleX / 256.0));
                    int16_t pb = (int16_t)((sinA * 256.0) / (scaleX / 256.0));
                    int16_t pc = (int16_t)((-sinA * 256.0) / (scaleY / 256.0));
                    int16_t pd = (int16_t)((cosA * 256.0) / (scaleY / 256.0));
                    
                    // Calculate start position (19.8 fixed point)
                    // StartX = OrigCenterX - (PA * DispCenterX + PB * DispCenterY)
                    // StartY = OrigCenterY - (PC * DispCenterX + PD * DispCenterY)
                    int32_t startX = origCenterX - ((pa * dispCenterX + pb * dispCenterY) >> 8);
                    int32_t startY = origCenterY - ((pc * dispCenterX + pd * dispCenterY) >> 8);
                    
                    // Write destination
                    memory.Write16(dst, (uint16_t)pa);
                    memory.Write16(dst + 2, (uint16_t)pb);
                    memory.Write16(dst + 4, (uint16_t)pc);
                    memory.Write16(dst + 6, (uint16_t)pd);
                    memory.Write32(dst + 8, (uint32_t)startX);
                    memory.Write32(dst + 12, (uint32_t)startY);
                    
                    src += 20;  // Source is 20 bytes
                    dst += 16;  // Destination is 16 bytes
                }
                break;
            }
            case 0x0F: // ObjAffineSet
            {
                // R0 = Source Address, R1 = Destination Address, R2 = Number of calculations, R3 = Offset
                // Source: 2 bytes ScaleX, 2 bytes ScaleY, 2 bytes Angle (each entry is 8 bytes, padded)
                // Destination: 2 bytes PA, 2 bytes PB, 2 bytes PC, 2 bytes PD (written with R3 offset between each)
                uint32_t src = registers[0];
                uint32_t dst = registers[1];
                uint32_t count = registers[2];
                uint32_t offset = registers[3];
                
                for (uint32_t i = 0; i < count; ++i) {
                    // Read source parameters (8.8 fixed point)
                    int16_t scaleX = (int16_t)memory.Read16(src);     // 8.8 fixed point, 0x100 = 1.0
                    int16_t scaleY = (int16_t)memory.Read16(src + 2); // 8.8 fixed point
                    uint16_t angle = memory.Read16(src + 4);          // 0-FFFF = 0-360°
                    
                    // Convert angle to radians (0-FFFF maps to 0-2π)
                    double theta = (angle / 65536.0) * 2.0 * 3.14159265358979323846;
                    double cosA = cos(theta);
                    double sinA = sin(theta);
                    
                    // Calculate affine parameters
                    // For ObjAffineSet, the formula is different from BgAffineSet:
                    // PA = cos * scaleX / 256, PB = sin * scaleX / 256
                    // PC = -sin * scaleY / 256, PD = cos * scaleY / 256
                    // This is because we're scaling UP (the inverse of background scaling)
                    int16_t pa = (int16_t)(cosA * scaleX / 256.0 * 256.0);   // Result is 8.8 fixed
                    int16_t pb = (int16_t)(sinA * scaleX / 256.0 * 256.0);
                    int16_t pc = (int16_t)(-sinA * scaleY / 256.0 * 256.0);
                    int16_t pd = (int16_t)(cosA * scaleY / 256.0 * 256.0);
                    
                    // Write destination with offset
                    memory.Write16(dst, (uint16_t)pa);
                    memory.Write16(dst + offset, (uint16_t)pb);
                    memory.Write16(dst + offset * 2, (uint16_t)pc);
                    memory.Write16(dst + offset * 3, (uint16_t)pd);
                    
                    src += 8;                  // Source entries are 8 bytes apart
                    dst += offset * 4;         // Move to next destination entry
                }
                break;
            }
            case 0x10: // BitUnPack
            {
                // R0 = Source, R1 = Dest, R2 = Pointer to UnPackInfo
                // UnPackInfo: 2 bytes SrcLen, 1 byte SrcWidth, 1 byte DestWidth, 4 bytes DataOffset
                uint32_t src = registers[0];
                uint32_t dst = registers[1];
                uint32_t info = registers[2];
                
                uint16_t srcLen = memory.Read16(info);
                uint8_t srcWidth = memory.Read8(info + 2);
                uint8_t dstWidth = memory.Read8(info + 3);
                uint32_t dataOffset = memory.Read32(info + 4);
                
                uint32_t srcMask = (1 << srcWidth) - 1;
                uint32_t dstBuffer = 0;
                int dstBitPos = 0;
                int srcBitPos = 0;
                uint8_t srcByte = 0;
                
                for (uint32_t i = 0; i < srcLen; ) {
                    if (srcBitPos == 0) {
                        srcByte = memory.Read8(src++);
                        i++;
                    }
                    
                    uint32_t val = (srcByte >> srcBitPos) & srcMask;
                    srcBitPos += srcWidth;
                    if (srcBitPos >= 8) srcBitPos = 0;
                    
                    // Apply data offset if value is non-zero or zero-data flag set
                    if (val != 0 || (dataOffset & 0x80000000)) {
                        val += (dataOffset & 0x7FFFFFFF);
                    }
                    
                    dstBuffer |= (val << dstBitPos);
                    dstBitPos += dstWidth;
                    
                    if (dstBitPos >= 32) {
                        memory.Write32(dst, dstBuffer);
                        dst += 4;
                        dstBuffer = 0;
                        dstBitPos = 0;
                    }
                }
                
                // Write any remaining bits
                if (dstBitPos > 0) {
                    memory.Write32(dst, dstBuffer);
                }
                break;
            }
            case 0x11: // LZ77UnCompWram - Decompress LZ77 to WRAM (8-bit writes)
            case 0x12: // LZ77UnCompVram - Decompress LZ77 to VRAM (16-bit writes)
            {
                uint32_t src = registers[0];
                uint32_t dst = registers[1];
                bool toVram = (comment == 0x12);
                

                // Read header: bits 4-7 = compression type (1 = LZ77), bits 8-31 = decompressed size
                uint32_t header = memory.Read32(src);
                uint32_t decompSize = header >> 8;
                
                // Debug: trace LZ77 to VRAM
                if (toVram && (dst & 0xFF000000) == 0x06000000) {
                    std::cout << "[SWI 0x12] LZ77 to VRAM: src=0x" << std::hex << src
                              << " dst=0x" << dst << " size=" << std::dec << decompSize << std::endl;
                }
                src += 4;
                
                uint32_t written = 0;
                uint16_t vramBuffer = 0;
                bool vramBufferFull = false;
                
                while (written < decompSize) {
                    uint8_t flags = memory.Read8(src++);
                    
                    for (int i = 7; i >= 0 && written < decompSize; --i) {
                        if (flags & (1 << i)) {
                            // Compressed block
                            uint8_t byte1 = memory.Read8(src++);
                            uint8_t byte2 = memory.Read8(src++);
                            uint32_t length = ((byte1 >> 4) & 0xF) + 3;
                            uint32_t offset = ((byte1 & 0xF) << 8) | byte2;
                            offset += 1;
                            
                            for (uint32_t j = 0; j < length && written < decompSize; ++j) {
                                uint8_t val = memory.Read8(dst - offset);
                                if (toVram) {
                                    if (!vramBufferFull) {
                                        vramBuffer = val;
                                        vramBufferFull = true;
                                    } else {
                                        vramBuffer |= (val << 8);
                                        memory.Write16(dst & ~1, vramBuffer);
                                        vramBufferFull = false;
                                    }
                                } else {
                                    memory.Write8(dst, val);
                                }
                                dst++;
                                written++;
                            }
                        } else {
                            // Uncompressed byte
                            uint8_t val = memory.Read8(src++);
                            if (toVram) {
                                if (!vramBufferFull) {
                                    vramBuffer = val;
                                    vramBufferFull = true;
                                } else {
                                    vramBuffer |= (val << 8);
                                    memory.Write16(dst & ~1, vramBuffer);
                                    vramBufferFull = false;
                                }
                            } else {
                                memory.Write8(dst, val);
                            }
                            dst++;
                            written++;
                        }
                    }
                }
                break;
            }
            case 0x13: // HuffUnComp - Huffman decompression (based on mGBA)
            {
                // GBA BIOS Huffman Decompression (SWI 0x13)
                // Exact port from mGBA src/gba/bios.c _unHuffman()
                
                uint32_t source = registers[0] & 0xFFFFFFFC;  // Align to 4 bytes
                uint32_t dest = registers[1];
                
                // Read header (4 bytes)
                uint32_t header = memory.Read32(source);
                int remaining = header >> 8;  // Decompressed size
                unsigned bits = header & 0xF;  // 4 or 8 bits per symbol
                
                if (bits == 0) {
                    bits = 8;  // mGBA defaults to 8 if 0
                }
                if (32 % bits || bits == 1) {
                    // Unaligned Huffman not supported
                    break;
                }
                
                // Tree size: (size_byte << 1) + 1 = actual tree table size in bytes
                int treesize = (memory.Read8(source + 4) << 1) + 1;
                
                // Tree base is at source + 5 (after header + tree size byte)
                uint32_t treeBase = source + 5;
                
                // Bitstream starts after tree table
                uint32_t bitSource = source + 5 + treesize;
                
                // Current node pointer, starts at root
                uint32_t nPointer = treeBase;
                
                // Read root node data
                uint8_t node = memory.Read8(nPointer);
                
                int block = 0;
                int bitsSeen = 0;
                
                while (remaining > 0) {
                    // Load next 32-bit word of compressed bitstream
                    uint32_t bitstream = memory.Read32(bitSource);
                    bitSource += 4;
                    
                    // Process all 32 bits
                    for (int bitsRemaining = 32; bitsRemaining > 0 && remaining > 0; --bitsRemaining, bitstream <<= 1) {
                        // Calculate next child address
                        // Offset field is bits 0-5 of node
                        uint32_t offset = node & 0x3F;
                        uint32_t next = (nPointer & ~1u) + offset * 2 + 2;
                        
                        int readBits;
                        
                        if (bitstream & 0x80000000) {
                            // Bit is 1 - go right (Node1)
                            // RTerm is bit 6 - if set, right child is data
                            if (node & 0x40) {
                                // Terminal node - read data
                                readBits = memory.Read8(next + 1);
                            } else {
                                // Non-terminal - continue traversal
                                nPointer = next + 1;
                                node = memory.Read8(nPointer);
                                continue;
                            }
                        } else {
                            // Bit is 0 - go left (Node0)
                            // LTerm is bit 7 - if set, left child is data
                            if (node & 0x80) {
                                // Terminal node - read data
                                readBits = memory.Read8(next);
                            } else {
                                // Non-terminal - continue traversal
                                nPointer = next;
                                node = memory.Read8(nPointer);
                                continue;
                            }
                        }
                        
                        // Accumulate decoded bits into output block
                        // Mask to only use 'bits' bits (4 or 8)
                        block |= (readBits & ((1 << bits) - 1)) << bitsSeen;
                        bitsSeen += bits;
                        
                        // Reset to root for next symbol
                        nPointer = treeBase;
                        node = memory.Read8(nPointer);
                        
                        // Write when we have 32 bits
                        if (bitsSeen == 32) {
                            bitsSeen = 0;
                            memory.Write32(dest, block);
                            dest += 4;
                            remaining -= 4;
                            block = 0;
                        }
                    }
                }
                
                // Update registers like real BIOS
                registers[0] = bitSource;
                registers[1] = dest;
                
                break;
            }
            case 0x14: // RLUnCompWram - Run-Length decompression to WRAM
            case 0x15: // RLUnCompVram - Run-Length decompression to VRAM
            {
                uint32_t src = registers[0];
                uint32_t dst = registers[1];
                bool toVram = (comment == 0x15);
                
                uint32_t header = memory.Read32(src);
                uint32_t decompSize = header >> 8;
                src += 4;
                
                uint32_t written = 0;
                uint16_t vramBuffer = 0;
                bool vramBufferFull = false;
                
                while (written < decompSize) {
                    uint8_t flag = memory.Read8(src++);
                    
                    if (flag & 0x80) {
                        // Compressed run
                        uint32_t length = (flag & 0x7F) + 3;
                        uint8_t val = memory.Read8(src++);
                        
                        for (uint32_t i = 0; i < length && written < decompSize; ++i) {
                            if (toVram) {
                                if (!vramBufferFull) {
                                    vramBuffer = val;
                                    vramBufferFull = true;
                                } else {
                                    vramBuffer |= (val << 8);
                                    memory.Write16(dst & ~1, vramBuffer);
                                    vramBufferFull = false;
                                }
                            } else {
                                memory.Write8(dst, val);
                            }
                            dst++;
                            written++;
                        }
                    } else {
                        // Uncompressed run
                        uint32_t length = (flag & 0x7F) + 1;
                        
                        for (uint32_t i = 0; i < length && written < decompSize; ++i) {
                            uint8_t val = memory.Read8(src++);
                            if (toVram) {
                                if (!vramBufferFull) {
                                    vramBuffer = val;
                                    vramBufferFull = true;
                                } else {
                                    vramBuffer |= (val << 8);
                                    memory.Write16(dst & ~1, vramBuffer);
                                    vramBufferFull = false;
                                }
                            } else {
                                memory.Write8(dst, val);
                            }
                            dst++;
                            written++;
                        }
                    }
                }
                break;
            }
            case 0x16: // Diff8bitUnFilterWram
            case 0x17: // Diff8bitUnFilterVram
            case 0x18: // Diff16bitUnFilter
            {
                // Differential unfilter - used less commonly
                uint32_t src = registers[0];
                uint32_t dst = registers[1];
                
                uint32_t header = memory.Read32(src);
                uint32_t size = header >> 8;
                src += 4;
                
                if (comment == 0x18) {
                    // 16-bit differential
                    uint16_t prev = 0;
                    for (uint32_t i = 0; i < size; i += 2) {
                        uint16_t diff = memory.Read16(src);
                        src += 2;
                        prev += diff;
                        memory.Write16(dst, prev);
                        dst += 2;
                    }
                } else {
                    // 8-bit differential
                    uint8_t prev = 0;
                    for (uint32_t i = 0; i < size; ++i) {
                        uint8_t diff = memory.Read8(src++);
                        prev += diff;
                        memory.Write8(dst++, prev);
                    }
                }
                break;
            }
            case 0x19: // SoundBias - Set sound bias
            {
                // R0 = delay, bias level
                // Not critical for most games
                break;
            }
            case 0x1F: // MidiKey2Freq - MIDI to frequency conversion
            {
                // Used for sound - not critical for gameplay
                // R0 = WaveData pointer, R1 = MIDI key, R2 = Fine adjust
                // Returns frequency in R0
                uint32_t key = registers[1];
                uint32_t fine = registers[2];
                // Approximate conversion
                double freq = 440.0 * pow(2.0, (key - 69 + fine / 256.0) / 12.0);
                registers[0] = (uint32_t)(freq * 2048.0); // Fixed-point result
                break;
            }
            default:
                std::cout << "Unimplemented SWI 0x" << std::hex << comment << " at PC=" << registers[15] << std::endl;
                break;
        }
    }

    void ARM7TDMI::DecodeThumb(uint16_t instruction) {
        // Thumb Instruction Decoding
        
        // Trace VBlank handler button processing - DISABLED for clean output
        /*
        uint32_t execPC = registers[15] - 2;
        
        // Trace only first few button events
        if (execPC >= 0x80014c2 && execPC <= 0x80014e0) {
            // Check if this is button input processing with actual buttons
            if (registers[0] != 0x3ff && registers[0] != 0xfffffc00 && 
                registers[0] != 0xfc000000 && registers[0] != 0xfc00 &&
                registers[0] != 0 && registers[0] != 0xffffffff) {
                static int btnTraceCount = 0;
                if (btnTraceCount++ < 20) {
                    std::cout << "[BTN TRACE] PC=0x" << std::hex << execPC 
                              << " Instr=0x" << instruction
                              << " R0=0x" << registers[0] 
                              << " R1=0x" << registers[1]
                              << " R2=0x" << registers[2]
                              << " R3=0x" << registers[3]
                              << " R4=0x" << registers[4] << std::dec << std::endl;
                }
            }
        }
        */
        
        // Trace main loop - DISABLED
        /*
        uint32_t execPC = registers[15] - 2;
        if (execPC >= 0x08000510 && execPC <= 0x08000550) {
             std::cout << "[MAIN LOOP] PC=0x" << std::hex << execPC << " Instr=0x" << instruction
                       << " R0=" << registers[0] << " R1=" << registers[1] << " R2=" << registers[2]
                       << " CPSR=" << cpsr << std::dec << std::endl;
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
            
            // Debug shift operations at VBlank handler input processing - DISABLED
            /*
            if (registers[15] >= 0x80014c4 && registers[15] <= 0x80014ca) {
                static int shiftLogCount = 0;
                if (shiftLogCount++ < 200 || (val != 0xfffffc00 && val != 0xfc000000)) {
                    std::cout << "[SHIFT at VBlankHandler] PC=0x" << std::hex << registers[15]
                              << " opcode=" << std::dec << opcode << " offset=" << offset
                              << " val=0x" << std::hex << val << " res=0x" << res << std::dec << std::endl;
                }
            }
            */
            
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
                {
                    bool carryIn = (cpsr >> 29) & 1;
                    uint64_t result64 = (uint64_t)val + (uint64_t)op2 + carryIn;
                    res = (uint32_t)result64;
                    SetZN(res);
                    // Set carry if overflow occurred
                    if (result64 > 0xFFFFFFFF) cpsr |= 0x20000000; else cpsr &= ~0x20000000;
                    // Set overflow
                    bool sign1 = (val >> 31) & 1;
                    bool sign2 = (op2 >> 31) & 1;
                    bool signR = (res >> 31) & 1;
                    if (sign1 == sign2 && sign1 != signR) cpsr |= 0x10000000; else cpsr &= ~0x10000000;
                    registers[rd] = res;
                    break;
                }
                case 0x6: // SBC
                {
                    bool carryIn = (cpsr >> 29) & 1;
                    // SBC: Rd = Rd - Rs - NOT(Carry)
                    res = val - op2 - !carryIn;
                    SetZN(res);
                    // Set carry if NO borrow
                    if (val >= (uint64_t)op2 + !carryIn) cpsr |= 0x20000000; else cpsr &= ~0x20000000;
                    // Set overflow
                    bool sign1 = (val >> 31) & 1;
                    bool sign2 = (op2 >> 31) & 1;
                    bool signR = (res >> 31) & 1;
                    if (sign1 != sign2 && sign1 != signR) cpsr |= 0x10000000; else cpsr &= ~0x10000000;
                    registers[rd] = res;
                    break;
                }
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
                // PC as source needs +2 adjustment in Thumb mode
                uint32_t rmVal = (regM == 15) ? (registers[15] + 2) : registers[regM];
                if (regD == 15) {
                     // For ADD PC, Rm: PC is destination, use current registers[15]
                     uint32_t newPC = registers[15] + rmVal;
                     LogBranch(registers[15] - 2, newPC);
                     // In Thumb, ADD to PC should clear bit 0 (PC must be aligned)
                     registers[15] = newPC & 0xFFFFFFFE;
                } else {
                    // Normal ADD Rd, Rm where Rd is not PC
                    registers[regD] += rmVal;
                }
                // Format 5 ADD does NOT set flags
            } else if (opcode == 1) { // CMP Rd, Rm
                // PC as source needs +2 adjustment in Thumb mode
                uint32_t val = (regD == 15) ? (registers[15] + 2) : registers[regD];
                uint32_t op2 = (regM == 15) ? (registers[15] + 2) : registers[regM];
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
                     // PC as source needs +2 adjustment in Thumb mode
                     uint32_t val = (regM == 15) ? (registers[15] + 2) : registers[regM];
                     LogBranch(registers[15] - 2, val);
                     // In Thumb, MOV PC, Rm should clear bit 0 (not interworking on ARM7TDMI)
                     registers[15] = val & 0xFFFFFFFE;
                } else {
                    // PC as source needs +2 adjustment in Thumb mode
                    uint32_t val = (regM == 15) ? (registers[15] + 2) : registers[regM];
                    registers[regD] = val;
                }
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
                    cpsr |= 0x20;  // Set T bit in CPSR
                    registers[15] = target & 0xFFFFFFFE;
                } else {
                    thumbMode = false;
                    cpsr &= ~0x20; // Clear T bit in CPSR
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

            // DEBUG: Trace ALL STR to 0x03xxxxxx range (disabled)
            // if (!L && !B && (addr >> 24) == 0x03) {
            //     static int strCount = 0;
            //     if (strCount++ < 100 || ((addr & 0x7FFF) >= 0x1500 && (addr & 0x7FFF) < 0x1600)) {
            //         std::cout << "[F9 STR] PC=0x" << std::hex << (registers[15] - 2) 
            //                   << " instr=0x" << instruction
            //                   << " Addr=0x" << addr << " Val=0x" << registers[rd] 
            //                   << " (R" << std::dec << rd << "=[R" << rn << "+#" << (imm*4) << "])" << std::endl;
            //     }
            // }

            if (L) { // Load
                if (B) {
                    registers[rd] = memory.Read8(addr);
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
            
            if (L) {
                registers[rd] = memory.Read16(addr);
            }
            else {
                // Debug STRH to button state memory at 0x3002b94 - DISABLED
                /*
                if (addr == 0x3002b94) {
                    static int strhCount = 0;
                    if (strhCount++ < 200 || (registers[rd] & 0xFFFF) != 0xfc00) {
                        std::cout << "[STRH to button state] addr=0x" << std::hex << addr 
                                  << " rd=" << std::dec << rd << " val=0x" << std::hex << (registers[rd] & 0xFFFF)
                                  << " PC=0x" << registers[15] << std::dec << std::endl;
                    }
                }
                */
                memory.Write16(addr, registers[rd] & 0xFFFF);
            }
        }
        // Format 9: SP-relative Load/Store (CRITICAL - was missing!)
        // 1001 xxxx xxxx xxxx
        else if ((instruction & 0xF000) == 0x9000) {
            bool L = (instruction >> 11) & 1;  // 0=STR, 1=LDR
            uint32_t rd = (instruction >> 8) & 0x7;
            uint32_t imm = instruction & 0xFF;
            
            uint32_t addr = registers[13] + (imm * 4);  // SP + offset*4
            
            if (L) {
                // LDR Rd, [SP, #imm]
                registers[rd] = memory.Read32(addr);
            } else {
                // STR Rd, [SP, #imm]
                memory.Write32(addr, registers[rd]);
            }
        }
        // Format 11: Load/Store Multiple
        // 1100 xxxx xxxx xxxx
        else if ((instruction & 0xF000) == 0xC000) {
            bool L = (instruction >> 11) & 1;
            uint32_t rb = (instruction >> 8) & 0x7;
            uint8_t rList = instruction & 0xFF;
            
            uint32_t addr = registers[rb];
            uint32_t startAddr = addr;
            
            // DEBUG: Trace STMIA to DMA2 control registers (0x40000C8-0x40000D3) - disabled
            // bool traceDMA2 = (!L && addr >= 0x40000C8 && addr <= 0x40000D3);
            // if (traceDMA2) {
            //     std::cout << "[STMIA DMA2] PC=0x" << std::hex << (registers[15] - 2)
            //               << " R" << std::dec << rb << "=0x" << std::hex << addr
            //               << " rList=0x" << (int)rList;
            //     for (int i=0; i<8; ++i) {
            //         if ((rList >> i) & 1) {
            //             std::cout << " R" << i << "=0x" << registers[i];
            //         }
            //     }
            //     std::cout << std::dec << std::endl;
            // }
            
            // DEBUG: Trace STMIA to 0x3001500 - disabled
            // bool traceMixbuf = (!L && (addr & 0xFF000000) == 0x03000000 && (addr & 0x7FFF) >= 0x1500 && (addr & 0x7FFF) < 0x1600);
            // if (traceMixbuf) {
            //     std::cout << "[STMIA] PC=0x" << std::hex << (registers[15] - 2) 
            //               << " R" << std::dec << rb << "=0x" << std::hex << addr
            //               << " rList=0x" << (int)rList << std::dec << std::endl;
            // }
            
            for (int i=0; i<8; ++i) {
                if ((rList >> i) & 1) {
                    if (L) { // Load
                        registers[i] = memory.Read32(addr);
                    } else { // Store
                        // if (traceMixbuf) {
                        //     std::cout << "  Store R" << i << "=0x" << std::hex << registers[i] 
                        //               << " to 0x" << addr << std::dec << std::endl;
                        // }
                        memory.Write32(addr, registers[i]);
                    }
                    addr += 4;
                }
            }
            
            // Write-back (debug disabled)
            // if (traceMixbuf) {
            //     std::cout << "  Writeback R" << std::dec << rb << ": 0x" << std::hex << startAddr 
            //               << " -> 0x" << addr << std::dec << std::endl;
            // }
            registers[rb] = addr;
        }
        // Format 13: Add Offset to Stack Pointer
        // 1011 0000 xxxx xxxx
        else if ((instruction & 0xFF00) == 0xB000) {
            bool S = (instruction >> 7) & 1;
            uint32_t imm = instruction & 0x7F;
            imm *= 4;
            
            uint32_t oldSP = registers[13];
            if (S) registers[13] = oldSP - imm; // SUB
            else   registers[13] = oldSP + imm; // ADD
            
            if (registers[13] < 0x02000000) {
                 std::cerr << "[SP WARNING] Format 13: SP changed from 0x" << std::hex << oldSP << " to 0x" << registers[13] << " at PC=0x" << (registers[15]-2) << std::dec << std::endl;
            }
        }
        // Format 14: Push/Pop Registers
        // 1011 x10x xxxx xxxx
        else if ((instruction & 0xF600) == 0xB400) {
            bool L = (instruction >> 11) & 1; // 0=Push, 1=Pop
            bool R = (instruction >> 8) & 1; // PC/LR
            uint8_t rList = instruction & 0xFF;
            
            uint32_t oldSP = registers[13];
            
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
                    registers[15] &= ~1; 
                    currentAddr += 4;
                }
                
                registers[13] = currentAddr;
            }
            
            if (registers[13] < 0x02000000) {
                 std::cerr << "[SP WARNING] Format 14: SP changed from 0x" << std::hex << oldSP << " to 0x" << registers[13] << " at PC=0x" << (registers[15]-2) << std::dec << std::endl;
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
                // std::cout << "[BL High] PC=" << std::hex << (registers[15]-2) << " LR=" << registers[14] << std::dec << std::endl;
            } else { // Second instruction (Low)
                uint32_t nextPC = registers[15] - 2; // Instruction address + 2 (already incremented)
                uint32_t target = registers[14] + (offset << 1);
                LogBranch(nextPC, target);
                
                // Debug BL
                if (nextPC == 0x080005b2) {
                     std::cout << "[BL Low] PC=" << std::hex << nextPC << " LR_in=" << registers[14] << " Offset=" << offset << " Target=" << target << std::dec << std::endl;
                }

                registers[14] = (nextPC + 2) | 1; // LR = Return Address + 1 (Thumb) -> Next Instruction
                registers[15] = target;
            }
        }
        else {
            // Unknown Thumb instruction
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
