#pragma once

#include <cstdint>

/**
 * ARM7TDMI CPU Constants - Symbolic definitions replacing magic numbers
 * Derived from ARM Architecture Reference Manual and GBATEK specifications
 */
namespace AIO::Emulator::GBA::ARM7TDMIConstants {

    // ===== CPU MODES =====
    // CPSR[4:0] - Current Processor Status Register bits
    namespace CPUMode {
        constexpr uint32_t USER       = 0x10;  // User mode
        constexpr uint32_t FIQ        = 0x11;  // FIQ mode
        constexpr uint32_t IRQ        = 0x12;  // IRQ mode
        constexpr uint32_t SUPERVISOR = 0x13; // Supervisor mode
        constexpr uint32_t ABORT      = 0x17; // Abort mode
        constexpr uint32_t UNDEFINED  = 0x1B; // Undefined mode
        constexpr uint32_t SYSTEM     = 0x1F; // System mode
        constexpr uint32_t MASK       = 0x1F; // Mode bits mask [4:0]
    }

    // ===== CPSR FLAG BITS =====
    // CPSR is a 32-bit register: [31:28] Cond, [7] I, [6] F, [5] T, [4:0] Mode
    namespace CPSR {
        // Condition Flags
        constexpr uint32_t FLAG_N = 0x80000000; // [31] Negative/Sign flag
        constexpr uint32_t FLAG_Z = 0x40000000; // [30] Zero flag
        constexpr uint32_t FLAG_C = 0x20000000; // [29] Carry/Borrow flag
        constexpr uint32_t FLAG_V = 0x10000000; // [28] Overflow flag
        
        // Control Bits
        constexpr uint32_t FLAG_I = 0x00000080; // [7] IRQ disable (0=IRQ enabled)
        constexpr uint32_t FLAG_F = 0x00000040; // [6] FIQ disable (0=FIQ enabled)
        constexpr uint32_t FLAG_T = 0x00000020; // [5] Thumb mode (1=Thumb, 0=ARM)
        
        // Mode bits
        constexpr uint32_t MODE_MASK = 0x0000001F; // [4:0] Processor mode
    }

    // ===== ARM CONDITION CODES =====
    // ARM instruction bits [31:28] specify execution condition
    namespace Condition {
        constexpr uint32_t EQ = 0x0;  // 0000 - Equal (Z=1)
        constexpr uint32_t NE = 0x1;  // 0001 - Not Equal (Z=0)
        constexpr uint32_t CS = 0x2;  // 0010 - Carry Set (C=1)
        constexpr uint32_t HS = 0x2;  // (alias for CS)
        constexpr uint32_t CC = 0x3;  // 0011 - Carry Clear (C=0)
        constexpr uint32_t LO = 0x3;  // (alias for CC)
        constexpr uint32_t MI = 0x4;  // 0100 - Minus (N=1)
        constexpr uint32_t PL = 0x5;  // 0101 - Plus (N=0)
        constexpr uint32_t VS = 0x6;  // 0110 - Overflow Set (V=1)
        constexpr uint32_t VC = 0x7;  // 0111 - Overflow Clear (V=0)
        constexpr uint32_t HI = 0x8;  // 1000 - Higher (C=1 && Z=0)
        constexpr uint32_t LS = 0x9;  // 1001 - Lower or Same (C=0 || Z=1)
        constexpr uint32_t GE = 0xA;  // 1010 - Greater or Equal (N==V)
        constexpr uint32_t LT = 0xB;  // 1011 - Less Than (N!=V)
        constexpr uint32_t GT = 0xC;  // 1100 - Greater Than (Z=0 && N==V)
        constexpr uint32_t LE = 0xD;  // 1101 - Less or Equal (Z=1 || N!=V)
        constexpr uint32_t AL = 0xE;  // 1110 - Always
        constexpr uint32_t NV = 0xF;  // 1111 - Never (reserved)
    }

    // ===== ARM INSTRUCTION MASKS AND PATTERNS =====
    // Used to decode 32-bit ARM instructions
    namespace ARMInstructionFormat {
        // Generic 4-bit encoding patterns
        constexpr uint32_t COND_MASK        = 0xF0000000; // [31:28]
        constexpr uint32_t COND_SHIFT       = 28;
        
        // Instruction type identification (top 4 bits after condition)
        constexpr uint32_t TYPE_MASK        = 0x0F000000; // [27:24]
        
        // Branch and Exchange: xxxx 0001 0010 xxxx xxxx xxxx 0001 xxxx
        constexpr uint32_t BX_MASK          = 0x0FFFFFF0;
        constexpr uint32_t BX_PATTERN       = 0x012FFF10;
        
