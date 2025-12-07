#include "emulator/switch/CpuCore.h"
#include "emulator/switch/MemoryManager.h"
#include <iostream>
#include <sstream>
#include <iomanip>

namespace AIO::Emulator::Switch {

    CpuCore::CpuCore(MemoryManager& mem) : memory(mem) {
        Reset();
    }

    CpuCore::~CpuCore() {
    }

    void CpuCore::Reset() {
        x.fill(0);
        sp = 0;
        pc = 0;
        pstate = 0;
    }

    void CpuCore::Run(int cycles) {
        for (int i = 0; i < cycles; ++i) {
            uint32_t instr = memory.Read32(pc);
            ExecuteInstruction(instr);
            pc += 4;
        }
    }

    void CpuCore::ExecuteInstruction(uint32_t instr) {
        // ARMv8 A64 Instruction Decoding
        // Top-level encoding:
        // [28:25]
        // 00xx: Unallocated / Reserved
        // 100x: Data Processing - Immediate
        // 101x: Branch, Exception Generation and System instructions
        // x1x0: Loads and Stores
        // x101: Data Processing - Register
        // x111: Data Processing - SIMD and Floating Point

        // Detect NOP (0xD503201F)
        if (instr == 0xD503201F) {
            return;
        }

        uint32_t group = (instr >> 25) & 0xF;

        // Data Processing - Immediate (100x -> 8 or 9)
        if ((group & 0xE) == 0x8) {
            // Add/Subtract (Immediate)
            // 31: sf, 30: op, 29: S, 28-24: 10001
            if (((instr >> 24) & 0x1F) == 0x11) {
                bool sf = (instr >> 31) & 1;
                bool op = (instr >> 30) & 1; // 0=Add, 1=Sub
                bool S = (instr >> 29) & 1;  // Set flags
                
                int rd = instr & 0x1F;
                int rn = (instr >> 5) & 0x1F;
                uint64_t imm = (instr >> 10) & 0xFFF;
                int shift = (instr >> 22) & 3;
                if (shift == 1) imm <<= 12;

                uint64_t op1 = x[rn];
                uint64_t op2 = imm;
                uint64_t result = op ? (op1 - op2) : (op1 + op2);

                if (!sf) result &= 0xFFFFFFFF; // 32-bit
                
                if (rd != 31) x[rd] = result; // x31 is SP/ZR depending on context, usually ZR for arithmetic
                
                // std::cout << "[CPU] " << (op ? "SUB" : "ADD") << " X" << rd << ", X" << rn << ", #" << imm << std::endl;
                return;
            }
            
            // MOVZ/MOVN/MOVK (Wide Immediate)
            // 100100
            if (((instr >> 23) & 0x3F) == 0x25) {
                bool sf = (instr >> 31) & 1;
                int opc = (instr >> 29) & 3;
                int hw = (instr >> 21) & 3;
                uint64_t imm16 = (instr >> 5) & 0xFFFF;
                int rd = instr & 0x1F;
                
                int shift = hw * 16;
                
                if (opc == 0) { // MOVZ
                    uint64_t result = imm16 << shift;
                    if (rd != 31) x[rd] = result;
                    // std::cout << "[CPU] MOVZ X" << rd << ", #" << imm16 << ", LSL #" << shift << std::endl;
                } else if (opc == 3) { // MOVK
                    uint64_t mask = ~(0xFFFFULL << shift);
                    uint64_t result = (x[rd] & mask) | (imm16 << shift);
                    if (rd != 31) x[rd] = result;
                    // std::cout << "[CPU] MOVK X" << rd << ", #" << imm16 << ", LSL #" << shift << std::endl;
                }
                return;
            }
        }

        // Branch, Exception, System (101x -> A or B)
        if ((group & 0xE) == 0xA) {
            // Unconditional Branch (Immediate) - B
            // 0001 01
            if (((instr >> 26) & 0x3F) == 0x05) {
                int64_t offset = instr & 0x3FFFFFF;
                // Sign extend 26 bits
                if (offset & 0x2000000) offset |= 0xFFFFFFFFFFC00000;
                offset <<= 2; // Multiply by 4
                
                pc += offset - 4; // -4 because Run() adds 4
                // std::cout << "[CPU] B " << offset << std::endl;
                return;
            }
            
            // Branch with Link (BL)
            // 1001 01
            if (((instr >> 26) & 0x3F) == 0x25) {
                int64_t offset = instr & 0x3FFFFFF;
                if (offset & 0x2000000) offset |= 0xFFFFFFFFFFC00000;
                offset <<= 2;
                
                x[30] = pc + 4; // LR
                pc += offset - 4;
                // std::cout << "[CPU] BL " << offset << std::endl;
                return;
            }
            
            // Unconditional Branch (Register) - BR, BLR, RET
            // 1101 011
            if (((instr >> 25) & 0x7F) == 0x6B) {
                int opc = (instr >> 21) & 0xF;
                int op2 = (instr >> 16) & 0x1F;
                int op3 = (instr >> 10) & 0x3F;
                int rn = (instr >> 5) & 0x1F;
                int op4 = instr & 0x1F;
                
                if (opc == 0 && op2 == 0x1F && op3 == 0 && op4 == 0) { // BR
                    pc = x[rn] - 4;
                    return;
                }
                if (opc == 2 && op2 == 0x1F && op3 == 0 && op4 == 0) { // RET
                    pc = x[rn] - 4; // Usually X30 (LR)
                    // std::cout << "[CPU] RET" << std::endl;
                    return;
                }
            }
            
            // Exception Generation (SVC)
            // 1101 0100
            if (((instr >> 21) & 0xFF) == 0xD4) {
                int opc = (instr >> 5) & 7;
                int op2 = (instr >> 2) & 7;
                int ll = instr & 3;
                uint16_t imm = (instr >> 5) & 0xFFFF;
                
                if (opc == 0 && op2 == 0 && ll == 1) { // SVC
                    // std::cout << "[CPU] SVC #" << imm << std::endl;
                    // TODO: Trigger Exception Handler / HLE
                    return;
                }
            }
        }

        // Loads and Stores (x1x0)
        if ((group & 0x5) == 0x4) { // Bit 27=1, Bit 25=0
             // Load/Store Register (Immediate Post-Indexed)
             // ... simplified check for LDR (Immediate)
             // 1x11 1000
             if (((instr >> 24) & 0xF8) == 0xB8) {
                 int size = (instr >> 30) & 3;
                 int opc = (instr >> 22) & 3;
                 int rn = (instr >> 5) & 0x1F;
                 int rt = instr & 0x1F;
                 int imm9 = (instr >> 12) & 0x1FF;
                 
                 // Sign extend imm9
                 int64_t offset = imm9;
                 if (offset & 0x100) offset |= 0xFFFFFFFFFFFFFE00;
                 
                 uint64_t addr = x[rn] + offset; // Pre-indexed or Unsigned Offset?
                 // This is a simplification.
                 
                 if (opc == 1) { // LDR
                     if (size == 3) { // 64-bit
                         uint64_t val = memory.Read64(addr);
                         if (rt != 31) x[rt] = val;
                     } else if (size == 2) { // 32-bit
                         uint32_t val = memory.Read32(addr);
                         if (rt != 31) x[rt] = val;
                     }
                 } else if (opc == 0) { // STR
                     if (size == 3) {
                         memory.Write64(addr, x[rt]);
                     } else if (size == 2) {
                         memory.Write32(addr, (uint32_t)x[rt]);
                     }
                 }
                 return;
             }
        }

        // For now, just log unknown instructions occasionally
        // std::cout << "[CPU] Executing 0x" << std::hex << instr << " at PC=0x" << pc << std::dec << std::endl;
    }

    uint64_t CpuCore::GetX(int index) const {
        if (index >= 0 && index < 31) return x[index];
        return 0;
    }

    void CpuCore::SetX(int index, uint64_t value) {
        if (index >= 0 && index < 31) x[index] = value;
    }

    std::string CpuCore::GetStateString() {
        std::stringstream ss;
        ss << "PC: 0x" << std::hex << pc << " SP: 0x" << sp << "\n";
        for (int i = 0; i < 31; i += 4) {
            ss << "X" << std::dec << i << ": " << std::hex << x[i] << " ";
            if (i+1 < 31) ss << "X" << std::dec << i+1 << ": " << std::hex << x[i+1] << " ";
            if (i+2 < 31) ss << "X" << std::dec << i+2 << ": " << std::hex << x[i+2] << " ";
            if (i+3 < 31) ss << "X" << std::dec << i+3 << ": " << std::hex << x[i+3] << " ";
            ss << "\n";
        }
        return ss.str();
    }

}