        // Branch: xxxx 101x xxxx xxxx xxxx xxxx xxxx xxxx
        constexpr uint32_t B_MASK           = 0x0E000000;
        constexpr uint32_t B_PATTERN        = 0x0A000000;
        constexpr uint32_t BL_BIT           = 0x01000000; // [24] L bit
        constexpr uint32_t B_OFFSET_MASK    = 0x00FFFFFF; // [23:0] signed offset
        
        // Data Processing: xxxx 00xx xxxx xxxx xxxx xxxx xxxx xxxx
        constexpr uint32_t DP_MASK          = 0x0C000000;
        constexpr uint32_t DP_PATTERN       = 0x00000000;
        constexpr uint32_t DP_OPCODE_MASK   = 0x01E00000; // [24:21]
        constexpr uint32_t DP_OPCODE_SHIFT  = 21;
        constexpr uint32_t DP_S_BIT         = 0x00100000; // [20] S (set flags)
        constexpr uint32_t DP_RN_MASK       = 0x000F0000; // [19:16]
        constexpr uint32_t DP_RN_SHIFT      = 16;
        constexpr uint32_t DP_RD_MASK       = 0x0000F000; // [15:12]
        constexpr uint32_t DP_RD_SHIFT      = 12;
        constexpr uint32_t DP_I_BIT         = 0x02000000; // [25] I (immediate)
        
        // Multiply: xxxx 0000 00xx xxxx xxxx 1001 xxxx
        constexpr uint32_t MUL_MASK         = 0x0FC000F0;
        constexpr uint32_t MUL_PATTERN      = 0x00000090;
        
        // Multiply Long: xxxx 0000 1xxx xxxx xxxx 1001 xxxx
        constexpr uint32_t MULL_MASK        = 0x0F8000F0;
        constexpr uint32_t MULL_PATTERN     = 0x00800090;
        
        // Single Data Transfer: xxxx 01xx xxxx xxxx xxxx xxxx xxxx xxxx
        constexpr uint32_t SDT_MASK         = 0x0C000000;
        constexpr uint32_t SDT_PATTERN      = 0x04000000;
        
        // Block Data Transfer: xxxx 100x xxxx xxxx xxxx xxxx xxxx xxxx
        constexpr uint32_t BDT_MASK         = 0x0E000000;
        constexpr uint32_t BDT_PATTERN      = 0x08000000;
        
        // Software Interrupt: xxxx 1111 xxxx xxxx xxxx xxxx xxxx xxxx
        constexpr uint32_t SWI_MASK         = 0x0F000000;
        constexpr uint32_t SWI_PATTERN      = 0x0F000000;
        constexpr uint32_t SWI_COMMENT_MASK = 0x00FFFFFF; // [23:0]
    }

    // ===== ARM DATA PROCESSING OPCODES =====
    // CPSR[24:21] in Data Processing instructions
    namespace DPOpcode {
        constexpr uint32_t AND = 0x0;  // AND - Logical AND
        constexpr uint32_t EOR = 0x1;  // EOR - Logical Exclusive OR
        constexpr uint32_t SUB = 0x2;  // SUB - Subtract
        constexpr uint32_t RSB = 0x3;  // RSB - Reverse Subtract
        constexpr uint32_t ADD = 0x4;  // ADD - Add
        constexpr uint32_t ADC = 0x5;  // ADC - Add with Carry
        constexpr uint32_t SBC = 0x6;  // SBC - Subtract with Carry
        constexpr uint32_t RSC = 0x7;  // RSC - Reverse Subtract with Carry
        constexpr uint32_t TST = 0x8;  // TST - Test (AND, discard result)
        constexpr uint32_t TEQ = 0x9;  // TEQ - Test Exclusive OR
        constexpr uint32_t CMP = 0xA;  // CMP - Compare (SUB, discard result)
        constexpr uint32_t CMN = 0xB;  // CMN - Compare Negative (ADD, discard result)
        constexpr uint32_t ORR = 0xC;  // ORR - Logical OR
        constexpr uint32_t MOV = 0xD;  // MOV - Move
        constexpr uint32_t BIC = 0xE;  // BIC - Bit Clear (AND with NOT)
        constexpr uint32_t MVN = 0xF;  // MVN - Move NOT
    }

    // ===== THUMB INSTRUCTION MASKS =====
    // Used to decode 16-bit Thumb instructions
    namespace ThumbInstructionFormat {
        // Format 1: Move Shifted Register (0000 xxxx xxxx xxxx)
        constexpr uint16_t FMT1_MASK        = 0xE000;
        constexpr uint16_t FMT1_PATTERN     = 0x0000;
        constexpr uint16_t FMT1_OPCODE_MASK = 0x1800; // [12:11]
        constexpr uint16_t FMT1_OPCODE_SHIFT = 11;
        constexpr uint16_t FMT1_OFFSET_MASK = 0x07C0; // [10:6]
        constexpr uint16_t FMT1_OFFSET_SHIFT = 6;
        
        // Format 2: Add/Subtract (0001 1xxx xxxx xxxx)
        constexpr uint16_t FMT2_MASK        = 0xF800;
        constexpr uint16_t FMT2_PATTERN     = 0x1800;
        constexpr uint16_t FMT2_I_BIT       = 0x0400; // [10] I (immediate)
        constexpr uint16_t FMT2_SUB_BIT     = 0x0200; // [9] SUB (0=ADD, 1=SUB)
        
        // Format 3: Move/Compare/Add/Subtract Immediate (001x xxxx xxxx xxxx)
        constexpr uint16_t FMT3_MASK        = 0xE000;
        constexpr uint16_t FMT3_PATTERN     = 0x2000;
        constexpr uint16_t FMT3_OPCODE_MASK = 0x1800; // [12:11]
        constexpr uint16_t FMT3_OPCODE_SHIFT = 11;
        
        // Format 4: ALU Operations (0100 00xx xxxx xxxx)
        constexpr uint16_t FMT4_MASK        = 0xFC00;
        constexpr uint16_t FMT4_PATTERN     = 0x4000;
        constexpr uint16_t FMT4_OPCODE_MASK = 0x03C0; // [9:6]
        constexpr uint16_t FMT4_OPCODE_SHIFT = 6;
        
        // Format 5: Hi Register Operations (0100 01xx xxxx xxxx)
        constexpr uint16_t FMT5_MASK        = 0xFC00;
        constexpr uint16_t FMT5_PATTERN     = 0x4400;
        constexpr uint16_t FMT5_OPCODE_MASK = 0x0300; // [9:8]
        constexpr uint16_t FMT5_OPCODE_SHIFT = 8;
        constexpr uint16_t FMT5_H1_BIT      = 0x0080; // [7] H1
        constexpr uint16_t FMT5_H2_BIT      = 0x0040; // [6] H2
        
        // Format 6: PC-Relative Load (0100 1xxx xxxx xxxx)
        constexpr uint16_t FMT6_MASK        = 0xF800;
        constexpr uint16_t FMT6_PATTERN     = 0x4800;
        
        // Format 7: Load/Store Register Offset (0101 xxx0 xxxx xxxx)
        constexpr uint16_t FMT7_MASK        = 0xF200;
        constexpr uint16_t FMT7_PATTERN     = 0x5000;
        constexpr uint16_t FMT7_B_BIT       = 0x0400; // [10] B (byte/word)
        constexpr uint16_t FMT7_L_BIT       = 0x0800; // [11] L (load/store)
        
        // Format 8: Load/Store Sign-Extended (0101 xx1x xxxx xxxx)
        constexpr uint16_t FMT8_MASK        = 0xF200;
        constexpr uint16_t FMT8_PATTERN     = 0x5200;
        constexpr uint16_t FMT8_H_BIT       = 0x0800; // [11] H
        constexpr uint16_t FMT8_S_BIT       = 0x0400; // [10] S
        
        // Format 9: Load/Store Immediate Offset (011x xxxx xxxx xxxx)
        constexpr uint16_t FMT9_MASK        = 0xE000;
        constexpr uint16_t FMT9_PATTERN     = 0x6000;
        constexpr uint16_t FMT9_B_BIT       = 0x1000; // [12] B
        constexpr uint16_t FMT9_L_BIT       = 0x0800; // [11] L
        
        // Format 10: Load/Store Halfword (1000 xxxx xxxx xxxx)
        constexpr uint16_t FMT10_MASK       = 0xF000;
        constexpr uint16_t FMT10_PATTERN    = 0x8000;
        constexpr uint16_t FMT10_L_BIT      = 0x0800; // [11] L
        
        // Format 11: SP-Relative Load/Store (1001 xxxx xxxx xxxx)
        constexpr uint16_t FMT11_MASK       = 0xF000;
        constexpr uint16_t FMT11_PATTERN    = 0x9000;
        constexpr uint16_t FMT11_L_BIT      = 0x0800; // [11] L
        
        // Format 12: Load Address (1010 xxxx xxxx xxxx)
        constexpr uint16_t FMT12_MASK       = 0xF000;
        constexpr uint16_t FMT12_PATTERN    = 0xA000;
        constexpr uint16_t FMT12_SP_BIT     = 0x0800; // [11] SP (0=PC, 1=SP)
        
        // Format 13: Adjust SP (1011 0000 xxxx xxxx)
        constexpr uint16_t FMT13_MASK       = 0xFF00;
        constexpr uint16_t FMT13_PATTERN    = 0xB000;
        constexpr uint16_t FMT13_S_BIT      = 0x0080; // [7] S (0=ADD, 1=SUB)
        
        // Format 14: Push/Pop (1011 x10x xxxx xxxx)
        constexpr uint16_t FMT14_MASK       = 0xF600;
        constexpr uint16_t FMT14_PATTERN    = 0xB400;
        constexpr uint16_t FMT14_L_BIT      = 0x0800; // [11] L (load/store)
        constexpr uint16_t FMT14_PC_LR_BIT  = 0x0100; // [8] PC/LR
        
        // Format 15: Multiple Load/Store (1100 xxxx xxxx xxxx)
        constexpr uint16_t FMT15_MASK       = 0xF000;
        constexpr uint16_t FMT15_PATTERN    = 0xC000;
        constexpr uint16_t FMT15_L_BIT      = 0x0800; // [11] L
        
        // Format 16: Conditional Branch (1101 xxxx xxxx xxxx)
        constexpr uint16_t FMT16_MASK       = 0xF000;
        constexpr uint16_t FMT16_PATTERN    = 0xD000;
        constexpr uint16_t FMT16_COND_MASK  = 0x0F00; // [11:8]
        constexpr uint16_t FMT16_COND_SHIFT = 8;
        
        // Format 17: Software Interrupt (1101 1111 xxxx xxxx)
        constexpr uint16_t FMT17_MASK       = 0xFF00;
        constexpr uint16_t FMT17_PATTERN    = 0xDF00;
        
        // Format 18: Unconditional Branch (1110 xxxx xxxx xxxx)
        constexpr uint16_t FMT18_MASK       = 0xF800;
        constexpr uint16_t FMT18_PATTERN    = 0xE000;
        
        // Format 19: Long Branch with Link (1111 xxxx xxxx xxxx)
        constexpr uint16_t FMT19_MASK       = 0xF000;
        constexpr uint16_t FMT19_PATTERN    = 0xF000;
        constexpr uint16_t FMT19_H_BIT      = 0x0800; // [11] H
    }

    // ===== THUMB ALU OPCODES =====
    namespace ThumbALUOpcode {
        constexpr uint32_t AND = 0x0;
        constexpr uint32_t EOR = 0x1;
        constexpr uint32_t LSL = 0x2;
        constexpr uint32_t LSR = 0x3;
        constexpr uint32_t ASR = 0x4;
        constexpr uint32_t ADC = 0x5;
        constexpr uint32_t SBC = 0x6;
        constexpr uint32_t ROR = 0x7;
        constexpr uint32_t TST = 0x8;
        constexpr uint32_t NEG = 0x9;
        constexpr uint32_t CMP = 0xA;
        constexpr uint32_t CMN = 0xB;
        constexpr uint32_t ORR = 0xC;
        constexpr uint32_t MUL = 0xD;
        constexpr uint32_t BIC = 0xE;
        constexpr uint32_t MVN = 0xF;
    }

    // ===== SHIFT CONSTANTS =====
    namespace Shift {
        constexpr uint32_t LSL = 0x0;  // Logical Shift Left
        constexpr uint32_t LSR = 0x1;  // Logical Shift Right
        constexpr uint32_t ASR = 0x2;  // Arithmetic Shift Right
        constexpr uint32_t ROR = 0x3;  // Rotate Right
        constexpr uint32_t RRX = 0x4;  // Rotate Right Extended (special case)
    }

    // ===== REGISTER INDICES =====
    namespace Register {
        constexpr uint32_t R0  = 0;
        constexpr uint32_t R1  = 1;
        constexpr uint32_t R2  = 2;
        constexpr uint32_t R3  = 3;
        constexpr uint32_t R4  = 4;
        constexpr uint32_t R5  = 5;
        constexpr uint32_t R6  = 6;
        constexpr uint32_t R7  = 7;
        constexpr uint32_t R8  = 8;
        constexpr uint32_t R9  = 9;
        constexpr uint32_t R10 = 10;
        constexpr uint32_t R11 = 11;
        constexpr uint32_t R12 = 12;
        constexpr uint32_t SP  = 13;  // Stack Pointer (R13)
        constexpr uint32_t LR  = 14;  // Link Register (R14)
        constexpr uint32_t PC  = 15;  // Program Counter (R15)
    }

    // ===== INTERRUPT VECTORS =====
    // Memory addresses where CPU jumps on exception
    namespace ExceptionVector {
        constexpr uint32_t RESET      = 0x00000000;
        constexpr uint32_t UNDEFINED  = 0x00000004;
        constexpr uint32_t SWI        = 0x00000008;
        constexpr uint32_t PREFETCH   = 0x0000000C;
        constexpr uint32_t DATA_ABORT = 0x00000010;
        constexpr uint32_t IRQ        = 0x00000018;
        constexpr uint32_t FIQ        = 0x0000001C;
    }

} // namespace AIO::Emulator::GBA::ARM7TDMIConstants
